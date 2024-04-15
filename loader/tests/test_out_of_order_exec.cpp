// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <algorithm>
#include <random>

#include "runtime.hpp"
#include "verif.hpp"

bool check_queue_wrptr(tt_runtime_workload &workload, std::string queue_name, int wrptr_val, tt_cluster *cluster) {
    tt_queue_info &queue_info = workload.queues[queue_name].my_queue_info;
    vector<uint32_t> wrptr;
    for (auto &alloc : queue_info.alloc_info ) {
        tt_target_dram dram = {queue_info.target_device, alloc.channel, 0/*subchan*/};
        cluster->read_dram_vec(wrptr, dram, alloc.address + 4, 4);
        if (wrptr[0] != wrptr_val) {
            log_warning(LogTest, "check_queue_wrptr: expected = {}, observed = {}, check FAILED!", wrptr_val, wrptr[0]);
            return false;
        }
    }
    log_info(LogTest, "Queue global wrptr = {}, \tcheck PASSED!", wrptr_val);
    return true;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool run_silicon;
    int cache_hits;
    std::string netlist_path;
    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_program/out_of_order_epochs.yaml");

    std::string arch_name;
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, 0, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    // Runtime run all programs
    tt_device_params device_params;
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized successfully");

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor);

    std::vector<std::string> program_exec_order = {
        "compile_order_1_exec_order_0",
        "compile_order_1_exec_order_0",
        "compile_order_1_exec_order_0",
        "compile_order_1_exec_order_0",
        "compile_order_0_exec_order_1",
        "compile_order_3_exec_order_2",
        "compile_order_2_exec_order_3",
    };

    // Test 1: Run epochs in an order that's different from compiled epochs
    for (std::string program : program_exec_order) {
        runtime.run_program(program, {});
    }
    log_assert(check_queue_wrptr(workload, "out0", 4, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out1", 4, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out2", 4, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out3", 4, runtime.cluster.get()), "failed");

    // Test 2: Shuffle and run epochs in a random order
    unsigned seed = 0xfeed;
    std::shuffle(std::begin(program_exec_order), std::end(program_exec_order), std::default_random_engine(seed));
    for (std::string program : program_exec_order) {
        runtime.run_program(program, {});
    }
    log_assert(check_queue_wrptr(workload, "out0", 8, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out1", 8, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out2", 8, runtime.cluster.get()), "failed");
    log_assert(check_queue_wrptr(workload, "out3", 8, runtime.cluster.get()), "failed");

    runtime.finish();
    return 0;
}

