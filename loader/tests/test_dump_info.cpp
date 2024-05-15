// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"
#include "param_lib.hpp"
#include "tt_backend_api.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;

    tt_tensor tensor(md);
    tensor = 1.0f;
    return tensor;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool skip_run = false;
    std::tie(skip_run, args)  = verif_args::has_command_option_and_remaining_args(args, "--skip-run");

    // Runtime setup
    tt_runtime_config config(args);

    verif_args::validate_remaining_args(args);

    // ----------------------------------------------------------------
    // Dump Backend Params
    // ----------------------------------------------------------------

    log_info(tt::LogTest, "Dumping backend params for '{}' and '{}'", config.soc_descriptor_path, config.cluster_descriptor_path);

    auto &store = tt::param::tt_backend_params::get(config.soc_descriptor_path, config.cluster_descriptor_path);
    auto desc = tt::param::populate_runtime_system_params(store.params, config.soc_descriptor_path, config.cluster_descriptor_path);

    std::string num_devices = tt::backend::get_backend_param("system-device-num_mmio_devices", std::get<0>(desc), std::get<1>(desc), "");
    std::string num_params = tt::backend::get_backend_param("*", std::get<0>(desc), std::get<1>(desc), "");
    std::cout << "Total devices = " << std::stoi(num_devices) << std::endl;
    std::cout << "Total params = " << std::stoi(num_params) << std::endl;

    // ----------------------------------------------------------------
    // Dump Static Analysis Passes
    // ----------------------------------------------------------------

    tt_runtime_workload workload(config.netlist_path, config);
    workload.print_cross_chip_e2e_queues();

    // ----------------------------------------------------------------
    // Run the workload
    // ----------------------------------------------------------------

    if (!skip_run) {
        tt_runtime runtime(config.netlist_path, config);

        // Start device and statically assemble binaries
        log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected target device to be initialized successfully");

        vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &get_tilized_tensor, -1, true/*force_sw_tilize*/);

        // Interactively run programs
        for (auto const& program : runtime.get_workload()->programs) {
            log_assert(runtime.run_program(program.first, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfully on target backend");
        }
        // Shutdown device
        log_assert(runtime.finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target device to close successfully");

        // Get performance data
        vector<tt::EpochPerfInfo> perf = runtime.get_perf_info();
        log_info(tt::LogTest, "|{:-^20}|{:-^20}|{:-^20}|", "Program", "Graph", "Tensors/s");
        for (auto &epoch : perf) {
            log_info(tt::LogTest, "|{:^20}|{:^20}|{:^20.2f}|", epoch.program_name, epoch.graph_name, epoch.num_inputs_per_second);
        }
    }

    log_info(tt::LogTest, "Ringo!");
    return 0;
}
