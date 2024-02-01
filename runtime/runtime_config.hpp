// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>

#include "verif/verif.hpp"
#include "common/base.hpp"
#include "common/param_lib.hpp"
#include "netlist/tt_backend_api_types.hpp"
#include "perf_lib/perf_descriptor.hpp"

namespace fs = std::experimental::filesystem;

struct tt_runtime_config : tt_backend_config
{
    bool skip_io_init            = false;  // do not initialize io queues
    bool skip_device_init        = false;  // do not reset device or init epoch queues
    bool skip_device_shutdown    = false;  // do not shutdown device or clean up system states
    bool skip_overlap_checks     = false;  // do not check for queue overlap
    bool concurrent_mode         = false;
    
    // Device runtime configuration
    tt_device_params device_params;

    // Performance infra configurations
    perf::PerfDesc perf_desc;

    bool do_compile() const {
        return (mode == DEVICE_MODE::CompileAndRun) or (mode == DEVICE_MODE::CompileOnly);
    };

    bool do_run() const {
        return (mode == DEVICE_MODE::CompileAndRun) or (mode == DEVICE_MODE::RunOnly);
    }

    bool do_shutdown() const {
        return (mode != DEVICE_MODE::CompileOnly) && (!skip_device_shutdown);
    }
    
    bool valid_device_desc() const {
        if (this->soc_descriptor_path == "") {
            return false;
        }
        return fs::exists(this->soc_descriptor_path);
    }

    bool supports_harvesting() const {
        return (arch == tt::ARCH::GRAYSKULL || arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::WORMHOLE);
    }

    void parse_runtime_args();
    std::string netlist_path;

    tt_runtime_config() {}
    tt_runtime_config(vector<string> &cmdline_args);
};

inline perf::PerfDesc get_perf_desc(const std::string &perf_desc_args, const std::string &netlist_path) {
    std::vector<std::string> args;
    verif_args::split_string_into_vector(args, perf_desc_args, " ");
    perf::PerfDesc perf_desc(args, netlist_path);
    return perf_desc;
}

inline tt_runtime_config get_runtime_config(
    perf::PerfDesc perf_desc, std::string output_dir, int opt_level, bool run_silicon) {
    tt_runtime_config config;
    config.type = (run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim);
    config.optimization_level = opt_level;
    config.perf_desc = perf_desc;
    config.output_dir = output_dir;
    return config;
};

inline tt_runtime_config get_runtime_config(const tt_backend_config &base_config, const string &netlist_path) {
    log_assert(base_config.type == tt::DEVICE::Silicon or base_config.type == tt::DEVICE::Versim, "Expected device type Silicon or Versim.");
    tt_runtime_config runtime_config;
    // Upcast to base config and use copy constructor
    tt_backend_config &copy_to_config = runtime_config;
    copy_to_config = base_config;
    runtime_config.netlist_path = netlist_path;
    runtime_config.perf_desc = get_perf_desc(base_config.perf_desc_args, netlist_path);
    runtime_config.parse_runtime_args();
    return runtime_config;
};

inline tt_runtime_config get_runtime_config(const tt_runtime_config &base_config, const string &netlist_path) {
    tt_runtime_config runtime_config(base_config);
    runtime_config.netlist_path = netlist_path;
    return runtime_config;
};

inline void tt_runtime_config::parse_runtime_args() {
    std::vector<std::string> args;
    verif_args::split_string_into_vector(args, runtime_args, " ");
    std::tie(skip_io_init, args) = verif_args::has_command_option_and_remaining_args(args, "--skip-io-init");
    std::tie(skip_overlap_checks,  args) = verif_args::has_command_option_and_remaining_args(args, "--skip-overlap-check");
    std::tie(concurrent_mode,      args) = verif_args::has_command_option_and_remaining_args(args, "--concurrent-mode");
    std::tie(skip_device_init,     args) = verif_args::has_command_option_and_remaining_args(args, "--skip-device-init");
    std::tie(skip_device_shutdown, args) = verif_args::has_command_option_and_remaining_args(args, "--skip-device-shutdown");
}

inline tt_runtime_config::tt_runtime_config(vector<string> &cmdline_args) {
    
    bool run_silicon;
    bool run_only;
    std::string vcd_dump_cores = "";
    std::tie(output_dir, cmdline_args) =
        verif_args::get_command_option_and_remaining_args(cmdline_args, "--outdir", "");
    // Setup
    if (output_dir == "") {
        output_dir = verif_filesystem::get_output_dir(__FILE__, cmdline_args);
    }
    std::tie(optimization_level,        cmdline_args) = verif_args::get_command_option_uint32_and_remaining_args(cmdline_args, "--O", 0);
    std::tie(run_silicon,               cmdline_args) = verif_args::has_command_option_and_remaining_args(cmdline_args, "--silicon");
    std::tie(run_only,                  cmdline_args) = verif_args::has_command_option_and_remaining_args(cmdline_args, "--run-only");
    std::tie(netlist_path,              cmdline_args) = verif_args::get_command_option_and_remaining_args(cmdline_args, "--netlist", "default_netlist.yaml");
    std::tie(cluster_descriptor_path,   cmdline_args) = verif_args::get_command_option_and_remaining_args(cmdline_args, "--network-desc", "");
    std::tie(vcd_dump_cores,            cmdline_args) = verif_args::get_command_option_and_remaining_args(cmdline_args, "--vcd-dump-cores", "");
    std::tie(soc_descriptor_path,       cmdline_args) = verif_args::get_command_option_and_remaining_args(cmdline_args, "--soc-desc", "");
    std::tie(cluster_descriptor_path,   cmdline_args) = verif_args::get_command_option_and_remaining_args(cmdline_args, "--cluster_desc", "");

    if (run_only) {
        mode = DEVICE_MODE::RunOnly;
    }

    verif_args::split_string_into_vector(device_params.vcd_dump_cores, vcd_dump_cores, ",");

    type = (run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim);
    perf_desc = perf::PerfDesc(cmdline_args, netlist_path);
}
