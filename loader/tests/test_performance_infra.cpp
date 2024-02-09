// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

const string OP_DELAY_TEST_NETLIST_NAME = "loader/tests/net_basic/netlist_matmul_op_delay_perf_test.yaml";
const tt_xy_pair OP_DELAY_TEST_TARGET_CORE(2, 2);
const uint OP_DELAY_TEST_INPUT_COUNT = 128;

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

unordered_map<tt_xy_pair, vector<uint64_t>> remove_idle_cores(unordered_map<tt_xy_pair, vector<uint64_t>> expected_runtime) {
    unordered_map<tt_xy_pair, vector<uint64_t>> expected_runtime_active_cores;
    for (auto core_it: expected_runtime) {
        for (auto runtime: core_it.second) {
            if (runtime != 0) {
                expected_runtime_active_cores.insert({core_it.first, core_it.second});
                break;
            }
        }
    }
    return expected_runtime_active_cores;
}

unordered_map<tt_xy_pair, vector<uint64_t>> adjust_runtime(unordered_map<tt_xy_pair, vector<uint64_t>> expected_runtime, float overhead) {
    for (auto &core_it: expected_runtime) {
        for (auto &runtime: core_it.second) {
            if (runtime != 0) {
                runtime += uint64_t(overhead * runtime);
            }
        }
    }
    return expected_runtime;
}


enum PerfTestType {
    Delay = 1,
    InfraOverhead = 2
};

PerfTestType get_perf_test_type(string test_type) {
    if (test_type == "delay") {
        return PerfTestType::Delay;
    } else if (test_type == "infra_overhead") {
        return PerfTestType::InfraOverhead;
    } else {
        log_fatal("Invalid perf test type {}", test_type);
        return PerfTestType::Delay; // Just to allow compilation to pass
    }
}

struct TestArgs {
    std::vector<std::string> original_args;
    PerfTestType test_type;
    
    uint32_t overhead_light_percentage;
    uint32_t overhead_verbose_percentage;

    vector<uint> target_test_ids;

};

TestArgs get_test_arguments(std::vector<std::string> original_args) {
    TestArgs test_args;
    string test_type_str;
    string target_test_ids_str;

    std::tie(test_type_str, original_args) = verif_args::get_command_option_and_remaining_args(original_args, "--perf-test-type", "delay");
    std::tie(target_test_ids_str, original_args) = verif_args::get_command_option_and_remaining_args(original_args, "--target-test-ids", "");
    
    test_args.test_type = get_perf_test_type(test_type_str);
    verif_args::split_string_into_vector(test_args.target_test_ids, target_test_ids_str, ",");

    if (test_args.test_type == PerfTestType::InfraOverhead) {
        std::tie(test_args.overhead_light_percentage, original_args) = verif_args::get_command_option_uint32_and_remaining_args(original_args, "--perf-light-overhead-percent", 7);
        std::tie(test_args.overhead_verbose_percentage, original_args) = verif_args::get_command_option_uint32_and_remaining_args(original_args, "--perf-verbose-overhead-percent", 7);
    } else if (test_args.test_type == PerfTestType::Delay) {
        string netlist_path;
        std::tie(netlist_path, original_args) = verif_args::get_command_option_and_remaining_args(original_args, "--netlist", "");
        log_assert(netlist_path == "", "In delay infra performance test, netlist will be hardcoded to {}. --netlist input argument is not allowed", OP_DELAY_TEST_NETLIST_NAME);
    }

    test_args.original_args = original_args;
    return test_args;

}

bool compare_observed_expected(uint64_t observed, uint64_t target, float rtol) {
    if ((observed < target + target * rtol) && (observed > target - target * rtol)) {
        return true;
    } else {
        return false;
    }
}

