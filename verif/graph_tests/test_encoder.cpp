// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <thread>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_golden.hpp"
#include "verif.hpp"

using namespace tt;
using namespace tt::golden;
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

// Meant for communication between the only thread spawned and the main thread which checks this.
std::atomic<bool> done(false);
std::atomic<bool> pass(true);

struct test_args {
    std::string netlist_path = "";
    std::string output_dir = "";
    int num_program_loops = 1;
    int num_loops = 1;
    bool help = false;
    bool run_silicon = false;
    bool force_sw_tilize = false;
    string stimulus_config_yaml = "";
    std::string arch_name = "";
    std::string vcd_dump_cores = "";
    int seed = -1;
    int timeout = -1;
    bool init_rams = false;
    perf::PerfDesc perf_desc;
};

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "test_op --netlist [netlist_path] --num-loops [1]\n";
    help_string += "--num-loops <>       : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>            : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>         : Path to netlist file\n";
    help_string += "--silicon            : Run on Silicon (Default: Versim)\n";
    help_string += "--force-sw-tilize    : Force SW tilize for tt_runtime (default will use HW-Tilize when possible)\n";
    help_string +=
        "--arch <>            : specify the device architecture. Options: \"grayskull\", \"wormhole\", \"wormhole_b0\" "
        "(Default: grayskull)\n";
    help_string += "--vcd-dump-cores <>  : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\" ";
    help_string += "--timeout <>         : Timeout in Seconds (Default: -1 (no timeout))\n";
    help_string += "--help               : Prints this message\n";
    help_string += "--init-rams          : Host inits all rams (default false)\n";
    help_string += "--num-program-loops  : Number of loops in program (per run_program call)\n";
    try {
        std::tie(args.output_dir, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outdir", "");
        if (args.output_dir == "") {
            args.output_dir = verif_filesystem::get_output_dir(__FILE__, input_args);
        }
        std::tie(args.num_program_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-program-loops", 1);
        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.timeout, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--timeout", -1);
        std::tie(args.netlist_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--netlist", "verif/op_tests/netlist_unary_op.yaml");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.run_silicon, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--silicon");
        std::tie(args.force_sw_tilize, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--force-sw-tilize");
        std::tie(args.arch_name, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
        std::tie(args.init_rams, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--init-rams");
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        args.perf_desc = perf::PerfDesc(input_args, args.netlist_path);
        verif_args::validate_remaining_args(input_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (args.help) {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(0);
    }
    return args;
}

void test_main(test_args args) {
    // Setup
    if (args.seed == -1) {
        tt_rnd_set_seed(tt_gen_seed());
    } else {
        tt_rnd_set_seed(args.seed);
    }
    // Golden Pathway
    tt_golden_config golden_config ({.en_quantize_golden = true});
    tt_golden golden(args.netlist_path, golden_config);
    std::map<string, tt_queue_info> input_queue_info_map = golden.get_host_queue_info_map(args.init_rams); 
    std::map<string, tt_queue_info> input_ram_info_map = golden.get_host_ram_info_map(); 
    std::uint32_t num_queues = input_queue_info_map.size();
    std::uint32_t num_rams = input_ram_info_map.size();
    std::uint32_t num_program_loops = args.num_program_loops;
    std::uint32_t num_loops = args.num_loops;

    std::map <std::string, std::string> program_parameters = {{"$p_microbatch_count", to_string(num_program_loops)}};

    std::map<string, std::vector<tt_tensor>> input_queues;
    golden.reset_queues();
    VerifTestConfig test_config = verif::test_config::read_from_yaml_file(args.netlist_path);
    
    // Setup
    // Compile static binaries
    const tt::ARCH arch = arch_name_from_string(args.arch_name);

    tt_runtime_config config = get_runtime_config(args.perf_desc, args.output_dir, 0, args.run_silicon);
    // Runtime setup
    config.device_params = tt_device_params{};
    if (args.vcd_dump_cores != "") {
        config.device_params.vcd_dump_cores = verif_args::get_vcd_dump_cores_from_arg_string(args.vcd_dump_cores);
    }
    tt_runtime runtime(args.netlist_path, config);
    // Runtime init
    runtime.initialize();

    tt_runtime_workload& workload = *runtime.get_workload();
    std::string program_name = workload.programs.begin()->first;
    log_assert(workload.programs.size() >= 1, "Expected a single program");

    // TODO: parameterize theses
    std::vector<std::string> input_qs = {
        "input_0_add_mha_0",
        "input_0_mask_copy_0",
    };
    vector<string> output_qs = {
        // for 4x encoder output
        "encoder3.output_norm_ff_3_bias",
        "encoder3.output_mask_copy_3",
        // for 2x encoder output
        "encoder1.output_norm_ff_1_bias",
        "encoder1.output_mask_copy_1",
    };
    const int seq_len = 128;

    // Host input push
    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc(); // Which Queues to push
    int input_count = 0;
    for (int loop = 0; loop < num_loops; loop++) {
        // Process input queues -- Process a microbatch worth of inputs for all queues in graph.
        for (const auto& it : input_queue_info_map) {
            string q_name = it.first;
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            input_queues.insert({q_name, std::vector<tt_tensor>(q_info.input_count * num_program_loops)});
            md.is_tilized = true;
            bool is_input_q = std::find(input_qs.begin(), input_qs.end(), q_name) != input_qs.end();

            input_count = is_input_q ? q_info.input_count * num_program_loops : 1;
            for (int b = 0; b < input_count; ++b) {
                bool is_first_input = (b == 0) && (loop == 0);
                if (is_input_q) {
                    input_queues.at(q_name).at(b) = tt_tensor(md);
                    generate_tensor(
                        verif::stimulus::get_config(q_name, test_config.stimulus_configs),
                        input_queues.at(q_name).at(b));
                    // set mask tokens to 1
                    // if (q_name.find("mask") != std::string::npos) {
                    //     input_queues.at(q_name).at(b) = 1.0f;
                    // }
                } else {
                    if (is_first_input) {
                        input_queues.at(q_name).at(b) = tt_tensor(md);
                        if (q_name.find("eps") != std::string::npos) {
                            verif::random::set_number(input_queues.at(q_name).at(b), std::numeric_limits<float>::epsilon());
                        } else if (q_name.find("mean") != std::string::npos) {
                            verif::random::set_number(input_queues.at(q_name).at(b), 1.0f/seq_len);
                        } else if (q_name.find("var") != std::string::npos) {
                            verif::random::set_number(input_queues.at(q_name).at(b), 1.0f/seq_len);
                        } else if (q_name.find("sum") != std::string::npos) {
                            verif::random::set_number(input_queues.at(q_name).at(b), 1.0f);
                        } else {
                            generate_tensor(
                                verif::stimulus::get_config(q_name, test_config.stimulus_configs),
                                input_queues.at(q_name).at(b));
                        }
                    }
                }
                if (q_info.type == IO_TYPE::Queue) {
                    if (is_input_q) {
                        golden.push_input(q_info, std::make_shared<tt_tensor>(input_queues.at(q_name).at(b)));
                    } else {
                        if (is_first_input) {
                            golden.push_input(q_info, std::make_shared<tt_tensor>(input_queues.at(q_name).at(b)));
                        }
                    }
                } else {
                    if (is_first_input) {
                        golden.push_input(q_info, std::make_shared<tt_tensor>(input_queues.at(q_name).at(b)), 0);
                    }
                }
            }
        }

        // Run program -- Need to make sure program is only 1 microbatch programed as looping is handled by test.
        for (std::string program_name : workload.program_order) {
            golden.run_program(program_name, program_parameters);
        }

        if (args.init_rams) {
            for (const auto& it : input_ram_info_map) {
                string q_name = it.first;
                tt_queue_info q_info = it.second;
                tt_dram_io_desc io_desc = runtime.get_queue_descriptor(q_name);
                tt::io::translate_addresses(io_desc);
                input_io_desc.push_back(io_desc);
            }
        }

        auto get_golden_input = [&input_queues](tt_queue_info& queue_info, int entry_idx) {
            log_assert(
                entry_idx < input_queues.at(queue_info.name).size(),
                "Entry accessed by runtime is more than amount of inputs specified in test -- please increase inputs to test or decrease entries in queue");  // FIXME Fatal on access issues
            log_assert(
                input_queues.find(queue_info.name) != input_queues.end(),
                "Cannot find queue in inputs created by golden");
            return input_queues.at(queue_info.name).at(entry_idx);
        };

        log_info(tt::LogTest, "Pushing all inputs...");
        auto push_start = std::chrono::high_resolution_clock::now();

        for (tt_dram_io_desc &io : input_io_desc) {
            bool is_input_q = std::find(input_qs.begin(), input_qs.end(), io.queue_name) != input_qs.end();
            input_count = is_input_q ? io.input_count * num_program_loops : 1;
            int ram_ptr = -1;
            // Param always initializes single entry
            if (io.io_type == IO_TYPE::RandomAccess) {
                input_count = 1;
                ram_ptr = 0;
            }
            if (!is_input_q and loop>0) continue;
            tt::io::push_host_input(io, get_golden_input, input_count, ram_ptr, args.force_sw_tilize);
        }

        auto push_end = std::chrono::high_resolution_clock::now();
        auto push_duration = std::chrono::duration_cast<std::chrono::microseconds>(push_end - push_start).count();
        log_info(tt::LogTest, "Push Elapsed Time: {} us", push_duration);

        for (std::string program_name : workload.program_order) {
            runtime.run_program(program_name, program_parameters);
            runtime.wait_for_idle();  // explicitly wfi before popping from device
        }

        // Collect outputs via lazy method that returns all output queues
        log_info(tt::LogTest, "Runtime complete, collecting outputs from device");
        vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();

        log_info(tt::LogTest, "Popping all outputs...");
        auto pop_start = std::chrono::high_resolution_clock::now();
        tt::io::pop_outputs_to_tensor_queues(output_io_desc, 0 /*timeout_in_seconds*/);

        auto pop_end = std::chrono::high_resolution_clock::now();
        auto pop_duration = std::chrono::duration_cast<std::chrono::microseconds>(pop_end - pop_start).count();
        log_info(tt::LogTest, "Pop Elapsed Time: {} us", pop_duration);

        std::map<string, tt_queue_info> output_queue_info_map = {};

        vector<string> output_queues = output_qs;
        vector<string> output_rams = {};

        for (auto const& output_q_name : output_rams) {
            if (workload.queues.find(output_q_name) != workload.queues.end()) {
                output_queue_info_map.insert(
                    {workload.queues[output_q_name].my_queue_info.name, workload.queues[output_q_name].my_queue_info});
            }
        }

        for (auto const& output_q_name : output_queues) {
            if (workload.queues.find(output_q_name) != workload.queues.end()) {
                output_queue_info_map.insert(
                    {workload.queues[output_q_name].my_queue_info.name, workload.queues[output_q_name].my_queue_info});
            }
        }

        // Checking for q_info.input_count
        VerifComparisonConfig comparison_config = verif::comparison::read_from_yaml_file(args.netlist_path);
        // for (const auto& it : golden.get_device_queue_info_map()) {
        for (const auto& it : output_queue_info_map) {
            string q_name = it.first;
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            for (int b = 0; b < q_info.input_count * num_program_loops; ++b) {
                tt_tensor golden_output;
                log_info(tt::LogTest, "Loop {} popping {} from golden tensor queue", loop, q_name);
                if (q_info.type == IO_TYPE::Queue) {
                    golden_output = *golden.pop_output(q_info);
                } else {
                    if (b == 0) {
                        golden_output = *golden.get_output(q_info, 0);
                    } else {
                        continue;
                    }
                }

                log_info(tt::LogTest, "Loop {} popping {} from device tensor queue", loop, q_name);
                std::shared_ptr<tt_tensor> observed_output = workload.queues[q_name].my_io->get();
                workload.queues[q_name].my_io->pop();
                log_info(tt::LogTest, "Checking Entry idx={}", b);
                if (not compare_tensors(
                        golden_output,
                        *observed_output,
                        verif::comparison::get_config(q_name, test_config.comparison_configs))) {
                    pass = false;
                    log_error("Entry idx={} Mismatched", b);
                }
            }
        }
    }
    runtime.finish();
    done = true;
}

int main(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);
    log_info(tt::LogTest, "test_encoder");
    test_args args = parse_test_args(input_args);

    // test_main(args);

    // Execute Job
    std::thread test(test_main, args);

    if (args.timeout == -1) {
        args.timeout = INT_MAX;
    }
    double elapsed_seconds = 0;
    while (!done and (elapsed_seconds < args.timeout)) {
        std::this_thread::sleep_for(100ms);
        elapsed_seconds += 0.1;
    }
    log_info(tt::LogTest, "Elapsed Time: {}s", elapsed_seconds);
    if (done) {
        test.join();
    } else {
        log_error("TIMEOUT ERROR");
        std::terminate();
        exit(1);
    }
    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
}
