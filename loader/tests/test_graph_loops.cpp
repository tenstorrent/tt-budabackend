// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "runtime.hpp"
#include "verif.hpp"
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool run_silicon;
    bool run_only;
    bool wfi_between_programs;
    int opt_level;
    int cache_hits, cache_misses, cache_preloads, epoch_barriers, full_grid_syncs, epoch_id_alias_hazards;

    std::string netlist_path;
    std::string arch_name;
    std::string program_name;
    std::string outdir;

    std::tie(opt_level, args)      = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 1);
    std::tie(run_silicon, args)    = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(cache_hits, args)     = verif_args::get_command_option_uint32_and_remaining_args(args, "--epoch-cache-hits", -1);
    std::tie(cache_misses, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--epoch-cache-misses", -1);
    std::tie(cache_preloads, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--epoch-cache-preloads", -1);
    std::tie(epoch_barriers, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--epoch-barriers", -1);
    std::tie(full_grid_syncs, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--full-grid-syncs", -1);
    std::tie(epoch_id_alias_hazards, args)  = verif_args::get_command_option_uint32_and_remaining_args(args, "--epoch-id-alias-hazards", -1);
    std::tie(netlist_path, args)            = verif_args::get_command_option_and_remaining_args(args, "--netlist", "default_netlist.yaml");
    std::tie(program_name, args)            = verif_args::get_command_option_and_remaining_args(args, "--program", ""); // Run a single program from NL.
    std::tie(wfi_between_programs, args)    = verif_args::has_command_option_and_remaining_args(args, "--wait-for-idle-between-programs");
    std::tie(outdir, args)                  = verif_args::get_command_option_and_remaining_args(args, "--outdir", "");
    const std::string output_dir = outdir == "" ? verif_filesystem::get_output_dir(__FILE__, args) : outdir;

    std::tie(run_only, args)                = verif_args::has_command_option_and_remaining_args(args, "--run-only");

    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "invalid arch");
    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);

    if (run_only) {
        config.mode = DEVICE_MODE::RunOnly;
    }

    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    // Runtime run all programs
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized successfully");

    bool force_sw_tilize = true;

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor, -1, force_sw_tilize);

    // Support for running all programs in NL or single program (for debug/bringup testing). Program order is broken actually.
    if (program_name == "") {
        for (auto prog_name : workload.program_order) {
            runtime.run_program(prog_name, {});
            if (wfi_between_programs) {
                runtime.wait_for_idle();
            }
        }
    } else {
        runtime.run_program(program_name, {});
    }

    // Test that this function call is safe to do when WC is disabled, or WC is empty.
    runtime.loader->flush_all_wc_epoch_queues_to_dram();
    runtime.loader->flush_all_wc_epoch_queues_to_dram();

    runtime.finish();

    // Optional coverage/checking on event counters. Careful, these are quite sensitive to optimization level.
    std::cout << "Observed epoch binary cache hit " << runtime.loader->event_counters.at("epoch_cache_hit") << " times" << std::endl;
    std::cout << "Expected epoch binary cache hit " << cache_hits << " times" << std::endl;
    if (cache_hits != -1){
        log_assert(runtime.loader->event_counters.at("epoch_cache_hit") == cache_hits, "epoch_cache_hit event mismatch");
    }

    std::cout << "Observed epoch binary cache miss " << runtime.loader->event_counters.at("epoch_cache_miss") << " times" << std::endl;
    std::cout << "Expected epoch binary cache miss " << cache_misses << " times" << std::endl;
    if (cache_misses != -1){
        log_assert(runtime.loader->event_counters.at("epoch_cache_miss") == cache_misses, "epoch_cache_miss event mismatch");
    }

    std::cout << "Observed epoch binary cache preload " << runtime.loader->event_counters["epoch_cache_preload"] << " times" << std::endl;
    std::cout << "Expected epoch binary cache preload " << cache_preloads << " times" << std::endl;
    if (cache_preloads != -1){
        log_assert(runtime.loader->event_counters["epoch_cache_preload"] == cache_preloads, "epoch_cache_preload event mismatch");
    }

    std::cout << "Observed epoch barriers " << runtime.loader->event_counters.at("epoch_barrier") << " " << std::endl;
    std::cout << "Expected epoch barriers " << epoch_barriers << " times" << std::endl;
    if (epoch_barriers != -1){
        log_assert(runtime.loader->event_counters.at("epoch_barrier") == epoch_barriers, "epoch_barrier event mismatch");
    }

    std::cout << "Observed full-grid-syncs " << runtime.loader->event_counters.at("full_grid_syncs") << " " << std::endl;
    std::cout << "Expected full-grid-syncs " << full_grid_syncs << " times" << std::endl;
    if (full_grid_syncs != -1){
        log_assert(runtime.loader->event_counters.at("full_grid_syncs") == full_grid_syncs, "full_grid_syncs event mismatch");
    }

    std::cout << "Observed epoch-id-alias-hazards " << runtime.loader->event_counters.at("epoch_id_alias_hazards") << " " << std::endl;
    std::cout << "Expected epoch-id-alias-hazards " << epoch_id_alias_hazards << " times" << std::endl;
    if (epoch_id_alias_hazards != -1){
        log_assert(runtime.loader->event_counters.at("epoch_id_alias_hazards") == epoch_id_alias_hazards, "epoch_id_alias_hazards event mismatch");
    }

    return 0;
}