bool run_infra_overhead_test(const TestArgs &test_args) {
    constexpr uint num_modes = 3;
    constexpr uint num_repeats = 10;
    std::array<unordered_map<tt_xy_pair, vector<uint64_t>>, num_modes> runtime_active_cores;

    float max_perf_overhead_allowed_light = test_args.overhead_light_percentage / 100.0;
    float max_perf_overhead_allowed_verbose = test_args.overhead_verbose_percentage / 100.0;
    // Mode-0: No perf
    // Mode-1: --dump-perf-events --perf-level 0
    // Mode-2: --dump-perf-events-intermediate --perf-level 1
    for (uint mode = 0; mode < num_modes; mode++) {
        log_info(tt::LogTest, "Starting perf-infra performance test for mode {}", mode);
        std::vector<std::string> args = test_args.original_args;
        tt_runtime_config config(args);

        if (mode == 0) {
            log_assert(config.perf_desc.device_perf_mode == perf::PerfDumpMode::Disable, "In perf-infra testing, the input cmdline args must not have enabled perf");
        } else if (mode == 1) {
            config.perf_desc.device_perf_mode = perf::PerfDumpMode::SingleDumpPerEpoch;
            config.perf_desc.perf_dump_level = 0;
            // config.perf_desc.perf_infra_check = adjust_runtime(expected_runtime_active_cores, max_perf_overhead_allowed_light);
            config.perf_desc.target_inputs = {"0","1","2","3","4","5","6","7","-1","-2","-3","-4","-5"};
        } else if (mode == 2) {
            config.perf_desc.device_perf_mode = perf::PerfDumpMode::IntermediateDump;
            config.perf_desc.perf_dump_level = 2;
            // config.perf_desc.perf_infra_check = adjust_runtime(expected_runtime_active_cores, max_perf_overhead_allowed_verbose);
            config.perf_desc.target_inputs = {"0","1","2","3","4","5","6","7","-1","-2","-3","-4","-5"};;
        }
        config.perf_desc.dump_debug_mailbox = true;
        
        verif_args::validate_remaining_args(args);

        for (int repeat = 0; repeat < num_repeats; repeat++) {
            log_info(tt::LogTest, "Running perf-infra performance check test in mode {} for {}/{}", mode, repeat, num_repeats-1);
            // config.device_params = {.vcd_dump_cores = {"1-1"}};
            tt_runtime runtime(config.netlist_path, config);

            // Start device and statically assemble binaries
            log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

            bool force_sw_tilize = true;

            vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
            tt::io::push_host_inputs(input_io_desc, &get_tilized_tensor, -1, force_sw_tilize);

            // Interactively run programs
            for (auto const& program : runtime.get_workload()->programs) {
                log_assert(runtime.run_program(program.first, {}) == tt::DEVICE_STATUS_CODE::Success, "run_program failed");
            }

            // Shutdown device
            log_assert(runtime.finish() == tt::DEVICE_STATUS_CODE::Success, "finish failed");

            unordered_map<tt_xy_pair, vector<uint64_t>> current_mode_kernel_runtime = runtime.get_last_epoch_kernel_runtime();
            unordered_map<tt_xy_pair, vector<uint64_t>> current_mode_kernel_runtime_trimmed = remove_idle_cores(current_mode_kernel_runtime);
            if (repeat == 0) {
                runtime_active_cores.at(mode) = current_mode_kernel_runtime_trimmed;
                log_assert(runtime_active_cores.size() != 0, "There must be at least one active runtime recorded in debug mailbox.");
            } else {
                for (auto &core_to_runtime: current_mode_kernel_runtime_trimmed) {
                    // cout << "KD-core " << core_to_runtime.first.str() << endl;
                    log_assert(runtime_active_cores.at(mode).find(core_to_runtime.first) != runtime_active_cores.at(mode).end(), "");
                    for (int thread = 0; thread < core_to_runtime.second.size(); thread++) {
                        // cout << "KD- " << core_to_runtime.second.at(thread) << "--" << runtime_active_cores.at(mode).at(core_to_runtime.first).at(thread) << endl;
                        runtime_active_cores.at(mode).at(core_to_runtime.first).at(thread) += core_to_runtime.second.at(thread);
                    }
                }
            }
        }
        for (auto &core_to_runtime: runtime_active_cores.at(mode)) {
            // cout << "final values = " << core_to_runtime.first.str() << endl;
            for (int thread = 0; thread < core_to_runtime.second.size(); thread++) {
                // cout << "KD- " << core_to_runtime.second.at(thread) << endl;
                core_to_runtime.second.at(thread) /= num_repeats;
                // cout << "KD-2 " << core_to_runtime.second.at(thread) << endl;
            }
        }
    }

    log_info(tt::LogTest, "Checking light-perf runtime overhead");
    unordered_map<tt_xy_pair, vector<uint64_t>> adjusted_expected_runtime_light = adjust_runtime(runtime_active_cores.at(0), 0);
    bool all_passed = true;
    for (auto core_to_runtime: adjusted_expected_runtime_light) {
        log_info(tt::LogTest, "Checking light-perf runtime overhead for core {}", core_to_runtime.first.str());
        log_assert(runtime_active_cores.at(1).find(core_to_runtime.first) != runtime_active_cores.at(1).end(), "");
        for (int thread = 0; thread < core_to_runtime.second.size(); thread++) {
            uint64_t expected_kernel_runtime = core_to_runtime.second.at(thread);
            uint64_t observed_kernel_runtime = runtime_active_cores.at(1).at(core_to_runtime.first).at(thread);
            if (expected_kernel_runtime != 0) {
                // if (abs(float(observed_kernel_runtime) - expected_kernel_runtime) > abs(max_perf_overhead_allowed_light * expected_kernel_runtime)) {
                if (float(observed_kernel_runtime) - expected_kernel_runtime > max_perf_overhead_allowed_light * expected_kernel_runtime) {
                    all_passed = false;
                    log_error("Performance check of perf-infra failed for core {} thread {}. Observed runtime {} and expected {}.", core_to_runtime.first.str(), thread, observed_kernel_runtime, expected_kernel_runtime);
                } else {
                    log_info(tt::LogTest, "Performance check of perf-infra passed for core {} thread {}. Observed runtime {} and expected {}.", core_to_runtime.first.str(), thread, observed_kernel_runtime, expected_kernel_runtime);
                }
            }
        }
    }
    unordered_map<tt_xy_pair, vector<uint64_t>> adjusted_expected_runtime_verbose = adjust_runtime(runtime_active_cores.at(0), 0);
    for (auto core_to_runtime: adjusted_expected_runtime_verbose) {
        log_info(tt::LogTest, "Checking verbose-perf runtime overhead for core {}", core_to_runtime.first.str());
        log_assert(runtime_active_cores.at(2).find(core_to_runtime.first) != runtime_active_cores.at(2).end(), "");
        for (int thread = 0; thread < core_to_runtime.second.size(); thread++) {
            uint64_t expected_kernel_runtime = core_to_runtime.second.at(thread);
            uint64_t observed_kernel_runtime = runtime_active_cores.at(2).at(core_to_runtime.first).at(thread);
            if (expected_kernel_runtime != 0) {
                // if (abs(float(observed_kernel_runtime) - expected_kernel_runtime) > abs(max_perf_overhead_allowed_verbose * expected_kernel_runtime)) {
                if (float(observed_kernel_runtime) - expected_kernel_runtime > max_perf_overhead_allowed_verbose * expected_kernel_runtime) {
                    all_passed = false;
                    log_error("Performance check of perf-infra failed for core {} thread {}. Observed runtime {} and expected {}.", core_to_runtime.first.str(), thread, observed_kernel_runtime, expected_kernel_runtime);
                } else {
                    log_info(tt::LogTest, "Performance check of perf-infra passed for core {} thread {}. Observed runtime {} and expected {}.", core_to_runtime.first.str(), thread, observed_kernel_runtime, expected_kernel_runtime);
                }
            }
        }
    }
    return all_passed;
}

