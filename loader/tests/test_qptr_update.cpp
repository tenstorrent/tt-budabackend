// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(const tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format = tt::DataFormat::Float16) {
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

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);


    string input_args_flattened = flatten_input_args(args);
    
    int opt_level = 4;
    bool run_inline_updates = false;
    int num_loops = 1;

    std::tie(num_loops, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-loops", 1);
    std::tie(opt_level, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 4);
    std::tie(run_inline_updates, args) = verif_args::has_command_option_and_remaining_args(args, "--inline-update");

    std::string netlist_path;
    if(run_inline_updates) {
        netlist_path = "loader/tests/loop_on_device/erisc_varinst_inline.yaml";
    } else{
        netlist_path = "loader/tests/loop_on_device/erisc_varinst_external.yaml";
    }
    log_info(tt::LogVerif, "Running {} Q Ptr update on device test at Opt level {}", run_inline_updates ? "Inline" : "External", opt_level);
    tt_backend_config config = {
    .type = tt::DEVICE::Silicon,
    .arch = tt::ARCH::WORMHOLE,
    .optimization_level = opt_level,
    .output_dir = output_dir,
    .perf_desc_args=input_args_flattened,
    };

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);
    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);
    backend.initialize();

    // More stress testing. This test caught a bug.
    for (int loop_idx=0; loop_idx < num_loops; loop_idx++) {

        log_info(tt::LogTest, "Running test loop: {} of {} now...", loop_idx+1, num_loops);

        for (const auto& it : workload.queues) {
            if (it.second.my_queue_info.input == "HOST") {
                tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
                tt::backend::translate_addresses(io_desc);
                auto input_tensor = get_tilized_tensor(it.second.my_queue_info, 0, false);
                aligned_vector<uint16_t> fp16_data = {};
                tt_PytorchTensorDesc flat_tensor = tt::io::get_pytorch_tensor_desc<uint16_t>(input_tensor, fp16_data);
                tt::backend::push_input(io_desc, flat_tensor, false, 10);
                tt::backend::push_input(io_desc, flat_tensor, false, 10);
            }
        }

        for (std::string program_name : workload.program_order) {
            log_assert(backend.run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Run program failed");
        }
        backend.wait_for_idle();

        for(const auto& it : workload.queues) {
            if(it.second.my_queue_info.input != "HOST") {
                auto queue_info = it.second.my_queue_info;
                for(auto& buffer : queue_info.alloc_info) {
                    uint32_t expected_wptr = 0;
                    if(buffer.channel == 0) {
                        expected_wptr = 7;
                    } else {
                        expected_wptr = 100;
                    }
                    std::vector<uint32_t> wptr_vec = {0};
                    tt_target_dram dram_loc = {queue_info.target_device, buffer.channel, 0};
                    cluster -> read_dram_vec(wptr_vec, dram_loc, buffer.address + 4, 4);
                    uint32_t observed_wptr = wptr_vec[0];
                    log_assert(expected_wptr == observed_wptr, 
                                    "ERISC producer to output buffer at channel {} and address {} did not update the wptr correctly. Expected {}, Observed {}", buffer.channel, buffer.address, expected_wptr, observed_wptr);
                    log_info(tt::LogVerif, "wptr {} matches expected for buffer at channel {} and address {}", observed_wptr, buffer.channel, buffer.address);
                }
            }
        }

    }

    backend.finish();

}