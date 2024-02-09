// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;

    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    tt_tensor tensor(md);
    tensor.init_to_tile_id(1.0f, entry_idx*16.0f);
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
        match &= observed == expected;
    }
    log_assert(match, "Tile mismatch!");
    return match;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;

    int opt_level, num_pushes;
    bool run_silicon, batched_push;
    std::string netlist_path;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 2);
    std::tie(num_pushes, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-pushes", 1);
    std::tie(batched_push, args) = verif_args::has_command_option_and_remaining_args(args, "--batched-push");
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    verif_args::validate_remaining_args(args);

    netlist_workload_data netlist_workload(netlist_path);

    // Backend setup
    tt_backend_config config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = netlist_workload.device_info.arch,
        .optimization_level = opt_level,
        .output_dir = output_dir};

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);

    // ----------------------------------------------------------------
    // PyBuda API for initializing backend
    // ----------------------------------------------------------------
    log_assert(backend.initialize() == tt::DEVICE_STATUS_CODE::Success, "Intitialize failed");

    // ----------------------------------------------------------------
    // PyBuda API for querying IO descriptors
    // ----------------------------------------------------------------
    vector<tt_dram_io_desc> input_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            input_io_desc.push_back(io_desc);
        }
    }

    std::vector<tt_tensor> expected_output;

    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);

    for (tt_dram_io_desc &io_desc : input_io_desc) {
        tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;

        for (int i=0; i<num_pushes; i++) {

            // Set up input and expected output, identity for unary datacopy
            tt_tensor input = get_tilized_tensor(queue_info, i, batched_push, queue_info.data_format);
            expected_output.push_back(input);

            // Mimic python runtime to generate input tensor to pytorch tensor descriptor
            aligned_vector<float> host_stored_tensor;
            aligned_vector<uint16_t> host_stored_tensor_half;
            tt::tt_PytorchTensorDesc py_tensor_desc = input.get_data_format() == DataFormat::Float32 ? 
                tt::io::get_pytorch_tensor_desc<float>(input, host_stored_tensor) :
                tt::io::get_pytorch_tensor_desc<uint16_t>(input, host_stored_tensor_half);
            py_tensor_desc.owner = tt::OWNERSHIP::Frontend;

            // ----------------------------------------------------------------
            // PyBuda API for pushing input
            // ----------------------------------------------------------------
            auto push_status = tt::backend::push_input(io_desc, py_tensor_desc, !batched_push, 0);
            log_assert(push_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on this push");
        }
    }

    // ----------------------------------------------------------------
    // PyBuda API for running programs
    // ----------------------------------------------------------------
    for (auto const& program : workload.programs) {
        backend.run_program(program.first, {});
    }
    backend.wait_for_idle();

    // ----------------------------------------------------------------
    // PyBuda API for querying IO descriptors
    // ----------------------------------------------------------------
    vector<tt_dram_io_desc> output_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input != "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            output_io_desc.push_back(io_desc);
        }
    }

    for (tt_dram_io_desc &io_desc : output_io_desc) {
        int output_idx = 0;

        tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;
        int batch_size = batched_push ? queue_info.input_count : 1;

        while (!tt::io::is_queue_empty(io_desc, queue_info, cluster)) {

            // ----------------------------------------------------------------
            // PyBuda API for popping output
            //
            // let's test host untilizer's t-slice stacking operations
            // peek at the output using different settings and compare against expected
            // ----------------------------------------------------------------
            tt::tt_PytorchTensorDesc py_desc;

            {   // #1 - hstack 3 + vstack 2
                io_desc.hstack_factor = 3;
                io_desc.vstack_factor = 2;
                io_desc.stack_row_major = true;
                log_assert(tt::backend::get_output(io_desc, py_desc, !batched_push, 0) == tt::DEVICE_STATUS_CODE::Success, "get_output failed");
                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt::backend::free_tensor(py_desc);

                tt_tensor expected = expected_output.at(output_idx);
                expected = expected.reshape_z_dim_into_c_dim(3);
                expected = expected.reshape_z_dim_into_r_dim(2);

                bool tensor_match = compare_tilized_tensors(observed, expected);
                pass &= tensor_match;
                log_info(tt::LogTest, "Output #{} tstack test #1 for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
            }

            {   // #2 - vstack 2 + hstack 3
                io_desc.hstack_factor = 3;
                io_desc.vstack_factor = 2;
                io_desc.stack_row_major = false;
                log_assert(tt::backend::get_output(io_desc, py_desc, !batched_push, 0) == tt::DEVICE_STATUS_CODE::Success, "get_output failed");
                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt::backend::free_tensor(py_desc);

                tt_tensor expected = expected_output.at(output_idx);
                expected = expected.reshape_z_dim_into_r_dim(2);
                expected = expected.reshape_z_dim_into_c_dim(3);

                bool tensor_match = compare_tilized_tensors(observed, expected);
                pass &= tensor_match;
                log_info(tt::LogTest, "Output #{} tstack test #2 for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
            }

            {   // #3 - hstack 2 + tstack 3
                io_desc.hstack_factor = 2;
                io_desc.vstack_factor = 1;
                io_desc.stack_row_major = false;
                log_assert(tt::backend::get_output(io_desc, py_desc, !batched_push, 0) == tt::DEVICE_STATUS_CODE::Success, "get_output failed");
                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt::backend::free_tensor(py_desc);

                tt_tensor expected = expected_output.at(output_idx);
                expected = expected.reshape_z_dim_into_c_dim(2);

                bool tensor_match = compare_tilized_tensors(observed, expected);
                pass &= tensor_match;
                log_info(tt::LogTest, "Output #{} tstack test #3 for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
            }


            {   // #4 - vstack 3 + tstack 2
                io_desc.hstack_factor = 1;
                io_desc.vstack_factor = 3;
                io_desc.stack_row_major = true;
                log_assert(tt::backend::get_output(io_desc, py_desc, !batched_push, 0) == tt::DEVICE_STATUS_CODE::Success, "get_output failed");
                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt::backend::free_tensor(py_desc);

                tt_tensor expected = expected_output.at(output_idx);
                expected = expected.reshape_z_dim_into_r_dim(3);

                bool tensor_match = compare_tilized_tensors(observed, expected);
                pass &= tensor_match;
                log_info(tt::LogTest, "Output #{} tstack test #4 for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
            }

            {   // #5 - hstack 6
                io_desc.hstack_factor = 6;
                io_desc.vstack_factor = 1;
                io_desc.stack_row_major = true;
                log_assert(tt::backend::get_output(io_desc, py_desc, !batched_push, 0) == tt::DEVICE_STATUS_CODE::Success, "get_output failed");
                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt::backend::free_tensor(py_desc);

                tt_tensor expected = expected_output.at(output_idx);
                expected = expected.reshape_z_dim_into_c_dim(6);

                bool tensor_match = compare_tilized_tensors(observed, expected);
                pass &= tensor_match;
                log_info(tt::LogTest, "Output #{} tstack test #5 for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
            }

            auto pop_status = tt::backend::pop_output(io_desc, !batched_push, 0);
            log_assert(pop_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on pop");

            output_idx++;
        }

        log_assert(expected_output.size() == output_idx, "Did not compare against all expected_output tensors.");
    }

    // ----------------------------------------------------------------
    // PyBuda API for stopping backend
    // ----------------------------------------------------------------
    backend.finish();
    log_assert(pass, "Test failed");
    return 0;
}