bool run_kernel_delay_test(const TestArgs &test_args) {
    log_info(tt::LogTest, "Starting op-delay infra performance test");

    bool all_passed = true;
    const uint num_tests = 9;
    const float rtol = 0.1;
    // Each test has the following settings:
    // 0. Original test
    // 1. Run_only and delay for target_op unpacker
    // 2. Run_only and delay for target_op math
    // 3. Run_only and delay for target_op pack
    // 4. Original test with perf mode
    // 5. Run_only and delay for target_op unpacker
    // 6. Run_only and delay for target_op math
    // 7. Run_only and delay for target_op pack
    // 8. Run_only and reset kernel delay for target_op
    
    std::unordered_map<uint, std::array<uint64_t, 3> > each_test_runtime_all_threads;
    std::unordered_map<uint, std::array<uint64_t, 3> > each_test_delay_cycles;

    for (uint test_id = 0; test_id < num_tests; test_id++) {
        
        if (test_args.target_test_ids.size() > 0 && std::find(test_args.target_test_ids.begin(), test_args.target_test_ids.end(), test_id) == test_args.target_test_ids.end()) {
            log_warning(tt::LogTest, "Skipping test id {} since it was not included in the target_test_ids list.", test_id);
            continue;
        }
        
        std::vector<std::string> args = test_args.original_args;
        args.push_back("--netlist");
        args.push_back(OP_DELAY_TEST_NETLIST_NAME);
        args.push_back("--dump-debug-mailbox");

        if (test_id == 0) {
            each_test_delay_cycles.insert({test_id, {0, 0, 0}});
        } else if (test_id == 1) {
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:500000-0-0");
            each_test_delay_cycles.insert({test_id, {500000, 0, 0}});
        } else if (test_id == 2) {
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:0-1500000-0");
            each_test_delay_cycles.insert({test_id, {0, 1500000, 0}});
        } else if (test_id == 3) {
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:0-0-1000000");
            each_test_delay_cycles.insert({test_id, {0, 0, 1000000}});
        } else if (test_id == 4) {
            args.push_back("--dump-perf-events");
            each_test_delay_cycles.insert({test_id, {0, 0, 0}});
        } else if (test_id == 5) {
            args.push_back("--dump-perf-events");
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:500000-0-0");
            each_test_delay_cycles.insert({test_id, {500000, 0, 0}});
        } else if (test_id == 6) {
            args.push_back("--dump-perf-events");
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:0-1500000-0");
            each_test_delay_cycles.insert({test_id, {0, 1500000, 0}});
        } else if (test_id == 7) {
            args.push_back("--dump-perf-events");
            args.push_back("--run-only");
            args.push_back("--insert-kernel-delay");
            args.push_back("target_op:0-0-1000000");
            each_test_delay_cycles.insert({test_id, {0, 0, 1000000}});
        } else if (test_id == 8) {
            args.push_back("--dump-perf-events");
            args.push_back("--run-only");
            args.push_back("--reset-kernel-delay");
            args.push_back("target_op");
            each_test_delay_cycles.insert({test_id, {0, 0, 0}});
        }
        log_info(tt::LogTest, "Running perf-infra op_delay test id {} with final args {}", test_id, args);
        tt_runtime_config config(args);
        config.perf_desc.dump_debug_mailbox = true;
        verif_args::validate_remaining_args(args);

        /////////////////////////////////////////////////////////////////
        // Run test
        /////////////////////////////////////////////////////////////////
        tt_runtime runtime(config.netlist_path, config);
        // Start device and statically assemble binaries
        log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");
        bool force_sw_tilize = true;
        vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &get_tilized_tensor, -1, force_sw_tilize);
        // Interactively run programs
        for (auto const& program : runtime.get_workload()->programs) {
            log_assert(runtime.run_program(program.first, {}) == tt::DEVICE_STATUS_CODE::Success, "run_program failed");
        }
        // Shutdown device
        log_assert(runtime.finish() == tt::DEVICE_STATUS_CODE::Success, "finish failed");

        /////////////////////////////////////////////////////////////////
        // Collect results
        /////////////////////////////////////////////////////////////////
        unordered_map<tt_xy_pair, vector<uint64_t>> current_mode_kernel_runtime = runtime.get_last_epoch_kernel_runtime();
        unordered_map<tt_xy_pair, vector<uint64_t>> current_mode_kernel_runtime_trimmed = remove_idle_cores(current_mode_kernel_runtime);

        log_assert(current_mode_kernel_runtime_trimmed.size() == 4, "This test must only have four active worker core");
        log_assert(current_mode_kernel_runtime_trimmed.find(OP_DELAY_TEST_TARGET_CORE) != current_mode_kernel_runtime_trimmed.end(), "Core coord {} must exist in the op delay perf test.", OP_DELAY_TEST_TARGET_CORE.str());

        vector<uint64_t> all_thread_runtime = current_mode_kernel_runtime_trimmed.at(OP_DELAY_TEST_TARGET_CORE);
        std::array<uint64_t, 3> runtime_trimmed = {all_thread_runtime.at(0), all_thread_runtime.at(1), all_thread_runtime.at(2)};
        log_info(tt::LogTest, "Runtime for core {}, thread-0: {}, thread-1: {}, thread-2: {}", OP_DELAY_TEST_TARGET_CORE.str(), runtime_trimmed.at(0), runtime_trimmed.at(1), runtime_trimmed.at(2));

        each_test_runtime_all_threads.insert({test_id, runtime_trimmed});
    }

    /////////////////////////////////////////////////////////////////
    // Check results for every mode
    /////////////////////////////////////////////////////////////////
    std::array<uint64_t, 3> runtime_original_test = each_test_runtime_all_threads.at(0);
    std::array<uint64_t, 3> runtime_original_test_perf_dump = each_test_runtime_all_threads.at(4);
    
    for (uint test_id = 0; test_id < num_tests; test_id++) {
        std::array<uint64_t, 3> current_test_runtime = each_test_runtime_all_threads.at(test_id);
        std::array<uint64_t, 3> target_test_runtime;
        if (test_id < 4) {
            target_test_runtime = runtime_original_test;
        } else {
            target_test_runtime = runtime_original_test_perf_dump;
        }
        bool current_test_passed = true;
        for (uint thread_id = 0; thread_id < 3; thread_id++) {
            const uint amount_of_delay = OP_DELAY_TEST_INPUT_COUNT * each_test_delay_cycles.at(test_id).at(thread_id);
            if (amount_of_delay == 0) {
                log_warning(tt::LogTest, "Skipping check for test_id {} thread_id {} since delay inserted is 0", test_id, thread_id);
                continue;
            }
            if (compare_observed_expected(current_test_runtime.at(thread_id), target_test_runtime.at(thread_id) + amount_of_delay, rtol)) {
                log_info(tt::LogTest, "Test id {} passed on thread {} runtime comaprison with observed {} original {} with delay {} total {} rtol {}", test_id, thread_id, current_test_runtime.at(thread_id), target_test_runtime.at(thread_id), amount_of_delay, target_test_runtime.at(thread_id) + amount_of_delay, rtol);
            } else {
                current_test_passed = false;
                log_error("Test id {} failed on thread {} runtime comaprison with observed {} original {} with delay {} total {} rtol {}", test_id, thread_id, current_test_runtime.at(thread_id), target_test_runtime.at(thread_id), amount_of_delay, target_test_runtime.at(thread_id) + amount_of_delay, rtol);
            }
        }
        all_passed = all_passed && current_test_passed;
    }
    return all_passed;
}

int main(int argc, char** argv)
{
    std::vector<std::string> original_args(argv, argv + argc);

    bool all_passed = true;
    TestArgs test_args = get_test_arguments(original_args);
    
    if (test_args.test_type == PerfTestType::InfraOverhead) {
        all_passed = run_infra_overhead_test(test_args);
    } else if (test_args.test_type == PerfTestType::Delay) {
        all_passed = run_kernel_delay_test(test_args);
    } else {
        log_fatal("Invalid test_type");
    }

    if (all_passed) {
        log_info(tt::LogTest, "Finished performance check for perf-infra successfully.");
    } else {
        log_fatal("Performance check of perf-infra failed for one or more threads. check the errors in test log for more detail");
    }
    return 0;
}

