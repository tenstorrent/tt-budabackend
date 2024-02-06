// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime.hpp"
#include "runtime_io.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"
#include "common/data_binary_lib.hpp"
#include <cstdlib>

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format, uint32_t tile_height = 32, uint32_t tile_width = 32, bool init_to_zero = false) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;
    md.shape.tile_height = tile_height;
    md.shape.tile_width = tile_width;
    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    tt_tensor tensor(md);
    if(init_to_zero) {
        tensor.init_to_input_value(0);
    }
    else {
        tensor.randomize(0, 1);
    }
    return tensor;
}

bool compare_tilized_tensors(tt_tensor &observed, tt_tensor &expected, bool verbose_compare) {

    bool match = observed == expected;

    for (int wi = 0; wi < expected.getw(); ++wi) {
        for (int zi = 0; zi < expected.getz(); ++zi) {
            for (int ri = 0; ri < expected.getrt(); ++ri) {
                for (int ci = 0; ci < expected.getct(); ++ci) {
                    if (!match && expected.tile_tensor[wi][zi][ri][ci] != observed.tile_tensor[wi][zi][ri][ci]) {
                        log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_info(tt::LogTest, "Diff\n{}", expected.tile_tensor[wi][zi][ri][ci].get_diff_string(observed.tile_tensor[wi][zi][ri][ci]));
                    }else if (verbose_compare){
                        log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_info(tt::LogTest, "Observed/Expected\n{}", expected.tile_tensor[wi][zi][ri][ci].get_string());
                    }
                }
            }
        }
    }

    if(match) log_info(tt::LogTest, "Tensors match");

    // Keep going and show more comparisons in verbose mode. Don't die here.
    if (!verbose_compare){
        log_assert(match, "Tile mismatch!");
    }
    return match;
}


void create_and_binarize_tensors(std::unordered_map<std::string, std::vector<tt_PytorchTensorDesc>>& tensors, std::vector<aligned_vector<float>>& host_data, const std::vector<tt_dram_io_desc>& q_descs, tt_runtime_workload& workload, uint32_t num_loops, const std::string& output_dir) {
    DataFormat host_format = DataFormat::Float32;
    if(!fs::exists(output_dir)) {
        fs::create_directory(output_dir);
    }
    for(const auto& desc : q_descs) {
        auto q_info = workload.queues[desc.queue_name].my_queue_info;
        tensors.insert({desc.queue_name, {}});
        for(int i = 0 ; i < num_loops; i++) {
            log_info(tt::LogTest, "Generating tilized binaries for {} loop {}", desc.queue_name, i);
            tt_tensor input = get_tilized_tensor(q_info, 0, true, host_format, desc.tile_height, desc.tile_width);
            host_data.push_back({});
            const auto tensor = tt::io::get_pytorch_tensor_desc<float>(input, host_data.back());
            tensors.at(desc.queue_name).push_back(tensor);
            auto tilized_tensor = tt::backend::tilize_tensor(desc, tensor);
            std::string filename = output_dir + "/" + desc.queue_name + std::to_string(i) + ".tbin";
            tt::backend::binarize_tensor(tilized_tensor, filename);
            tt::backend::free_tensor(tilized_tensor); // Free Backend allocated Tilized tensor

        }

    }
}

