// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "runtime.hpp"
#include "verif.hpp"

bool check_queue_gptr(tt_runtime_workload &workload, tt_cluster *cluster, std::string queue_name, int gptr_val, int lptr_val = -1) {
    tt_queue_info &queue_info = workload.queues[queue_name].my_queue_info;
    vector<uint32_t> rd_vec;
    bool pass = true;

    for (auto &alloc : queue_info.alloc_info ) {
        tt_target_dram dram = {queue_info.target_device, alloc.channel, 0/*subchan*/};
        cluster->read_dram_vec(rd_vec, dram, alloc.address, 16);
        std::stringstream addr_str;
        addr_str << "0x" << std::hex << alloc.address;

        tt_queue_header *header = reinterpret_cast<tt_queue_header*>(&rd_vec[0]);

        if (header->global_rd_ptr != gptr_val) {
            log_error("check_queue_gptr dram@[{},{}]: expected = {}, observed = {}, check FAILED!", alloc.channel, addr_str.str(), gptr_val, header->global_rd_ptr);
            pass = false;
        }

        if (lptr_val == -1) continue; // no local ptr check

        if (header->local_rd_ptr != lptr_val) {
            log_error("check_queue_lptr dram@[{},{}]: expected = {}, observed = {}, check FAILED!", alloc.channel, addr_str.str(), lptr_val, header->local_rd_ptr);
            pass = false;
        }
    }
    log_info(LogTest, "Queue global rdptr = {}, local rdptr = {}, check {}!", gptr_val, lptr_val, pass ? "PASSED" : "FAILED");
    return pass;
}

bool check_union_and_set_totals(tt_runtime_workload &workload, std::string prog_name) {
    std::set<std::string> union_set;
    int queue_set_total_size = 0;
    int union_set_total_size = 0;
    for (auto &mapping : workload.get_qptrs_wrap(prog_name).header_field_vars_to_queue_map[tt_queue_header_field::GlobalRdptr]) {
        auto queue_set = mapping.second;
        std::set_union(
            union_set.begin(), union_set.end(),
            queue_set.begin(), queue_set.end(),
            inserter(union_set, union_set.end()));
        queue_set_total_size += queue_set.size();
        union_set_total_size = union_set.size();
        log_info(LogTest, "Mapped set {}", mapping);
    }
    log_info(LogTest, "Union set {}", std::make_pair("U(all)", union_set));
    if (union_set_total_size != queue_set_total_size) {
        log_warning(LogTest, "check_union_and_set_totals: union = {}, sum = {}, check FAILED!", union_set_total_size, queue_set_total_size);
        return false;
    }
    log_info(LogTest, "check_union_and_set_totals \tcheck PASSED!");
    return true;
}

void run_program_with_checks(const string name, tt_runtime &runtime) {
    tt_runtime_workload &workload = *runtime.get_workload();
    runtime.run_program(name, {});
    check_union_and_set_totals(workload, name);
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool run_silicon;
    int opt_level;
    int cache_hits;
    std::string netlist_path;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 0);
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "default_netlist.yaml");

    std::string arch_name;
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    tt_runtime runtime(netlist_path, config);

    // Runtime run all programs
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    bool force_sw_tilize = true;

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor, -1, force_sw_tilize);
    tt_runtime_workload &workload = *runtime.get_workload();

    if (netlist_path == "loader/tests/net_program/gptr_updates.yaml") {
        // Test case 1
        // basic increment by 1, loop 8 times
        run_program_with_checks("incr_by_1_8_times", runtime);
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in0", 8), "failed");

        // Test case 2
        // reuse same queue used in prev program, test it's reset properly
        // increment by 2, test it's final increment is observed on device
        run_program_with_checks("incr_by_2_6_times", runtime);
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in0", 12), "failed");

        // Test case 3
        // reuse same queue used in prev program, test it's reset properly
        // initial offset by 4, run program, increment to 6 via repeated varinst
        run_program_with_checks("offset_by_4_incr_by_6", runtime);
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in0", 10), "failed");
    }
    else if (netlist_path == "loader/tests/net_program/gptr_multiple_side_effects.yaml") {
        // Test case 1
        // multiple ptr advance at the same time, test side effect pushes are not mixed up
        // each ptr may alias to multiple queues, which requires batched update for each gptr update
        run_program_with_checks("multi_queue_fast_slow", runtime);
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in0", 0), "failed");
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in1", 8), "failed");
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in2", 0), "failed");

        // Test case 2
        // same queue aliased to different pointers during execution
        // pointer values copied to pointers that have different aliasing
        run_program_with_checks("multi_queue_all", runtime);
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in0", 4), "failed");
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in1", 8), "failed");
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in2", 4), "failed");
    }
    else if (netlist_path == "loader/tests/net_basic/netlist_unary_lptr_gptr.yaml") {
        // Test case 1
        // do not increment the ptrs in the program
        // - check that device has not modified gptr

        run_program_with_checks("ptr_skip_incr", runtime);
        runtime.wait_for_idle();
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in", 0), "failed");

        // Test case 2
        // initialize ptrs to be non-zero, check that device correctly retained/incremented from those values
        // - check that device has not modified gptr

        run_program_with_checks("ptr_init_non_zero", runtime);
        runtime.wait_for_idle();
        log_assert(check_queue_gptr(workload, runtime.cluster.get(), "in", 2), "failed");
    }
    runtime.finish();
    return 0;
}

