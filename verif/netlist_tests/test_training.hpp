// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "utils.hpp"  //FIXME: Links to model
#include "verif.hpp"

using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

namespace training {

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

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}

struct test_args {
    std::string netlist_path = "";
    std::string tti_path = "";
    std::string output_dir = "";
    bool compile_only = false;
    bool run_only = false;
    int num_loops = 1;
    int microbatch_count = 1;
    int seed = -1;
    int opt_level = 0;
    int io_timeout = 1;
    std::string backend = "";
    std::string golden = "";
    std::vector<std::string> input_names = {};
    std::vector<std::string> weight_names = {};
    std::vector<std::string> output_names = {};
    std::string vcd_dump_cores = "";
    bool help = false;
    bool determinism_tests = false;
    perf::PerfDesc perf_desc;
    std::shared_ptr<tt::tt_device_image> tti;
    bool repeat_tensor_in_microbatch = false;
    int determinism_tests_override_opt_level = -1;
    string tensor_binary_dir = "";
    set<verif::ExternalBinaryMode> read_tensor_binaries = {};
    set<verif::ExternalBinaryMode> write_tensor_binaries = {};
    bool external_binaries_single_entry = false;
    std::string default_test_config_path = "";
    void parse_from_tti() {
        tti = std::make_shared<tt::tt_device_image>(tti_path, output_dir);
        netlist_path = tti->get_netlist_path();
        input_names = tti->get_graph_input_names();
        weight_names = tti->get_graph_constant_and_parameter_names();
        output_names = tti->get_graph_output_names();
    }
    bool is_input(std::string name) {
        return std::find(input_names.begin(), input_names.end(), name) != input_names.end();
    }
    bool is_output(std::string name) {
        return std::find(output_names.begin(), output_names.end(), name) != output_names.end();
    }
    bool is_weight(std::string name) {
        return std::find(weight_names.begin(), weight_names.end(), name) != weight_names.end();
    }
};