int main(int argc, char** argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_offline_tilizer since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    DataFormat host_format = DataFormat::Float32;
    std::vector<std::string> args(argv, argv + argc);

    std::string netlist_path = "";
    uint32_t num_loops = 4;
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_muti_input.yaml");
    std::tie(num_loops, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--loops", 4);

    tt_backend_config compile_only_config = {
        .type = tt::DEVICE::Silicon,
        .arch = tt::ARCH::WORMHOLE,
        .mode = DEVICE_MODE::CompileOnly,
        .optimization_level = 4,
        .output_dir =  verif_filesystem::get_output_dir(__FILE__, args),
        
    };
    
    std::unordered_map<std::string, std::vector<tt_PytorchTensorDesc>> py_tensors;
    std::unordered_map<std::string, std::vector<tt_tensor>> expected;
    std::unordered_map<std::string, std::vector<tt_tensor>> observed;
    // Create Compile Only Backend to generate tilized tensor binaries offline
    std::shared_ptr<tt_backend> backend_compile_api = tt_backend::create(netlist_path, compile_only_config);
    tt_backend &backend_compile = *backend_compile_api.get();
    tt_runtime_workload workload = *tt::io::get_workload(netlist_path);

    std::vector<aligned_vector<float>> host_data = {};
    log_assert(backend_compile.initialize() == tt::DEVICE_STATUS_CODE::Success, "initialize failed");

    vector<tt_dram_io_desc> input_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = backend_compile.get_queue_descriptor(it.first);
            input_io_desc.push_back(io_desc);
        }
    }
    create_and_binarize_tensors(py_tensors, host_data, input_io_desc, workload, num_loops, "tilized_tensors");
    backend_compile.finish(); // Compile completed
    
    tt_backend_config config  = {
        .type = tt::DEVICE::Silicon,
        .arch = tt::ARCH::WORMHOLE,
        .mode = DEVICE_MODE::CompileAndRun,
        .optimization_level = 4,
        .output_dir =  verif_filesystem::get_output_dir(__FILE__, args),
    };

    // Create new backend to access device
    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();

    for(auto& desc: input_io_desc) {
        tt::backend::translate_addresses(desc);
    }

    log_assert(backend.initialize() == tt::DEVICE_STATUS_CODE::Success, "initialize failed");
    log_info(tt::LogTest, "Pushing and reading back Pytorch Tensors");
    for(int i = 0; i < num_loops; i++) {
        for(const auto& q_desc : input_io_desc) {
            tt::backend::push_input(q_desc, py_tensors.at(q_desc.queue_name).at(i), true, 1);
            tt::tt_PytorchTensorDesc output_desc;
            tt::backend::get_output(q_desc, output_desc, false, 1);
            tt::backend::pop_output(q_desc, false, 1);
            if(expected.find(q_desc.queue_name) == expected.end()) {
                expected.insert({q_desc.queue_name, {}});
            }
            expected.at(q_desc.queue_name).push_back(tt::io::reconstruct_tensor_from_untilized(q_desc, output_desc, true));
            tt::backend::free_tensor(output_desc);
            tt::backend::free_tensor(py_tensors.at(q_desc.queue_name).at(i));
        }
    }
    log_info(tt::LogTest, "Clearing input queues");
    for(const auto& q_desc : input_io_desc) {
        auto q_info = workload.queues[q_desc.queue_name].my_queue_info;
        tt_tensor zero_input = get_tilized_tensor(q_info, 0, false, host_format, q_desc.tile_height, q_desc.tile_width, true);
        aligned_vector<float> zero_host_data;
        const auto zero_tensor = tt::io::get_pytorch_tensor_desc<float>(zero_input, zero_host_data);
        for(int i = 0; i < q_desc.bufq_num_slots; i++) {
            tt::backend::push_input(q_desc, zero_tensor, true, 10);
            tt::backend::pop_output(q_desc, true, 10);
        }
    }
    log_info(tt::LogTest, "Pushing and reading back Tilized Tensors");
    for(int i = 0; i < num_loops; i++) {
        for(const auto& q_desc : input_io_desc) {
            tt::tt_PytorchTensorDesc output_desc;
            tt_TilizedTensorDesc tilized_tensor;
            tilized_tensor.buf_size_bytes = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, q_desc.layout == IO_LAYOUT::Tilized, 
                                            q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width) * q_desc.input_count;
            tilized_tensor.num_buffers = q_desc.bufq_grid_dim_c * q_desc.bufq_grid_dim_r;
            tilized_tensor.format = q_desc.bufq_target_format;

            std::string filename = "tilized_tensors/" + q_desc.queue_name + std::to_string(i) + ".tbin";

            tt::backend::debinarize_tensor(tilized_tensor, filename); // Debinarize: Read tensor data from file
            tt::backend::push_input(q_desc, tilized_tensor, 1);
            tt::backend::get_output(q_desc, output_desc, false, 1);
            tt::backend::pop_output(q_desc, false, 1);
            if(observed.find(q_desc.queue_name) == observed.end()) {
                observed.insert({q_desc.queue_name, {}});
            }
            observed.at(q_desc.queue_name).push_back(tt::io::reconstruct_tensor_from_untilized(q_desc, output_desc, true));
            tt::backend::free_tensor(tilized_tensor); // Free backend owned tilized_tensor
            tt::backend::free_tensor(output_desc);
        }
    }
    for(auto& q : observed) {
        log_info(tt::LogTest, "Comparing Tilized Tensor Push with Pytorch Tensor push {}", q.first);
        for(int i = 0; i < num_loops; i++) {
            compare_tilized_tensors(q.second.at(i), expected.at(q.first).at(i), false);
        }
    }
}