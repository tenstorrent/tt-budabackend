// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

using namespace verif::random;

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;

    tt_tensor tensor(md);
    tensor.init_to_tile_id(1.0f, entry_idx*16.0f);

    // adjust for input format precision loss
    tensor.pack_data();
    tensor.unpack_data();
    return tensor;
}

bool compare_tilized_tensors(tt_tensor &observed, tt_tensor &expected) {
    bool match = true;
    if (observed != expected) {
        for (int wi = 0; wi < expected.getw(); ++wi) {
            for (int zi = 0; zi < expected.getz(); ++zi) {
                for (int ri = 0; ri < expected.getrt(); ++ri) {
                    for (int ci = 0; ci < expected.getct(); ++ci) {
                        if (expected.tile_tensor[wi][zi][ri][ci] != observed.tile_tensor[wi][zi][ri][ci]) {
                            log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                            log_info(tt::LogTest, "Diff\n{}", expected.tile_tensor[wi][zi][ri][ci].get_diff_string(observed.tile_tensor[wi][zi][ri][ci]));
                        }
                    }
                }
            }
        }
        match = false;
    }
    log_assert(match, "Tile mismatch!");
    return match;
}

int main(int argc, char **argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_pybuda_ram since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;

    int seed;
    int num_pushes;
    bool run_silicon;
    std::string netlist_path;
    std::string arch_name;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(seed, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", 1);
    std::tie(num_pushes, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-pushes", 1);
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    std::tie(arch_name, args)    = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    verif_args::validate_remaining_args(args);

    tt_rnd_set_seed(seed);

    // Backend setup
    tt_backend_config config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = arch_name == "grayskull" ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
        .optimization_level = 0, // disable epoch preload since we don't run program in this test
        .output_dir = output_dir};

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);
    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);

    // ----------------------------------------------------------------
    // PyBuda API for initializing backend
    // ----------------------------------------------------------------
    log_assert(backend.initialize() == tt::DEVICE_STATUS_CODE::Success, "initialize failed");

    for (auto& queue : workload.queues) {
        tt_queue_info& queue_info = queue.second.my_queue_info;
        if (queue_info.type != IO_TYPE::RandomAccess) {
            continue;
        }

        // ----------------------------------------------------------------
        // PyBuda API for querying IO descriptors
        // ----------------------------------------------------------------
        tt_dram_io_desc ram_desc = backend.get_queue_descriptor(queue_info.name);
        tt::backend::translate_addresses(ram_desc);

        // RAM is default initialized like a queue, ie. empty
        log_assert(tt::io::is_dram_queue_empty(ram_desc, queue_info, cluster), "Expected empty ram");

        bool batched_push = false;
        unordered_map<int, tt_tensor> expected_output= {};
        vector<int> accessed_ptrs;

        for (int i=0; i<num_pushes; i++) {
            // Set up input and expected output, identity for unary datacopy
            tt_tensor input = get_tilized_tensor(queue_info, i, batched_push);

            int ram_ptr = tt_rnd_int(0, queue_info.entries-1);
            expected_output[ram_ptr] = input;
            accessed_ptrs.push_back(ram_ptr);

            // Mimic python runtime to generate input tensor to pytorch tensor descriptor
            aligned_vector<float> host_stored_tensor;
            aligned_vector<uint16_t> host_stored_tensor_half;
            tt::tt_PytorchTensorDesc py_in_tensor_desc = input.get_data_format() == DataFormat::Float32 ? 
                tt::io::get_pytorch_tensor_desc<float>(input, host_stored_tensor) :
                tt::io::get_pytorch_tensor_desc<uint16_t>(input, host_stored_tensor_half);
            py_in_tensor_desc.owner = tt::OWNERSHIP::Frontend;

            // ----------------------------------------------------------------
            // PyBuda API for random input write
            // ----------------------------------------------------------------
            tt::backend::push_input(ram_desc, py_in_tensor_desc, !batched_push, 0, ram_ptr);

            log_assert(tt::io::is_dram_queue_empty(ram_desc, queue_info, cluster), "Expected empty ram"); // random access doesn't update ptrs

            // ----------------------------------------------------------------
            // PyBuda API for random output read
            // ----------------------------------------------------------------
            tt::tt_PytorchTensorDesc py_out_tensor_desc;
            tt::backend::get_output(ram_desc, py_out_tensor_desc, !batched_push, 0, ram_ptr);

            log_assert(tt::io::is_dram_queue_empty(ram_desc, queue_info, cluster), "Expected empty ram"); // random access doesn't update ptrs

            // ----------------------------------------------------------------
            // TEST check
            // Immediately check that pushed data can be random accessed correctly
            // ----------------------------------------------------------------

            // Mimic python runtime to extract output tensor from pytorch tensor descriptor
            tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(ram_desc, py_out_tensor_desc, batched_push);
            tt_tensor expected = expected_output.at(ram_ptr);

            // Compare against expected output
            bool tensor_match = compare_tilized_tensors(observed, expected);
            pass &= tensor_match;
            log_info(tt::LogTest, "Read-After-Write check {}[{}] in == out {}", queue_info.name, ram_ptr, tensor_match ? "PASSED" : "FAILED");

            tt::backend::free_tensor(py_out_tensor_desc);
        }
        log_info(tt::LogTest, "Read-After-Write \t{} \t{} for {} inputs", queue_info.name, pass ? "PASSED" : "FAILED", num_pushes);

        for (int i=0; i<num_pushes; i++) {
            int ram_ptr = tt_rnd_pick<int>(accessed_ptrs);

            // ----------------------------------------------------------------
            // PyBuda API for random output read
            // ----------------------------------------------------------------
            tt::tt_PytorchTensorDesc py_out_tensor_desc;
            tt::backend::get_output(ram_desc, py_out_tensor_desc, !batched_push, 0, ram_ptr);

            log_assert(tt::io::is_dram_queue_empty(ram_desc, queue_info, cluster), "Expected empty ram"); // random access doesn't update ptrs

            // ----------------------------------------------------------------
            // TEST check
            // Randomly peek output ensure no wrong slot writes has ever occured
            // ----------------------------------------------------------------

            // Mimic python runtime to extract output tensor from pytorch tensor descriptor
            tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(ram_desc, py_out_tensor_desc, batched_push);
            tt_tensor expected = expected_output.at(ram_ptr);

            // Compare against expected output
            bool tensor_match = compare_tilized_tensors(observed, expected);
            pass &= tensor_match;
            log_info(tt::LogTest, "Random-Read check {}[{}] in == out {}", queue_info.name, ram_ptr, tensor_match ? "PASSED" : "FAILED");

            tt::backend::free_tensor(py_out_tensor_desc);
        }
        log_info(tt::LogTest, "Random-Reads \t{} \t{} for {} outputs", queue_info.name, pass ? "PASSED" : "FAILED", num_pushes);

    }

    // ----------------------------------------------------------------
    // PyBuda API for stopping backend
    // ----------------------------------------------------------------
    backend.finish();

    log_assert(pass, "Test Failed");
    return 0;
}

