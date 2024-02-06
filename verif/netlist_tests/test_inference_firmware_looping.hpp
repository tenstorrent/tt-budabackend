// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>
#include <chrono>
#include <ctime>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "utils.hpp"  //FIXME: Links to model
#include "verif.hpp"
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

namespace inference {

int calculate_total_sample_count(std::shared_ptr<tt_backend> target_backend, std::map<string, tt_queue_wrap> queues, std::vector<std::string> input_names, int loop_count) {
    for (auto &queue: queues) {
        tt_queue_info &q_info = queue.second.my_queue_info;
        if (q_info.input == "HOST" && q_info.type == IO_TYPE::Queue) {
            if (std::find(input_names.begin(), input_names.end(), q_info.name) != input_names.end()) {
                tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(q_info.name);
                return q_desc.input_count * loop_count;
            }
        }
    }
    return -1;
}

struct test_args {
    std::string netlist_path = "";
    std::string output_dir = "";
    bool compile_only = false;
    bool run_only = false;
    int num_loops = 1;
    int seed = -1;
    int opt_level = 0;
    int io_timeout = 1;
    std::string backend = "";
    std::string golden = "";
    std::vector<std::string> input_names = {};
    std::vector<std::string> output_names = {};
    std::string vcd_dump_cores = "";
    bool help = false;
    perf::PerfDesc perf_desc;
    std::string cluster_desc_path = "";
    std::string host_data_format = "";
    bool determinism_tests = false;
    int host_hstack_factor = 1;
    int host_vstack_factor = 1;
    int host_stack_row_major = 1;
    bool push_all_entries = false;
    bool skip_check = false;
    string tensor_binary_dir = "";
};

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--outdir <>                 : Custom output dir path, must be provided for run-only to locate pre-compiled objects\n";
    help_string += "--compile-only              : Compile only and skip run, compiled objects are stored in outdir\n";
    help_string += "--run-only                  : Run only, an output dir with precompiled objects must be given\n";
    help_string += "--num-loops <>              : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>                   : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>                : Path to netlist file\n";
    help_string += "--backend                   : Target backend to run to (Default: Silicon)\n";
    help_string += "--golden                    : Golden reference to compare to (Default: Golden)\n";
    help_string += "--cluster-desc              : File path to multichip cluster configuration yaml (Default: "")\n";
    help_string += "--vcd-dump-cores <>         : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\"\n";
    help_string += "--inputs                    : Host inputs to device\n";
    help_string += "--outputs                   : Device outputs to host\n";
    help_string += "--determinism-tests         : Check if the target backend generates consistent outputs on the same input and seed across num-loops runs (Default: false)\n";
    help_string += "--push-all-entries          : For each loop, push num_entries tensors to each queue instead of num_inputs of current graph.\n";
    help_string += "--skip-check                : Skips the golden/observed check.\n";
    help_string += "--tensor-binary-dir         : Reads all the tensors (except for inputs to the graph) from the binaries";
    help_string += "--help                      : Prints this message\n";
    try {
        std::tie(args.output_dir, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outdir", "");
        if (args.output_dir == "") {
            args.output_dir = verif_filesystem::get_output_dir(__FILE__, input_args);
        }
        std::tie(args.compile_only, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--compile-only");
        std::tie(args.run_only, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--run-only");

        std::string cmdline_arg;
        // std::tie(firmware_num_loop_iterations, input_args) =
        //     verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.opt_level, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--O", 2); // default to highest level
        std::tie(args.io_timeout, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--io-timeout", 1);
        std::tie(args.netlist_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--netlist", "netlist.yaml");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Silicon");
        std::tie(args.golden, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--golden", "Golden");
        std::tie(args.cluster_desc_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--cluster-desc", "");
        std::tie(args.host_data_format, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--host-data-format", "QueueFormat");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--inputs", "");
        tt::args::split_string_into_vector(args.input_names, cmdline_arg, ",");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outputs", "");

        tt::args::split_string_into_vector(args.output_names, cmdline_arg, ",");

        std::tie(args.determinism_tests, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--determinism-tests");
        std::tie(args.host_hstack_factor, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--host-hstack-factor", 1);
        std::tie(args.host_vstack_factor, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--host-vstack-factor", 1);
        std::tie(args.host_stack_row_major, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--host-stack-row-major", 1);

        args.perf_desc = perf::PerfDesc(input_args, args.netlist_path);
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        std::tie(args.push_all_entries, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--push-all-entries");
        std::tie(args.skip_check, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--skip-check");
        std::tie(args.tensor_binary_dir, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--tensor-binary-dir", "");
        verif_args::validate_remaining_args(input_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (args.help) {
        log_info(tt::LogTest, "Usage Help:\n{}", help_string);
        exit(0);
    }
    return args;
}

int run(std::vector<std::string> &input_args)
{
    bool pass = true;
    string input_args_flattened = flatten_input_args(input_args);
    test_args args = parse_test_args(input_args);
    auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    log_info(tt::LogTest, "{} - Starting test for netlist {}.", strtok(std::ctime(&start_time), "\n"), args.netlist_path);

    // Randomization seeding
    if (args.seed == -1) {
        args.seed = tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", args.seed);
    }
    tt_rnd_set_seed(args.seed);
    bool external_tensors = args.tensor_binary_dir != "";

    // ----------------------------------------------------------------
    // Test configurations
    // ----------------------------------------------------------------
    netlist_workload_data workload(args.netlist_path);

    // Test configs for stimulus and comparison
    auto config = verif::test_config::read_from_yaml_file(args.netlist_path, "verif/netlist_tests/default_test_config.yaml");

    // Append netlist config to test args
    args.input_names.insert(args.input_names.end(), config.io_config["inputs"].begin(), config.io_config["inputs"].end());
    args.output_names.insert(args.output_names.end(), config.io_config["outputs"].begin(), config.io_config["outputs"].end());

    const int sequence_length   = verif::test_config::get<int>(config.test_args, "sequence_length", 128);
    const int microbatch_count = verif::test_config::get<int>(config.test_args, "microbatch_count", 1);
 
    std::map<std::string, std::string> program_parameters = {
        {"$p_microbatch_count", std::to_string(microbatch_count)},
        {"$p_loop_count", std::to_string(microbatch_count)},
    };

    // O3 runtime removes all barriers, and runs without any epoch barriers
    bool syncless_mode = (args.opt_level > 2);

    if (syncless_mode) {
        log_assert(args.io_timeout > 0, "Sync free mode does not guarantee queue empty/full for push/drain, a timeout value must be used!");
    }

    auto num_exec_loop_iterations_env_var = std::getenv("NUM_EXEC_LOOP_ITERATIONS");
    // log_assert(num_exec_loop_iterations_env_var != nullptr, "Environment variable NUM_EXEC_LOOP_ITERATIONS must be specified");
    int firmware_num_loop_iterations =  num_exec_loop_iterations_env_var != nullptr ? std::stoi(num_exec_loop_iterations_env_var) : 1;

    // ----------------------------------------------------------------
    // Backend initialization
    // ----------------------------------------------------------------
    tt_backend_config target_config = {
        .type = get_device_from_string(args.backend),
        .arch = workload.device_info.arch,
        .optimization_level = args.opt_level, 
        .output_dir=args.output_dir,
        .perf_desc_args=input_args_flattened,
    };
    if (args.cluster_desc_path.size() > 0) {
        target_config.cluster_descriptor_path = args.cluster_desc_path;
    }
    if (args.compile_only) {
        target_config.mode = DEVICE_MODE::CompileOnly;
    } else if (args.run_only) {
        target_config.mode = DEVICE_MODE::RunOnly;
    }

    tt_backend_config golden_config = {
        .type = get_device_from_string(args.golden),
        .arch = workload.device_info.arch,
        .optimization_level = args.opt_level, 
        .output_dir=args.output_dir
    };

    std::shared_ptr<tt_backend> target_backend, golden_backend;
    if (target_config.type != tt::DEVICE::Invalid) {
        target_backend = tt_backend::create(args.netlist_path, target_config);
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        //Only create golden backend if not running determinism tests
        golden_backend = tt_backend::create(args.netlist_path, golden_config);
    }

    // Performance counter setup
    std::shared_ptr<tt_runtime> runtime;
    if (target_config.type == tt::DEVICE::Versim or target_config.type == tt::DEVICE::Silicon) {
        runtime = std::dynamic_pointer_cast<tt_runtime>(target_backend);
        if (target_config.type == tt::DEVICE::Versim && args.vcd_dump_cores.compare("") != 0) {
            runtime->config.device_params.vcd_dump_cores = verif_args::get_vcd_dump_cores_from_arg_string(args.vcd_dump_cores);
        }
    }

    if (target_config.type != tt::DEVICE::Invalid) {
        log_assert(target_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        //Only init golden backend if not running determinism tests
        log_assert(golden_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Golden Backend to get initialized");
    }

    if (!args.compile_only)
    {        
        for (const std::string &input_name : args.input_names) {
            log_assert(workload.queues.find(input_name) != workload.queues.end(), "Specified name of an input queue {} which isn't a name of any queue in the netlist", input_name);
        }
        for (const std::string &output_name : args.output_names) {
            log_assert(workload.queues.find(output_name) != workload.queues.end(), "Specified name of an input queue {} which isn't a name of any queue in the netlist",  output_name);
        }
        // Populate host io: activations, params, constants
        std::vector<std::string> init_io_names = {};
        for (auto &io : workload.queues) {
            tt_queue_info &q_info = io.second.my_queue_info;
            if (q_info.input == "HOST") {
                init_io_names.push_back(q_info.name);
            }
        }

        // ----------------------------------------------------------------
        // Backend run
        // Each loop performs {drive inputs, run programs, check outputs}
        // ----------------------------------------------------------------
        constexpr int ram_init_ptr = 0;
        //Umap storing a tensor value for each input. Tensor values are generated in the first loop of determinism testing and reused across loops to ensure input consistency.
        std::unordered_map<std::string, tt_tensor> input_map;
        //Umap storing a vector of tensors for each output queue. Tensor values are generated in the first loop during determinism testing.
        std::unordered_map<std::string, std::vector<float>> init_output_tensors_flat;

        log_info(tt::LogTest, "Running {} loop iterations", firmware_num_loop_iterations);

        for (int n = 0; n < firmware_num_loop_iterations; ++n) {
            if(!args.determinism_tests) {
                log_info(tt::LogTest, "Running test loop: {} of firmware_num_loop_iterations: {}", n, firmware_num_loop_iterations);
            }
            else {
                log_info(tt::LogTest, "Running Determinism Test {} of {} ", n, firmware_num_loop_iterations);
            }
            //
            // Drive inputs and params
            //
            std::set<std::string> input_names(args.input_names.begin(), args.input_names.end());

            log_assert(input_names.size() > 0 , "input_names must not be empty!");
            for (const auto &io_name : init_io_names) {
                tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(io_name);
                tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
                tt_queue_info &queue_info = workload.queues.at(q_desc.queue_name).my_queue_info;
                if (args.host_data_format != "QueueFormat") {
                    md.data_format = STRING_TO_DATA_FORMAT.at(args.host_data_format);
                }

                int num_pushes = args.push_all_entries ? queue_info.entries : q_desc.input_count; // determine if the entire queue gets filled
                bool is_first_input = n == 0; 
                bool is_activation_input = std::find(input_names.begin(), input_names.end(), io_name) != input_names.end();
                num_pushes = is_activation_input ? num_pushes : 1; // if this is not an activation input, we only push it once (batch size = 1)
                
                md.shape.w *= num_pushes;; //update batch size to send all tensors at once

                if(is_first_input or is_activation_input){
                    tt_tensor io_tensor(md);
                    // dynamic data: activations are always driven for every input
                    // static data: constant and params are driven for the first input

                    if((args.determinism_tests and !n) or !args.determinism_tests){
                        if (external_tensors && !is_activation_input) {
                            string saved_bin_root = args.tensor_binary_dir + "/";
                            // log_assert(verif::does_binary_exists(saved_bin_root, io_name, n),
                            //     "In 'read_tensor_from_binary' mode the binaries must exist for tensor " + io_name + " under following path: ");//+ verif::get_binary_filename(saved_bin_root, io_name, n));
                            
                            log_info(tt::LogTest, "Loading binary for queue {} from bin-path {}", io_name, saved_bin_root);
                            verif::read_tensor_from_binary(saved_bin_root, queue_info, n, io_tensor, false);

                        } else {
                            //Only generate new input tensors in this loop if not running determinism checking or if this is the first loop during a determinism test.
                            // derive io tensor context based on wildcard names and use custom stimulus
                            if (io_name.find("eps") != std::string::npos) {
                                verif::random::set_number(io_tensor, std::numeric_limits<float>::epsilon());
                            } else if (io_name.find("mean") != std::string::npos) {
                                verif::random::set_number(io_tensor, 1.0f/sequence_length);
                            } else if (io_name.find("var") != std::string::npos) {
                                verif::random::set_number(io_tensor, 1.0f/sequence_length);
                            } else if (io_name.find("sum") != std::string::npos) {
                                verif::random::set_number(io_tensor, 1.0f);
                            } else {
                                    generate_tensor(verif::stimulus::get_config(io_name, config.stimulus_configs), io_tensor);
                            }
                        }
                        if(args.determinism_tests and !n)
                            // For determinism checking populate the input map. Use this to push input tensors. The id of the tensor being stored in the input map is derived from the io_queue
                            input_map[io_name] = io_tensor;
                    }
                    // push the same tensor into both backends
                    

                    log_info(tt::LogTest, "Pushing batched tensors for io_name: {} is_activation_input: {} is_first_input: {}", io_name, is_activation_input, is_first_input);
                    if (target_config.type != tt::DEVICE::Invalid){
                        if(args.determinism_tests){
                            log_info(tt::LogTest, " --> Pushing tensor {} to backend. Data: {}", io_name, io_tensor.at(0,0,0,0,0,0));
                            // For determinism tests push tensors from the input map. Ensures consistency of inputs across runs.
                            tt_tensor temp = input_map[io_name]; //The id of the tensor being stored in the input map is derived from the io_queue
                            verif::push_batched_tensor(*target_backend, io_name, &temp, is_activation_input, ram_init_ptr, args.io_timeout);
                        }
                        else{
                            log_info(tt::LogTest, " --> Pushing tensor {} to backend. Data: {}", io_name, io_tensor.at(0,0,0,0,0,0));
                            verif::push_batched_tensor(*target_backend, io_name, &io_tensor, is_activation_input, ram_init_ptr, args.io_timeout);
                        }
                    }
                    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                        // only push to golden if created and initialized (not running determinism tests)
                        log_info(tt::LogTest, " --> Pushing tensor {} to golden. Data: {}", io_name, io_tensor.at(0,0,0,0,0,0));
                        verif::push_batched_tensor(*golden_backend, io_name, &io_tensor, is_activation_input, ram_init_ptr, args.io_timeout);
                    }
                }
                
            }

            //
            // Run programs
            //
            auto current_timestamp = perf::LoaderClock::now();
            for (std::string program_name : workload.program_order) {
                if (target_config.type != tt::DEVICE::Invalid) {
                    if  (n == 0) {
                        log_info(tt::LogTest, "-------------------");
                        log_info(tt::LogTest, "RUNNING PROGRAMS on SILICON...");
                        log_assert(target_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
                    }
                }
                if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                    //Only run golden if not checking for determinism
                    log_info(tt::LogTest, "-------------------");
                    log_info(tt::LogTest, "RUNNING PROGRAMS on GOLDEN...");
                    log_assert(golden_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
                }
            }
            
            if (!syncless_mode) {
                log_info(tt::LogTest, "-------------------");
                log_info(tt::LogTest, "WAITING for IDLE...");
                // When accessing RAM outputs, there are no pointers for HOST to poll for occupancy
                // since there is no way know when data has been committed to DRAM, let's insert explicit WFI
                if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                    log_info(tt::LogTest, "... on golden");
                    log_assert(golden_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on target backend");
                }
                if (target_config.type != tt::DEVICE::Invalid) {
                    if (n == (firmware_num_loop_iterations - 1)) {
                        log_info(tt::LogTest, "... on target backend");
                        log_assert(target_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on golden backend");
                    }
                }
            }

            //
            // Check outputs
            //
            log_info(tt::LogTest, "-------------------");
            log_info(tt::LogTest, "Checking outputs");

            std::set<std::string> output_names(args.output_names.begin(), args.output_names.end());
            log_assert(output_names.size() > 0 , "output_names must not be empty!");
            
            for (const auto &output_name : output_names) {
                if (workload.queues.find(output_name) != workload.queues.end()) {
                    log_info(tt::LogTest, "Reading and checking output {}", output_name);
                    tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
                    // Checking for q_info.input_count
                    std::vector<float> observed_flat, expected_flat;
                    log_info(tt::LogTest, "Getting tensor {}", output_name);
                    if (target_config.type != tt::DEVICE::Invalid) {
                        verif::get_and_pop_flat_tensors(*target_backend, output_name, observed_flat, -1, args.io_timeout);
                        log_info(tt::LogTest, " <-- Checking output {} from backend. Data: {}", output_name, observed_flat);
                    }
                    if(!args.determinism_tests){
                        // Comparing observed output against a golden model.
                        if (golden_config.type != tt::DEVICE::Invalid) {
                            verif::get_and_pop_flat_tensors(*golden_backend, output_name, expected_flat, -1, args.io_timeout);
                            log_info(tt::LogTest, " <-- Checking output {} from golden. Data: {}", output_name, expected_flat);
                        }
                        if (target_config.type == tt::DEVICE::Invalid){
                            log_info(tt::LogTest, "UH OH1");
                            observed_flat = expected_flat;
                        }
                        if (golden_config.type == tt::DEVICE::Invalid){
                            log_info(tt::LogTest, "UH OH2");
                            expected_flat = observed_flat;
                        }
                        if (!args.skip_check) {
                            log_assert(expected_flat.size() == observed_flat.size(), "expected and observed size mismatch");
                            pass &= compare_tensors(expected_flat, observed_flat, verif::comparison::get_config(output_name, config.comparison_configs), q_desc);
                        }
                    }
                    else {
                        // Running determinism test: get results from first run and then compare results from subsequent runs with the same input.
                        if(!n){ // First run (just store the output)
                            init_output_tensors_flat[output_name] = observed_flat;
                        } else{ // Compare against the previous output and fail test 
                            // Testing for non-deterministic results requires an exact comparison between tensors (with a specific comparison config)
                            if (!args.skip_check) {
                                log_assert(init_output_tensors_flat.at(output_name).size() == observed_flat.size(), "expected and observed size mismatch");
                                pass &= compare_tensors(init_output_tensors_flat.at(output_name), observed_flat, verif::comparison::get_config("determinism_test", config.comparison_configs), q_desc);
                            }
                        }
                    }
                } else {
                    pass = false;
                    log_error("Cannot find output queue with name {}", output_name);
                }
            }

        }
    }

    // ----------------------------------------------------------------
    // Backend teardown
    // ----------------------------------------------------------------
    if (target_config.type != tt::DEVICE::Invalid) {
        if (runtime) {
            int total_num_samples = calculate_total_sample_count(target_backend, workload.queues, args.input_names, firmware_num_loop_iterations);
            runtime->perf_set_total_number_of_samples(total_num_samples);
        }
        log_assert(target_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target device to close successfuly");
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        log_assert(golden_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected golden device to close successfuly");
    }

    // Performance dump summary
    if (runtime) {
        std::vector<tt::EpochPerfInfo> perf = runtime->get_perf_info();
        for (auto &epoch : perf) {
            log_info(tt::LogTest, "Program = {}, Graph = {}, Tensors/s = {:.2f}, Cycles/tensor = {:.2f} ", epoch.program_name, epoch.graph_name, epoch.num_inputs_per_second, epoch.num_cycles_per_input);
        }
    }

    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    log_info(tt::LogTest, "{} - Finished test for netlist {}. Duration was {}s", strtok(std::ctime(&end_time), "\n"), args.netlist_path, elapsed_seconds.count());

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
    return 0;
}

}