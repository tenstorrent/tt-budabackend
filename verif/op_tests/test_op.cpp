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
#include <cstdlib>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tensor.hpp"
#include "tt_backend_api_types.hpp"
#include "tt_golden.hpp"
#include "verif.hpp"
#include "verif_utils.hpp"

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
    std::string cluster_desc_path = "";
    std::string pytorch_bin_path = "";
    std::string bin_path = "";
    std::string output_dir = "";
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
    bool skip_golden = false;
    bool determinism_tests = false;
    bool reload_bin = false;
    bool dump_bin = false;
    bool repeat_tensor_in_microbatch = false;
    bool skip_check = false;
    bool stochastic_rnd_test = false;
    perf::PerfDesc perf_desc;
};

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "test_op --netlist [netlist_path] --num-loops [1]\n";
    help_string += "--num-loops <>       : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>            : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>         : Path to netlist file\n";
    help_string += "--cluster-desc              : File path to multichip cluster configuration yaml (Default: "")\n";
    help_string += "--pytorch-bin <>     : Path to the pytorch binary files to corresponding to netlist\n";
    help_string += "--bin-path <>        : Path where to store/load binary files\n";
    help_string += "--silicon            : Run on Silicon (Default: Versim)\n";
    help_string += "--force-sw-tilize    : Force SW tilize for tt_runtime (default will use HW-Tilize when possible)\n";
    help_string +=
        "--arch <>            : specify the device architecture. Options: \"grayskull\", \"wormhole\", \"wormhole_b0\" "
        "(Default: grayskull)\n";
    help_string += "--vcd-dump-cores <>  : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\"\n";
    help_string += "--timeout <>         : Timeout in Seconds (Default: -1 (no timeout))\n";
    help_string += "--determinism-tests         : Check if the target backend generates consistent outputs on the same input and seed across num-loops runs (Default: false)\n";
    help_string += "--help               : Prints this message\n";
    help_string += "--init-rams          : Host inits all rams (default false)\n";
    help_string += "--skip-golden        : Load expected data from pytorch binary\n";
    help_string += "--reload-bin         : Reload same input binary when --pytorch-bin or --bin-path args are set. Doesn't increment entry index in binary name\n";
    help_string += "--repeat-tensor-in-microbatch : Use the same tensor across all inputs in a microbatch (useful for determinism tests)\n";
    help_string += "--stochastic-rnd-test : Check outputs for every loop is random (but still passes PCC), tests stochastic rounding mode\n";
    try {
        std::tie(args.output_dir, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outdir", "");
        if (args.output_dir == "") {
            args.output_dir = verif_filesystem::get_output_dir(__FILE__, input_args);
        }
        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_uint32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.timeout, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--timeout", -1);
        std::tie(args.netlist_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--netlist", "verif/op_tests/netlist_unary_op.yaml");
        std::tie(args.cluster_desc_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--cluster-desc", "");
        std::tie(args.pytorch_bin_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--pytorch-bin", "");
        std::tie(args.bin_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--bin-path", "");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.run_silicon, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--silicon");
        std::tie(args.force_sw_tilize, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--force-sw-tilize");
        std::tie(args.arch_name, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--arch", std::getenv("ARCH_NAME"));
        std::tie(args.init_rams, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--init-rams");
        std::tie(args.skip_golden, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--skip-golden");
        std::tie(args.reload_bin, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--reload-bin");
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        std::tie(args.determinism_tests, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--determinism-tests");
        std::tie(args.repeat_tensor_in_microbatch, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--repeat-tensor-in-microbatch");
        std::tie(args.skip_check, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--skip-check");
        std::tie(args.stochastic_rnd_test, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--stochastic-rnd-test");
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
    std::uint32_t num_loops = 1;
    std::uint32_t num_program_loops = 1;

    if (args.num_loops==0) num_loops=tt_rnd_int(1,5);
    else num_loops = args.num_loops;

    log_info(tt::LogTest, "Running {} loops...", num_loops);

    // Setup runtime
    // Compile static binaries
    const tt::ARCH arch = arch_name_from_string(args.arch_name);

    tt_runtime_config config = get_runtime_config(args.perf_desc, args.output_dir, 0, args.run_silicon);
    if (args.cluster_desc_path.size() > 0) {
        config.cluster_descriptor_path = args.cluster_desc_path;
    }
    // Runtime setup
    config.device_params = tt_device_params{};
    if (args.vcd_dump_cores != "") {
        config.device_params.vcd_dump_cores = verif_args::get_vcd_dump_cores_from_arg_string(args.vcd_dump_cores);
    }
    tt_runtime runtime(args.netlist_path, config);
    // Runtime init
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

    tt_runtime_workload& workload = *runtime.get_workload();
    std::string program_name = workload.programs.begin()->first;
    log_assert(workload.programs.size() >= 1, "must have a program in netlist");

    std::map<string, std::vector<tt_tensor>> input_queues;
    std::map<string, std::vector<tt_tensor>> srnd_input_queues;

    if(!args.determinism_tests)
        // Only perform operations on golden if not running determinism tests
        golden.reset_queues();
    VerifTestConfig test_config = verif::test_config::read_from_yaml_file(args.netlist_path);

    log_assert(!(args.determinism_tests && args.stochastic_rnd_test), "Cannot have determinism tests and stochastic rnd test mode at same time");

    if(args.determinism_tests)
        test_config = verif::test_config::read_from_yaml_file(args.netlist_path, "verif/netlist_tests/default_test_config.yaml");
    else
        test_config = verif::test_config::read_from_yaml_file(args.netlist_path);

    for (auto &stimulus_config_iter : test_config.stimulus_configs) {
        if (stimulus_config_iter.second.type == StimulusType::Sparse ||
            stimulus_config_iter.second.type == StimulusType::SparseEncoding ||
            stimulus_config_iter.second.type == StimulusType::SparseEncodingNonzeroCounts) {
            sparse::get_op_info(&stimulus_config_iter.second, workload);
        }
    }

    if (test_config.test_args.count("program_loop_count")>0) {
       num_program_loops = stoi(test_config.test_args["program_loop_count"]);
    }
    log_info(tt::LogTest, "Running {} program loops...",
     num_program_loops);

    int input_count = 0;
    std::unordered_map<std::string, std::vector<tt_tensor>> init_output_tensors;
    string saved_pytorch_bin_root = args.pytorch_bin_path + "/";
    string saved_bin_root = args.bin_path + "/";

    bool first_loop = false;

    for (int loop=0; loop<num_loops; loop++) {
        log_info(tt::LogTest, "Started loop {} ", loop);
        first_loop = loop == 0;
        // Process input queues -- Process a microbatch worth of inputs for all queues in graph.
        if((args.determinism_tests and !loop) or !args.determinism_tests){
            for (const auto& it : input_queue_info_map) {
                string q_name = it.first;
                tt_queue_info q_info = it.second;
                tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
                input_count = q_info.input_count*num_program_loops;
                input_queues.insert({q_name, std::vector<tt_tensor>(input_count)});
                md.is_tilized = true;

                tt_tensor repeated_input = tt_tensor(md);

                for (int b = 0; b < input_count; ++b) {
                    input_queues.at(q_name).at(b) = tt_tensor(md);

                    // Load entry only on the first input for the first host loop iteration for:
                    //  - ram
                    //  - queue if it has only 1 entry but input count is > 1; which means it holds a constant and is consumed by prologue
                    if (q_info.type == IO_TYPE::RandomAccess || (q_info.type == IO_TYPE::Queue && q_info.entries == 1 && q_info.input_count > 1)) {
                        if (!first_loop || b>0) {
                            continue;
                        }
                    }

                    if (args.bin_path != "" && tt::data_binary::does_file_exists(saved_bin_root, q_name, q_info.input_count * loop + b))  {
                        log_info(tt::LogTest, "Loading binary for queue {} from bin-path {}", q_name, saved_bin_root);
                        verif::read_tensor_from_binary(saved_bin_root, q_info, args.reload_bin ? 0 : q_info.input_count * loop + b, input_queues.at(q_name).at(b), false);
                    } else if (args.pytorch_bin_path != "") {
                        log_info(tt::LogTest, "Loading binary for queue {} from pytorch_bin_path {}", q_name, saved_pytorch_bin_root);
                        verif::read_tensor_from_binary(
                        saved_pytorch_bin_root, q_info, args.reload_bin ? 0 : q_info.input_count * loop + b, input_queues.at(q_name).at(b), args.init_rams);
                    } else {
                        const VerifStimulusConfig &stimulus_config = verif::stimulus::get_config(q_name, test_config.stimulus_configs);
                        tt_tensor &io_tensor = input_queues.at(q_name).at(b);

                        if(args.repeat_tensor_in_microbatch){
                            if(b == 0){
                                generate_tensor(stimulus_config, repeated_input);
                            }
                            io_tensor = repeated_input;
                        } else {
                            generate_tensor(stimulus_config, io_tensor);
                        }
                    }

                    if (!args.determinism_tests) {
                        tt_tensor* golden_tensor;
                        if (args.stochastic_rnd_test && loop != 0) {
                            golden_tensor = &srnd_input_queues.at(q_name).at(b);
                            // string file_name = q_name + "_b_" + to_string(b) + "_loop_" + to_string(loop);
                            // golden_tensor.dump(file_name);
                        } else {
                            golden_tensor = &input_queues.at(q_name).at(b);
                        }
                        verif::push_tensor(golden, q_info.name, golden_tensor, q_info.type == IO_TYPE::Queue ? -1 : 0);
                    }
                }
            }

            if (args.stochastic_rnd_test && loop==0) {
                srnd_input_queues = input_queues;
            }
        }

        // Run program -- Need to make sure program is only 1 microbatch programed as looping is handled by test.
        if(!args.determinism_tests) {
            if (args.skip_golden) {
            } else {
               golden.run_programs();
            }
        }

        log_assert(input_count > 0, "input count has to be greated than 0");
        // Host input push
        vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc(); // Which Queues to push

        if (args.init_rams) {
            for (const auto& it : input_ram_info_map) {
                string q_name = it.first;
                tt_queue_info q_info = it.second;
                tt_dram_io_desc io_desc = runtime.get_queue_descriptor(q_name);
                tt::io::translate_addresses(io_desc);
                input_io_desc.push_back(io_desc);
            }
        }

        log_info(tt::LogTest, "Pushing all inputs...");
        auto push_start = std::chrono::high_resolution_clock::now();

        /*
        tt::io::push_host_inputs(
            input_io_desc,
            [&input_queues](tt_queue_info& queue_info, int entry_idx) {
                log_assert(
                    entry_idx < input_queues.at(queue_info.name).size(),
                    "Entry accessed by runtime is more than amount of inputs specified in test -- please increase "
                    "inputs to test or decrease entries in queue");  // FIXME Fatal on access issues
                log_assert(
                    input_queues.find(queue_info.name) != input_queues.end(),
                    "Cannot find queue in inputs created by golden");
                return input_queues.at(queue_info.name).at(entry_idx);
            },
            input_count, args.force_sw_tilize);
            */

        std::map<string, std::vector<tt_tensor>> golden_input_queues = (args.stochastic_rnd_test) ? srnd_input_queues : input_queues;

        auto get_golden_input = [&golden_input_queues](tt_queue_info& queue_info, int entry_idx) {
           log_assert(
               entry_idx < golden_input_queues.at(queue_info.name).size(),
               "Entry accessed by runtime is more than amount of inputs specified in test -- please increase "
               "inputs to test or decrease entries in queue");  // FIXME Fatal on access issues
           log_assert(
               golden_input_queues.find(queue_info.name) != golden_input_queues.end(), "Cannot find queue in inputs created by golden");
           return &golden_input_queues.at(queue_info.name).at(entry_idx);
        };

        for (tt_dram_io_desc &io : input_io_desc) {
            int num_inputs=1, ram_ptr=0;

            if (io.io_type == IO_TYPE::RandomAccess) {
                // Ram is pushed only once and on the first host loop iteration
                if (first_loop) {
                    num_inputs = 1;
                    ram_ptr = 0;
                } else {
                    continue;
                }
            } else {
                ram_ptr = -1;
                const auto& queue_info = workload.queues.at(io.queue_name).my_queue_info;
                if (queue_info.entries == 1 and queue_info.input_count > 1) {
                    // Queue with 1 entry is pushed only once and on the first host loop iteration
                    if (first_loop) {
                        num_inputs = 1;
                    } else {
                        continue;
                    }
                } else {
                    num_inputs = io.input_count*num_program_loops;
                }
            }

            tt_queue_info& queue_info = workload.queues.at(io.queue_name).my_queue_info;
            for (int i = 0; i < num_inputs; i++) {
                verif::push_tensor(
                    runtime,
                    queue_info.name,
                    get_golden_input(queue_info, i),
                    queue_info.type == IO_TYPE::Queue ? -1 : 0);
            }
        }

        auto push_end = std::chrono::high_resolution_clock::now();
        auto push_duration = std::chrono::duration_cast<std::chrono::microseconds>(push_end - push_start).count();
        log_info(tt::LogTest, "Push Elapsed Time: {} us", push_duration);

        for (std::string program_name: workload.program_order) {
            log_assert(runtime.run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
            log_assert(runtime.wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on target backend"); // explicitly wfi before popping from device
        }

        // Collect outputs via lazy method that returns all output queues
        log_info(tt::LogTest, "Runtime complete, collecting outputs from device");
        vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();

        log_info(tt::LogTest, "Popping all outputs...");
        auto pop_start = std::chrono::high_resolution_clock::now();
        auto pop_end = std::chrono::high_resolution_clock::now();
        auto pop_time = std::chrono::microseconds::zero();

        // Checking for q_info.input_count
        VerifComparisonConfig comparison_config = verif::comparison::read_from_yaml_file(args.netlist_path);

        for (const auto& it : golden.get_device_queue_info_map()) {
            string q_name = it.first;
            if (q_name.find("e2e") != std::string::npos) continue; // skip checking e2e queues
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            for (int b = 0; b < q_info.input_count * num_program_loops; ++b) {
                if(!args.determinism_tests){
                    tt_tensor golden_output;
                    golden_output.metadata = md;
                    if (args.skip_golden) {
                        if (args.pytorch_bin_path != "") {
                            log_info(tt::LogTest, "Loading binary for queue {} from pytorch_bin_path {}", q_info.name, saved_pytorch_bin_root);
                            verif::read_tensor_from_binary(
                                saved_pytorch_bin_root, q_info, args.reload_bin ? 0 : q_info.input_count * loop + b, golden_output, args.init_rams);
                        } else {
                            if (!args.skip_check) {
                                log_error("pytorch_bin_path has to be set when skip-golden flag is used!");
                            }
                        }
                    } else {
                        if (q_info.type == IO_TYPE::Queue) {
                            verif::get_and_pop_tensor(golden, q_info.name, &golden_output);
                        } else {
                            if (b>0) continue;
                            verif::get_and_pop_tensor(golden, q_info.name, &golden_output, 0);
                        }
                    }

                    tt_tensor observed_output;
                    pop_start = std::chrono::high_resolution_clock::now();
                    if (q_info.type == IO_TYPE::Queue) {
                        verif::get_and_pop_tensor(runtime, q_info.name, &observed_output);
                    } else {
                        if (b>0) continue;
                        verif::get_and_pop_tensor(runtime, q_info.name, &observed_output, 0);
                    }
                    pop_end = std::chrono::high_resolution_clock::now();
                    pop_time += std::chrono::duration_cast<std::chrono::microseconds>(pop_end - pop_start);

                    if (!args.skip_check) {
                        log_info(tt::LogTest, "Checking Entry idx={}", b);
                        if (not compare_tensors(
                            golden_output,
                            observed_output,
                            verif::comparison::get_config(q_name, test_config.comparison_configs))) {
                            pass = false;
                            log_error("Entry idx={} Mismatched", b);
                            log_error("Queue: {}", q_name);
                        }
                    } else {
                        log_warning(tt::LogTest, "Skipping output golden check since --skip-check flag is set");
                    }

                    if (args.stochastic_rnd_test) {
                        if(q_info.type != IO_TYPE::Queue and b > 0) continue;
                        if(!loop){
                            if(!b){
                                vector<tt_tensor> tensor_queue;
                                init_output_tensors[q_name] = tensor_queue;
                            }
                            init_output_tensors[q_name].push_back(observed_output);
                        } else {
                            if(compare_tensors_exact(init_output_tensors[q_name][b], observed_output)) {
                                pass = false;
                                log_error("Non-random output: Entry idx={} for output={} in run={}", b, q_name, loop);
                            }
                        }
                        // string file_name = "output_" + q_name + "_b_" + to_string(b) + "_loop_" + to_string(loop);
                        // (*observed_output).dump(file_name);
                    }
                } else {
                    tt_tensor observed_output;
                    pop_start = std::chrono::high_resolution_clock::now();
                    if (q_info.type == IO_TYPE::Queue) {
                        verif::get_and_pop_tensor(runtime, q_info.name, &observed_output);
                    } else {
                        if (b>0) continue;
                        verif::get_and_pop_tensor(runtime, q_info.name, &observed_output, 0);
                    }
                    pop_end = std::chrono::high_resolution_clock::now();
                    pop_time += std::chrono::duration_cast<std::chrono::microseconds>(pop_end - pop_start);
                    if(!loop){
                        if(!b){
                            vector<tt_tensor> tensor_queue;
                            init_output_tensors[q_name] = tensor_queue;
                        }
                        init_output_tensors[q_name].push_back(observed_output);
                    } else {
                        //observed_output -> tile_tensor[0][0][0][0].set(0, 0, observed_output -> tile_tensor[0][0][0][0].get(0, 0) + 100); //For debugging purposes
                        if(not compare_tensors(init_output_tensors[q_name][b], observed_output, verif::comparison::get_config("determinism_test", test_config.comparison_configs))){
                            pass = false;
                            log_error("Non Deterministic Output: Entry idx={} for output={} in run={} Mismatched with initial output", b, q_name, loop);
                        }
                    }
                }
            }
        }
        log_info(tt::LogTest, "Pop Elapsed Time: {} us", pop_time.count());
        log_info(tt::LogTest, "Ended loop {}. ", loop);
    }
    runtime.finish();
    done = true;
}

int main(int argc, char** argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_op since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    tt::assert::register_segfault_handler();

    std::vector<std::string> input_args(argv, argv + argc);
    log_info(tt::LogTest, "test_op");
    test_args args = parse_test_args(input_args);

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



