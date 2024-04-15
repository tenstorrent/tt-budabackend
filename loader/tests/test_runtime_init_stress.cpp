// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "runtime.hpp"
#include "verif.hpp"
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    std::string netlist_path;
    bool run_silicon = false;
    int opt_level;
    int num_loops;
    bool run_only = false;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args)      = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 1);
    std::tie(num_loops, args)      = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-loops", 10);
    std::tie(run_silicon, args)    = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(run_only, args)       = verif_args::has_command_option_and_remaining_args(args, "--run-only");
    std::tie(netlist_path, args)   = verif_args::get_command_option_and_remaining_args(args, "--netlist", "default_netlist.yaml");

    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);

    // Runtime setup
    for (int i=0; i < num_loops; i++) {

        log_info(tt::LogTest, "======= Running stress test loop {} of {} ============================", i+1, num_loops);

        tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);

        if (run_only || i > 0) {
            config.mode = DEVICE_MODE::RunOnly;
            config.cluster_descriptor_path = output_dir + "/cluster_desc.yaml";
        }

        tt_runtime runtime(netlist_path, config);
        log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized successfully");

        runtime.finish();

    }

    return 0;
}