void check_valid_external_options(test_args &args) {
    for (auto write_mode: args.write_tensor_binaries) {
        log_assert(args.read_tensor_binaries.find(write_mode) == args.read_tensor_binaries.end(), "Read and write tensor binaries should not be both enabled for an operand");
    }
    if (args.read_tensor_binaries.size() > 0) {
        log_assert(args.tensor_binary_dir != "", "binary dir not initialized");
        args.tensor_binary_dir += "/";        
        log_assert(fs::exists(args.tensor_binary_dir), "binary dir not found");
    }
    if (args.write_tensor_binaries.size() > 0) {
        log_assert(args.tensor_binary_dir != "",  "binary dir not initialized");
        args.tensor_binary_dir += "/";
        if (!fs::exists(args.tensor_binary_dir)) {
            fs::create_directory(args.tensor_binary_dir);
        }
    }
}

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--outdir <>          : Custom output dir path, must be provided for run-only to locate pre-compiled objects\n";
    help_string += "--compile-only       : Compile only and skip run, compiled objects are stored in outdir\n";
    help_string += "--run-only           : Run only, an output dir with precompiled objects must be given\n";
    help_string += "--num-loops <>       : Number of loops of programs to run (Default: 1)\n";
    help_string += "--microbatch-count <> : Number of microbatch loops to run within the program to run (Default: 1)\n";
    help_string += "--seed <>            : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>         : Path to netlist file\n";
    help_string += "--backend            : Target backend to run to (Default: Silicon)\n";
    help_string += "--golden             : Golden reference to compare to (Default: Golden)\n";
    help_string += "--vcd-dump-cores <>  : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\"\n";
    help_string += "--inputs             : Host inputs to device\n";
    help_string += "--outputs            : Device outputs to host\n";
    help_string += "--determinism-tests         : Check if the target backend generates consistent outputs on the same input and seed across num-loops runs (Default: false)\n";
    help_string += "--repeat-tensor-in-microbatch : Use the same tensor across all inputs in a microbatch (useful for determinism tests)\n";    
    help_string += "--help               : Prints this message\n";
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
        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.microbatch_count, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--microbatch-count", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.opt_level, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--O", 2); // default to highest level
        std::tie(args.io_timeout, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--io-timeout", 1);
        std::tie(args.netlist_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--netlist", "netlist.yaml");
        std::tie(args.tti_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--tti", "");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Silicon");
        std::tie(args.golden, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--golden", "Golden");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--inputs", "");
        tt::args::split_string_into_vector(args.input_names, cmdline_arg, ",");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outputs", "");
        tt::args::split_string_into_vector(args.output_names, cmdline_arg, ",");

        std::tie(args.determinism_tests, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--determinism-tests");
        std::tie(args.repeat_tensor_in_microbatch, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--repeat-tensor-in-microbatch");
        args.perf_desc = perf::PerfDesc(input_args, args.netlist_path);
        std::tie(args.determinism_tests_override_opt_level, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--determinism-tests-override-opt-level", -1);

        std::tie(args.default_test_config_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--default-test-config", "verif/netlist_tests/default_test_config.yaml");

        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        if (args.tti_path != "") {
            args.parse_from_tti();
        }
        // Cmdline args for external tensor binaries
        string read_tensor_binaries_str;
        string write_tensor_binaries_str;
        std::tie(args.tensor_binary_dir, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--tensor-binary-dir", "");
        std::tie(read_tensor_binaries_str, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--read-tensor-binaries", "");
        std::tie(write_tensor_binaries_str, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--write-tensor-binaries", "");
        std::tie(args.external_binaries_single_entry, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--external-use-first-entry");
        args.read_tensor_binaries = verif::cmdline_to_external_binary_mode(read_tensor_binaries_str);
        args.write_tensor_binaries = verif::cmdline_to_external_binary_mode(write_tensor_binaries_str);
        check_valid_external_options(args);

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

    // Randomization seeding
    if (args.seed == -1) {
        args.seed = tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", args.seed);
    }
    tt_rnd_set_seed(args.seed);

    // ----------------------------------------------------------------
    // Test configurations
    // ----------------------------------------------------------------
    netlist_workload_data workload(args.netlist_path);

    // Test configs for stimulus and comparison
    auto config = verif::test_config::read_from_yaml_file(args.netlist_path, args.default_test_config_path);

    // Append netlist config to test args
    args.input_names.insert(args.input_names.end(), config.io_config["inputs"].begin(), config.io_config["inputs"].end());
    args.output_names.insert(args.output_names.end(), config.io_config["outputs"].begin(), config.io_config["outputs"].end());

    const int sequence_lenth   = verif::test_config::get<int>(config.test_args, "sequence_lenth", 128);
    const int head_size = verif::test_config::get<int>(config.test_args, "head_size", 4);

    std::map<std::string, std::string> program_parameters = {
        {"$p_microbatch_count", std::to_string(args.microbatch_count)},
        {"$p_loop_count", std::to_string(args.microbatch_count)},
        {"$p_zero_grad", std::to_string(1)},
    };


    // ----------------------------------------------------------------
    // Check for usage issues
    // ----------------------------------------------------------------

    if (!args.compile_only) {
        bool has_binary_constants = args.tti or args.read_tensor_binaries.count(verif::ExternalBinaryMode::Constants) > 0;
        for (auto graph_it: workload.graphs) {
            for (auto &op_it: graph_it.second.my_graph_info.op_map) {
                log_assert(!(op_it.second.type == "matmul" && op_it.second.attributes.identity) || has_binary_constants,
                    "Netlist contains sparse-matmuls, must provide external tensor binaries for constants.");
            }
        }
    }

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
    tt_backend_config golden_config = {
        .type = get_device_from_string(args.golden),
        .arch = workload.device_info.arch,
        .optimization_level = args.opt_level, 
        .output_dir=args.output_dir
    };
    if (args.compile_only) {
        target_config.mode = DEVICE_MODE::CompileOnly;
    } else if (args.run_only) {
        target_config.mode = DEVICE_MODE::RunOnly;
    }

    std::shared_ptr<tt_backend> target_backend, golden_backend;
    if (target_config.type != tt::DEVICE::Invalid) {
        target_backend = (args.tti) ? tt_backend::create(args.tti, target_config)
                                    : tt_backend::create(args.netlist_path, target_config);
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        golden_backend = (args.tti) ? tt_backend::create(args.tti, golden_config)
                                    : tt_backend::create(args.netlist_path, golden_config);
    }

    std::shared_ptr<tt_runtime> runtime;
    if (target_config.type == tt::DEVICE::Versim or target_config.type == tt::DEVICE::Silicon) {
        runtime = std::dynamic_pointer_cast<tt_runtime>(target_backend);
    }

    tt_compile_result compile_result;
    if (target_config.type != tt::DEVICE::Invalid) {
        log_assert(target_backend->initialize(&compile_result) == tt::DEVICE_STATUS_CODE::Success,  "Expected Target Backend to get initialized");
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        //Only init golden backend if not running determinism tests
        log_assert(golden_backend->initialize(&compile_result) == tt::DEVICE_STATUS_CODE::Success,  "Expected Golden Backend to get initialized");
    }

    if (!args.compile_only)
    {
        // Populate host io: activations, params, constants (init_io_names)
        // (ram_io_names) Store names of any IO queues present in RAM. Need this to override weight updates across loops (during training). After each weight update, send the initial weight values to these queues.
        std::vector<std::string> init_io_names, ram_io_names;

        for (auto &io : workload.queues) {
            tt_queue_info &q_info = io.second.my_queue_info;
            bool is_output = std::find(args.output_names.begin(), args.output_names.end(), q_info.name) != args.output_names.end();
            bool is_e2e = q_info.name.find("e2e") != std::string::npos;
            bool is_buffering = q_info.name.find("buffer") != std::string::npos;
            if (is_output or is_e2e or is_buffering) {
                // do not initialize output or e2e queues
                continue;
            }
            if (args.tti and args.is_weight(q_info.name)) {
                // do not initialize weights for pre-commpiled model
                continue;
            }
            init_io_names.push_back(q_info.name);
            // Store io queue names initialized in RAM
            if(q_info.type == IO_TYPE::RandomAccess) 
                ram_io_names.push_back(q_info.name);
        }

        // ----------------------------------------------------------------
        // Backend run
        // Each loop performs {drive inputs, run programs, check outputs}
        // ----------------------------------------------------------------
        constexpr int ram_ptr = 0;
        // Umap storing a tensor value for each input queue's microbatch. Tensor values are generated in the first loop of determinism testing and reused across loops to ensure input consistency.
        std::unordered_map<std::string, std::vector<tt_tensor>> input_map;
        // Umap storing a vector of tensors for each output queue's microbatch. Tensor values are generated in the first loop during determinism testing.
        std::unordered_map<std::string, std::vector<std::vector<float>>> init_output_tensors_flat;

        for (int n = 0; n < args.num_loops; ++n) {
            //
            // Drive inputs and params
            //
            if(!args.determinism_tests) {
                log_info(tt::LogTest, "Running test loop: {} of args.num_loops: {}", n, args.num_loops);
            }
            else {
                log_info(tt::LogTest, "Running Determinism Test {} of {} ", n, args.num_loops);
            }

            if (args.determinism_tests && args.determinism_tests_override_opt_level > 0 && n > 0 && runtime){
                log_warning(tt::LogTest, "Overriding backend opt_level: {} for test: {} - May not be safe.", args.determinism_tests_override_opt_level, n);
                runtime->set_loader_settings(args.determinism_tests_override_opt_level);
            }

            std::set<std::string> input_names(args.input_names.begin(), args.input_names.end());
            log_assert(input_names.size() > 0 , "input_names must not be empty!");

            for (int m = 0; m < args.microbatch_count; ++m) {
                for (const auto &io_name : init_io_names) {
                    tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(io_name);
                    tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
                    tt_queue_info &queue_info = workload.queues.at(q_desc.queue_name).my_queue_info;

                    bool is_first_input = n == 0 and m == 0;
                    bool is_param = std::find(ram_io_names.begin(), ram_io_names.end(), io_name) != ram_io_names.end();
                    bool is_activation_input = std::find(input_names.begin(), input_names.end(), io_name) != input_names.end();
                    int num_pushes = is_activation_input ? q_desc.input_count : 1; // if this is not an activation input, we only push it once (batch size = 1)
                    
                    if(is_param) {
                        log_assert( queue_info.entries == 1, "Queue {} corresponds to a parameter and must not have a single entry.", q_desc.queue_name);
                    }

                    if(is_activation_input) md.shape.w *= q_desc.input_count; //this is a streaming activation which may correspond to a queue with multiple slots
                    if(is_param) md.shape.w = 1; // this is a parameter which must be pushed for each determinism run

                    bool initialize_input = is_first_input or is_activation_input or (is_param and args.determinism_tests);

                    if(initialize_input){
                        tt_tensor io_tensor(md);
                        // dynamic data: activations are always driven for every input. with determinism_tests = true, the same weights are driven for every input (for output consistency)
                        // static data: constants are driven for the first input. with determinism_tests = false, weights are driven only for the first input
                        if((args.determinism_tests and !n) or !args.determinism_tests){
                            if (
                                (args.read_tensor_binaries.find(verif::ExternalBinaryMode::Constants) != args.read_tensor_binaries.end() && !is_activation_input) ||
                                (args.read_tensor_binaries.find(verif::ExternalBinaryMode::AllInputs) != args.read_tensor_binaries.end() && is_activation_input)) {

                                string saved_bin_root = args.tensor_binary_dir + "/";
                                log_assert(tt::data_binary::does_file_exists(saved_bin_root, io_name, n),
                                    "In 'read_tensor_from_binary' mode the binaries must exist for tensor {} under following path: {}", io_name, tt::data_binary::get_filename(saved_bin_root, io_name, n));
                                
                                log_info(tt::LogTest, "Loading binary for queue {} from bin-path {}", io_name, saved_bin_root);
                                // Currently pybuda only stores the binaries for the first entry
                                // Adding the option to use those to populate every entry of input queueus
                                if (is_activation_input && args.external_binaries_single_entry) {
                                    verif::read_tensor_from_binary(saved_bin_root, queue_info, 0, io_tensor, false, num_pushes - 1);
                                } else {
                                    verif::read_tensor_from_binary(saved_bin_root, queue_info, n, io_tensor, false);
                                }

                            } else {
                                if(args.repeat_tensor_in_microbatch) {
                                    // Reduce this tensor down to a single block. Populate the block and stack it w-times to replicate it across the ubatch (downstream).
                                    io_tensor.metadata.shape.w = 1;
                                }
                                
                                // derive io tensor context based on wildcard names and use custom stimulus
                                if (io_name.find("eps") != std::string::npos) {
                                    verif::random::set_number(io_tensor, std::numeric_limits<float>::epsilon());
                                } else if (io_name.find("mean") != std::string::npos) {
                                    verif::random::set_number(io_tensor, 1.0f/sequence_lenth);
                                } else if (io_name.find("var") != std::string::npos) {
                                    verif::random::set_number(io_tensor, 1.0f/sequence_lenth);
                                } else if (io_name.find("sum") != std::string::npos) {
                                    verif::random::set_number(io_tensor, 1.0f);
                                } else if (io_name.find("reciprocal_of_sqrt_of_head_size") != std::string::npos) {
                                    verif::random::set_number(io_tensor, 1.0f/std::sqrt(head_size));
                                } else {
                                    generate_tensor(verif::stimulus::get_config(io_name, config.stimulus_configs), io_tensor);
                                }

                                if(args.repeat_tensor_in_microbatch and is_activation_input) {
                                    // Stack single block w-times to replicate it across the ubatch.
                                    std::vector<tt_tensor> temp_tensors;
                                    for(int i = 0; i < q_desc.input_count; i++) {
                                        temp_tensors.push_back(io_tensor);
                                    }
                                    io_tensor = tt_tensor::wstack(temp_tensors);
                                }
                            }
                            if(args.determinism_tests and !n) {
                                // For determinism checking populate the input map. Use this to push input tensors.  The id of the tensor being stored in the input map is derived from the io_queue
                                input_map[io_name].push_back(io_tensor);
                            }
                            if (
                                (args.write_tensor_binaries.find(verif::ExternalBinaryMode::Constants) != args.write_tensor_binaries.end() && !is_activation_input) ||
                                (args.write_tensor_binaries.find(verif::ExternalBinaryMode::AllInputs) != args.write_tensor_binaries.end() && is_activation_input)) {
                                    verif::write_tensor_to_binary(args.tensor_binary_dir, io_name, 0, io_tensor);
                            }
                        }
                        log_info(tt::LogTest, "Pushing batched tensors for io_name: {} is_activation_input: {} is_first_input: {}", io_name, is_activation_input, is_first_input);

                        if (target_config.type != tt::DEVICE::Invalid) {
                            if(args.determinism_tests){
                                // For determinism tests push tensors from the input map. Ensures consistency of inputs across runs.
                                tt_tensor temp = input_map.at(io_name).at(m); // The id of the tensor being stored in the input map is derived from the io_queue
                                verif::push_batched_tensor(*target_backend, io_name, &temp, is_activation_input, ram_ptr, args.io_timeout);
                            }
                            else{
                                verif::push_batched_tensor(*target_backend, io_name, &io_tensor, is_activation_input, ram_ptr, args.io_timeout);
                            }
                        }
                        if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                            // only push to golden if created and initialized (not running determinism tests)
                            verif::push_batched_tensor(*golden_backend, io_name, &io_tensor, is_activation_input, ram_ptr, args.io_timeout);
                        }
                    }
                }
            }
            //
            // Run programs
            //
            for (std::string program_name : workload.program_order) {
                if (target_config.type != tt::DEVICE::Invalid) {
                    log_assert(target_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
                }

                if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                    //Only run golden if not checking for determinism
                    log_assert(golden_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
                }
            }

            // When accessing RAM outputs, there are no pointers for HOST to poll for occupancy
            // since there is no way know when data has been committed to DRAM, let's insert explicit WFI
            if (target_config.type != tt::DEVICE::Invalid) {
                log_assert(target_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI execute successfuly on target backend");
            }
            if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
                log_assert(golden_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI execute successfuly on golden backend");
            }
            
            //
            // Check outputs
            //
            std::set<std::string> output_names(args.output_names.begin(), args.output_names.end());
            for (const auto &output_name : output_names) {
                if (workload.queues.find(output_name) != workload.queues.end()) {
                    log_info(tt::LogTest, "Reading and checking output {}", output_name);
                    tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);

                    for (int m = 0; m < args.microbatch_count; ++m) {
                        // Checking for q_info.input_count
                        std::vector<float> observed_flat, expected_flat;
                        if(target_config.type != tt::DEVICE::Invalid){
                            verif::get_and_pop_flat_tensors(*target_backend, output_name, observed_flat, ram_ptr, args.io_timeout);
                        }
                        if (args.write_tensor_binaries.find(verif::ExternalBinaryMode::AllOutputs) != args.write_tensor_binaries.end()) {
                            tt::data_binary::dump_file(tt::data_binary::get_filename(args.tensor_binary_dir, output_name, 0), observed_flat);
                        }

                        if(!args.determinism_tests){
                            // Comparing observed output against a golden model.
                            if (args.write_tensor_binaries.find(verif::ExternalBinaryMode::AllOutputs) != args.write_tensor_binaries.end()) {
                                string data_filepath = tt::data_binary::get_filename(args.tensor_binary_dir, output_name, 0);
                                log_info(tt::LogTest, "Reading the expected output tensor from {}", data_filepath);
                                tt::data_binary::read_file(data_filepath, expected_flat);
                            } else {
                                if (golden_config.type != tt::DEVICE::Invalid){
                                    verif::get_and_pop_flat_tensors(*golden_backend, output_name, expected_flat, ram_ptr, args.io_timeout);
                                }
                                if (target_config.type == tt::DEVICE::Invalid){
                                    observed_flat = expected_flat;
                                }
                                if (golden_config.type == tt::DEVICE::Invalid){
                                    expected_flat = observed_flat;
                                }
                            }
                            pass &= compare_tensors(expected_flat, observed_flat, verif::comparison::get_config(output_name, config.comparison_configs), q_desc);
                        }

                        else{
                            // Running determinism test: get results from first run and then compare results from subsequent runs with the same input.
                            if(!n){ //First run (just store the output)
                                init_output_tensors_flat[output_name].push_back(observed_flat);
                            }
                            else{ // Compare against the previous output and fail test 
                                // Testing for non-deterministic results requires an exact comparison between tensors (with a specific comparison config)
                                pass &= compare_tensors(init_output_tensors_flat.at(output_name).at(m), observed_flat, verif::comparison::get_config("determinism_test", config.comparison_configs), q_desc);
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
            int total_num_samples = calculate_total_sample_count(target_backend, workload.queues, args.input_names, args.num_loops);
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
    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }

    return 0;
}

}
