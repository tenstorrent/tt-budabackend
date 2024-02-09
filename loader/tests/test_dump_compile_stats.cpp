// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

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

    // config.device_params = {.vcd_dump_cores = {"1-1"}};
    tt_runtime runtime(config.netlist_path, config);

    tt_compile_result compile_result;
    // Start device and statically assemble binaries
    log_assert(runtime.initialize(&compile_result) == tt::DEVICE_STATUS_CODE::Success, "Expected target backend to be initialized");
    if (!skip_run) {
        bool force_sw_tilize = true;

        vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &get_tilized_tensor, -1, force_sw_tilize);

        // Interactively run programs
        for (auto const& program : runtime.get_workload()->programs) {
            log_assert(runtime.run_program(program.first, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
        }
        // Shutdown device
        log_assert(runtime.finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target backend to be closed");
    }
    log_info(tt::LogTest, "tt success!");
    return 0;
}

