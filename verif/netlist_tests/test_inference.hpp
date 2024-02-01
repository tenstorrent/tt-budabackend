// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>

#include "common/tti_lib.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "utils.hpp"  //FIXME: Links to model
#include "verif.hpp"
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;
using namespace tt;

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
    std::string tti_path = "";
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
    std::vector<std::string> untilized_inputs = {};
    std::vector<std::string> weight_names = {};
    std::vector<std::string> output_names = {};
    std::string vcd_dump_cores = "";
    bool help = false;
    perf::PerfDesc perf_desc;
    std::shared_ptr<tt::tt_device_image> tti = nullptr;
    std::string device_desc_path = "";
    std::string cluster_desc_path = "";
    std::string host_data_format = "";
    bool determinism_tests = false;
    int host_hstack_factor = 1;
    int host_vstack_factor = 1;
    int host_stack_row_major = 1;
    bool push_all_entries = false;
    bool skip_check = false;
    string tensor_binary_dir = "";
    set<verif::ExternalBinaryMode> read_tensor_binaries = {};
    set<verif::ExternalBinaryMode> write_tensor_binaries = {};
    bool external_binaries_single_entry = false;
    bool repeat_tensor_in_microbatch = false;
    int stride;
    std::vector<int> input_shape;
    bool unaligned_conv_activation = false;
    bool concurrent = false;
    std::string default_test_config_path = "";
    bool use_netlist_input_output_list = false;
    bool run_programs_out_of_order = false;
    std::string harvesting_masks = "";
    void parse_from_tti() {
        tti = std::make_shared<tt::tt_device_image>(tti_path, output_dir);
        netlist_path = tti->get_netlist_path();;
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
        log_assert(args.tensor_binary_dir != "", "binary dir not initialized");
        args.tensor_binary_dir += "/";
        if (!fs::exists(args.tensor_binary_dir)) {
            fs::create_directory(args.tensor_binary_dir);
        }
    }
}

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}

void push_data_to_device(test_args& args, std::vector<std::string>& init_io_names, int loop_iter, std::shared_ptr<tt_backend> target_backend, std::shared_ptr<tt_backend> golden_backend, netlist_workload_data& workload, int ram_ptr, VerifTestConfig& config, 
                        tt_backend_config& target_config, tt_backend_config& golden_config, std::unordered_map<std::string, tt_tensor>& input_map, std::unordered_map<std::string, tt_tensor>& inputs_for_golden, bool only_push_act = false, bool only_push_weights = false) {
    if (!args.determinism_tests) {
        log_info(tt::LogTest, "Running test loop: {} of args.num_loops: {}", loop_iter + 1, args.num_loops);
    }
    else {
        log_info(tt::LogTest, "Running Determinism Test {} of {} ", loop_iter + 1, args.num_loops);
    }

    std::set<std::string> input_names(args.input_names.begin(), args.input_names.end());
    log_assert(input_names.size() > 0 , "input_names must not be empty!");

    for (const auto &io_name : init_io_names) {
        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(io_name);
        if(args.stride > 0) {
            q_desc.s_descriptor.stride = args.stride;
            for(int x = 0; x < args.stride; x++){
                for(int y = 0; y < args.stride; y++){
                    q_desc.s_descriptor.xy_offsets.push_back({x, y});
                }
            }
        }
        tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
        tt_queue_info &queue_info = workload.queues.at(q_desc.queue_name).my_queue_info;
        if (args.host_data_format != "QueueFormat") {
            md.data_format = STRING_TO_DATA_FORMAT.at(args.host_data_format);
        }

        int num_pushes = args.push_all_entries ? queue_info.entries : q_desc.input_count; // determine if the entire queue gets filled
        bool is_first_input = loop_iter == 0; 
        bool is_activation_input = std::find(input_names.begin(), input_names.end(), io_name) != input_names.end();
        num_pushes = is_activation_input ? num_pushes : 1; // if this is not an activation input, we only push it once (batch size = 1)

        md.shape.w *= num_pushes;; //update batch size to send all tensors at once
        if(args.stride > 0) {
            // Stride passed in through cmdline
            log_assert(args.input_shape.size(), "Need to specify input shape for stride > 0!");
            md.shape.w = args.input_shape[0];
            md.shape.z = args.input_shape[1];
            md.shape.rt = args.input_shape[2];
            md.shape.ct = args.input_shape[3];
        }
        else if(q_desc.s_descriptor.stride > 0) {
            // Prestride transform specified in TTI
            log_assert(args.tti, "Expected a TTI to passed in if queue descriptor has an implicit stride");
            // Get "pre" prestride shape from TTI metadata
            md.shape.z = std::get<0>(args.tti -> get_io_prestride_map().at(io_name));
            md.shape.rt = std::get<1>(args.tti -> get_io_prestride_map().at(io_name)) / q_desc.tile_height;
            md.shape.ct = std::get<2>(args.tti -> get_io_prestride_map().at(io_name)) / q_desc.tile_width;
        }
        bool push_input;
        if(only_push_act or only_push_weights) {
            push_input = (only_push_act && is_activation_input) || (only_push_weights && !is_activation_input);
        } else {
            push_input = is_first_input or is_activation_input;
        }
        if(push_input){
            if(!args.unaligned_conv_activation) {
                tt_tensor io_tensor(md);
                // dynamic data: activations are always driven for every input
                // static data: constant and params are driven for the first input

                if((args.determinism_tests and !loop_iter) or !args.determinism_tests){
                    if (
                        (args.read_tensor_binaries.find(verif::ExternalBinaryMode::Constants) != args.read_tensor_binaries.end() && !is_activation_input) ||
                        (args.read_tensor_binaries.find(verif::ExternalBinaryMode::AllInputs) != args.read_tensor_binaries.end() && is_activation_input)) {

                        string saved_bin_root = args.tensor_binary_dir + "/";
                        log_assert(tt::data_binary::does_file_exists(saved_bin_root, io_name, loop_iter),
                            "In 'read_tensor_from_binary' mode the binaries must exist for tensor {} under following path: {}", io_name, tt::data_binary::get_filename(saved_bin_root, io_name, loop_iter));
                        
                        log_info(tt::LogTest, "Loading binary for queue {} from bin-path {}", io_name, saved_bin_root);
                        // Currently pybuda only stores the binaries for the first entry
                        // Adding the option to use those to populate every entry of input queueus
                        if (is_activation_input && args.external_binaries_single_entry) {
                            verif::read_tensor_from_binary(saved_bin_root, queue_info, 0, io_tensor, false, num_pushes - 1);
                        } else {
                            verif::read_tensor_from_binary(saved_bin_root, queue_info, loop_iter, io_tensor, false);
                        }

                        if (q_desc.s_descriptor.stride > 0) {
                            // If running convolution tests: Preserve host or netlist specified data format of the tensor when reading from binary 
                            io_tensor.metadata.data_format = md.data_format;
                        }
                    } else {
                        //Only generate new input tensors in this loop if not running determinism checking or if this is the first loop during a determinism test.
                        // derive io tensor context based on wildcard names and use custom stimulus
                        if(args.repeat_tensor_in_microbatch) {
                            io_tensor.metadata.shape.w = 1;
                        }
                        
                        const int sequence_length   = verif::test_config::get<int>(config.test_args, "sequence_length", 128);
                        
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
                    if(args.repeat_tensor_in_microbatch) {
                        std::vector<tt_tensor> temp_tensors;
                        for(int i = 0; i < num_pushes; i++) {
                            temp_tensors.push_back(io_tensor);
                        }
                        io_tensor = tt_tensor::wstack(temp_tensors);
                    }
                    
                    if(!args.determinism_tests && args.concurrent) {
                        inputs_for_golden[io_name + "_loop_" + std::to_string(loop_iter)] = io_tensor;
                    }

                    if(args.determinism_tests and !loop_iter)
                        // For determinism checking populate the input map. Use this to push input tensors. The id of the tensor being stored in the input map is derived from the io_queue
                        input_map[io_name] = io_tensor;
                    if (
                        (args.write_tensor_binaries.find(verif::ExternalBinaryMode::Constants) != args.write_tensor_binaries.end() && !is_activation_input) ||
                        (args.write_tensor_binaries.find(verif::ExternalBinaryMode::AllInputs) != args.write_tensor_binaries.end() && is_activation_input)) {
                            verif::write_tensor_to_binary(args.tensor_binary_dir, io_name, loop_iter, io_tensor);
                    }
                }
                // push the same tensor into both backends
                

                log_info(tt::LogTest, "Pushing batched tensors for io_name: {} is_activation_input: {} is_first_input: {}", io_name, is_activation_input, is_first_input);
                if (target_config.type != tt::DEVICE::Invalid){
                    bool untilize_flat_as_tt_tensor = std::find(args.untilized_inputs.begin(), args.untilized_inputs.end(), io_name) != args.untilized_inputs.end();
                    if(args.determinism_tests){
                        // For determinism tests push tensors from the input map. Ensures consistency of inputs across runs.
                        tt_tensor temp = input_map[io_name]; //The id of the tensor being stored in the input map is derived from the io_queue
                        verif::push_batched_tensor(*target_backend, io_name, &temp, is_activation_input, ram_ptr, args.io_timeout, q_desc, untilize_flat_as_tt_tensor);
                    }
                    else{
                        verif::push_batched_tensor(*target_backend, io_name, &io_tensor, is_activation_input, ram_ptr, args.io_timeout, q_desc, untilize_flat_as_tt_tensor);
                    }
                }
                if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests and !args.concurrent) {
                    // only push to golden if created and initialized (not running determinism tests)
                    auto tmp_queue_desc = q_desc;
                    if(std::find(args.untilized_inputs.begin(), args.untilized_inputs.end(), io_name) != args.untilized_inputs.end()) tmp_queue_desc.layout = IO_LAYOUT::Tilized;
                    verif::push_batched_tensor(*golden_backend, io_name, &io_tensor, is_activation_input, ram_ptr, args.io_timeout, tmp_queue_desc);
                }
            }
            else {
                // Generate Pytorch tensor directly, since face dims are not aligned to tile size
                tt::tt_PytorchTensorDesc tensor_for_shuffling;
                std::vector<float> input_data = std::vector<float>(args.input_shape[0] * args.input_shape[1] * args.input_shape[2] * args.input_shape[3], 1);
                
                if(md.data_format == tt::DataFormat::Float32) {
                    input_data = std::vector<float>(args.input_shape[0] * args.input_shape[1] * args.input_shape[2] * args.input_shape[3], 0);
                    for(int i = 0; i < input_data.size(); i++)
                        input_data[i] = i % 100000; // Bound data between 0 & 99999
                    
                    tensor_for_shuffling.ptr = input_data.data();
                    tt::io::set_py_tensor_metadata(tensor_for_shuffling, {static_cast<uint32_t>(args.input_shape[0]), static_cast<uint32_t>(args.input_shape[1]), static_cast<uint32_t>(args.input_shape[2]), static_cast<uint32_t>(args.input_shape[3])}, sizeof(float), tt::PY_TENSOR_DIMS, md.data_format);
                    verif::push_pytorch_tensor(*target_backend, io_name, tensor_for_shuffling, is_activation_input, ram_ptr, args.io_timeout, q_desc);
                    verif::push_pytorch_tensor(*golden_backend, io_name, tensor_for_shuffling, is_activation_input, ram_ptr, args.io_timeout, q_desc);
                } 
                else {
                    log_error("Unaligned convolution testing is only enabled for host data format Float32!");
                }
            }
        }

    }
}

void run_programs_on_device(test_args& args, std::shared_ptr<tt_backend> target_backend, std::shared_ptr<tt_backend> golden_backend, netlist_workload_data& workload,  std::map<std::string, std::string>& program_parameters, tt_backend_config& target_config, tt_backend_config& golden_config) {
    auto programs = workload.program_order;
    auto program_shuffler = std::default_random_engine{};
    if (args.run_programs_out_of_order) {
        std::shuffle(std::begin(programs), std::end(programs), program_shuffler);
    }
    for (std::string program_name : programs) {
        if (target_config.type != tt::DEVICE::Invalid) {
            log_assert(target_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
        }
        if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests and ! args.concurrent) {
            //Only run golden if not checking for determinism
            log_assert(golden_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
        }
    }
    // No barriers needed when running with opt level >= 3
    if (args.opt_level <= 2) {
        // When accessing RAM outputs, there are no pointers for HOST to poll for occupancy
        // since there is no way know when data has been committed to DRAM, let's insert explicit WFI
        if (target_config.type != tt::DEVICE::Invalid) {
            log_assert(target_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on target backend");
        }
        if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests and !args.concurrent) {
            log_assert(golden_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on golden backend");
        }
    }   
}

void get_data_from_device(test_args& args, int loop_iter, std::shared_ptr<tt_backend> target_backend, std::shared_ptr<tt_backend> golden_backend, netlist_workload_data& workload, int ram_ptr, VerifTestConfig& config, tt_backend_config& target_config, 
    tt_backend_config& golden_config, std::unordered_map<std::string, std::vector<float>>& init_output_tensors_flat,  std::unordered_map<std::string, std::vector<float>>& outputs_for_golden, bool& pass) {
    std::set<std::string> output_names(args.output_names.begin(), args.output_names.end());
    log_assert(output_names.size() > 0 , "output_names must not be empty!");
    
    for (const auto &output_name : output_names) {
        if (workload.queues.find(output_name) != workload.queues.end()) {
            log_info(tt::LogTest, "Reading and checking output {}", output_name);
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
            q_desc.hstack_factor = args.host_hstack_factor;
            q_desc.vstack_factor = args.host_vstack_factor;
            q_desc.stack_row_major = args.host_stack_row_major;

            // Checking for q_info.input_count
            std::vector<float> observed_flat, expected_flat;
            if (target_config.type != tt::DEVICE::Invalid) {
                verif::get_and_pop_flat_tensors(*target_backend, output_name, observed_flat, ram_ptr, args.io_timeout, args.host_hstack_factor, args.host_vstack_factor, args.host_stack_row_major);
            }
            if (args.write_tensor_binaries.find(verif::ExternalBinaryMode::AllOutputs) != args.write_tensor_binaries.end()) {
                tt::data_binary::dump_file(tt::data_binary::get_filename(args.tensor_binary_dir, output_name, loop_iter), observed_flat);
            }
            if(!args.determinism_tests){
                // Comparing observed output against a golden model.
                if (args.read_tensor_binaries.find(verif::ExternalBinaryMode::AllOutputs) != args.read_tensor_binaries.end()) {
                    string data_filepath = tt::data_binary::get_filename(args.tensor_binary_dir, output_name, loop_iter);
                    log_info(tt::LogTest, "Reading the expected output tensor from {}", data_filepath);
                    tt::data_binary::read_file(data_filepath, expected_flat);
                } else {
                    if(!args.concurrent) {
                        if (golden_config.type != tt::DEVICE::Invalid) {
                            verif::get_and_pop_flat_tensors(*golden_backend, output_name, expected_flat, ram_ptr, args.io_timeout, args.host_hstack_factor, args.host_vstack_factor, args.host_stack_row_major);
                        }
                        if (target_config.type == tt::DEVICE::Invalid){
                            observed_flat = expected_flat;
                        }
                        if (golden_config.type == tt::DEVICE::Invalid){
                            expected_flat = observed_flat;
                        }
                    }
                    else {
                        outputs_for_golden[output_name + "_loop_" + std::to_string(loop_iter)] = observed_flat;
                    }
                }
                // Skip check here if skip_check flag is set or if running in concurrent mode without reference binaries
                bool skip_check = args.skip_check or (args.concurrent ?
                                    args.read_tensor_binaries.find(verif::ExternalBinaryMode::AllOutputs) == args.read_tensor_binaries.end() : false);
                if (!skip_check) {
                    log_assert(expected_flat.size() == observed_flat.size(), "expected and observed size mismatch");
                    pass &= compare_tensors(expected_flat, observed_flat, verif::comparison::get_config(output_name, config.comparison_configs), q_desc);
                }
            }
            else {
                // Running determinism test: get results from first run and then compare results from subsequent runs with the same input.
                if(!loop_iter){ // First run (just store the output)
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

void run_sequential_test(test_args& args, std::vector<std::string>& init_io_names, std::shared_ptr<tt_backend> target_backend, std::shared_ptr<tt_backend> golden_backend, netlist_workload_data& workload, VerifTestConfig& config, tt_backend_config& target_config, tt_backend_config& golden_config,  std::unordered_map<std::string, tt_tensor>& inputs_for_golden, std::unordered_map<std::string, std::vector<float>>& outputs_for_golden, std::map<std::string, std::string> program_parameters, bool& pass) {
    std::unordered_map<std::string, std::vector<float>> init_output_tensors_flat;
    std::unordered_map<std::string, tt_tensor> input_map;
    int ram_ptr = 0;
    
    for(int n = 0; n < args.num_loops; ++n) {
        // Push data
        push_data_to_device(args, init_io_names, n, target_backend, golden_backend, workload, ram_ptr, config, target_config, golden_config, input_map, inputs_for_golden);
        // Run programs
        run_programs_on_device(args, target_backend, golden_backend, workload, program_parameters, target_config, golden_config);
        // Pop and check outputs
        get_data_from_device(args, n, target_backend, golden_backend, workload, ram_ptr, config, target_config, golden_config, init_output_tensors_flat, outputs_for_golden, pass);
    }
}
void run_concurrent_test(test_args& args, std::vector<std::string>& init_io_names, std::shared_ptr<tt_backend> target_backend, std::shared_ptr<tt_backend> golden_backend, netlist_workload_data& workload, VerifTestConfig& config, tt_backend_config& target_config, tt_backend_config& golden_config, std::unordered_map<std::string, tt_tensor>& inputs_for_golden, std::unordered_map<std::string, std::vector<float>>& outputs_for_golden, std::map<std::string, std::string> program_parameters, bool& pass) {
    std::vector<std::thread> threads;
    std::unordered_map<std::string, tt_tensor> input_map;
    std::unordered_map<std::string, std::vector<float>> init_output_tensors_flat;
    int ram_ptr = 0;

    push_data_to_device(args, init_io_names, 0, target_backend, golden_backend, workload, ram_ptr, config, target_config, golden_config, input_map, inputs_for_golden, false, true); // Push parameters to device (this is part of compile in Pybuda)
    // ---------- Start Concurrent Run ----------
    // Push activations
    threads.push_back(std::thread([&] {
        // Set thread seed to the main thread seed plus one
        // to make generating tensors predictable yet avoid generating the same tensor as the main thread.
        tt_rnd_set_seed(args.seed + 1);
        for(int n = 0; n < args.num_loops; ++n) {
            push_data_to_device(args, init_io_names, n, target_backend, golden_backend, workload, ram_ptr, config, target_config, golden_config, input_map, inputs_for_golden, true, false);
        }
    }));
    
    //
    // Run programs
    //
    threads.push_back(std::thread([&] {
        for(int n = 0; n < args.num_loops; ++n) {
            run_programs_on_device(args, target_backend, golden_backend, workload, program_parameters, target_config, golden_config);
        }
    }));
    //
    // Pop outputs
    //
    threads.push_back(std::thread([&] {
        for(int n = 0; n < args.num_loops; ++n) {
            get_data_from_device(args, n, target_backend, golden_backend, workload, ram_ptr, config, target_config, golden_config, init_output_tensors_flat, outputs_for_golden, pass);
        }
    }));
    
    for(auto& th: threads) th.join();
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
    help_string += "--device-desc               : File path to devic soc configuration yaml (Default: "")\n";
    help_string += "--cluster-desc              : File path to multichip cluster configuration yaml (Default: "")\n";
    help_string += "--vcd-dump-cores <>         : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\"\n";
    help_string += "--inputs                    : Host inputs to device\n";
    help_string += "--outputs                   : Device outputs to host\n";
    help_string += "--determinism-tests         : Check if the target backend generates consistent outputs on the same input and seed across num-loops runs (Default: false)\n";
    help_string += "--push-all-entries          : For each loop, push num_entries tensors to each queue instead of num_inputs of current graph.\n";
    help_string += "--skip-check                : Skips the golden/observed check.\n";
    help_string += "--tensor-binary-dir         : Reads all the tensors (except for inputs to the graph) from the binaries";
    help_string += "--repeat-tensor-in-microbatch : Use the same tensor across all inputs in a microbatch (useful for determinism tests)\n";
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
        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.opt_level, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--O", 2); // default to highest level
        std::tie(args.io_timeout, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--io-timeout", 10);
        std::tie(args.netlist_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--netlist", "");
        std::tie(args.tti_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--tti", "");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Silicon");
        std::tie(args.golden, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--golden", "Golden");
        std::tie(args.device_desc_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--device-desc", "");
        std::tie(args.cluster_desc_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--cluster-desc", "");
        std::tie(args.host_data_format, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--host-data-format", "QueueFormat");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--inputs", "");
        tt::args::split_string_into_vector(args.input_names, cmdline_arg, ",");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--untilized-inputs", "");
        tt::args::split_string_into_vector(args.untilized_inputs, cmdline_arg, ",");
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
        std::tie(args.stride, input_args) = verif_args::get_command_option_int32_and_remaining_args(input_args, "--stride", -1);
        std::tie(args.unaligned_conv_activation, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--unaligned_conv_activation");
        std::tie(cmdline_arg, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--input-tensor-shape", "");
        verif_args::split_string_into_vector(args.input_shape, cmdline_arg, ",");

        std::tie(args.default_test_config_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--default-test-config", "verif/netlist_tests/default_test_config.yaml");

        bool sequential;
        std::tie(sequential, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--sequential");
        args.concurrent= !sequential;
        if(args.backend == "Versim") {
            // Override concurrent var for versim. Run this backend in sequential
            args.concurrent = false;
            log_info(tt::LogTest, "Running test in sequential mode for Versim");
        }
        if (args.tti_path != "") {
            args.parse_from_tti();
        }
        args.perf_desc = perf::PerfDesc(input_args, args.netlist_path);
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        std::tie(args.push_all_entries, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--push-all-entries");
        std::tie(args.skip_check, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--skip-check");
        
        // Cmdline args for external tensor binaries
        string read_tensor_binaries_str;
        string write_tensor_binaries_str;
        std::tie(args.tensor_binary_dir, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--tensor-binary-dir", "");
        std::tie(read_tensor_binaries_str, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--read-tensor-binaries", "");
        std::tie(write_tensor_binaries_str, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--write-tensor-binaries", "");
        std::tie(args.external_binaries_single_entry, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--external-use-first-entry");
        std::tie(args.repeat_tensor_in_microbatch, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--repeat-tensor-in-microbatch");
        std::tie(args.harvesting_masks, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--harvesting-masks", "");
        std::tie(args.run_programs_out_of_order, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--run-programs-out-of-order");
        args.read_tensor_binaries = verif::cmdline_to_external_binary_mode(read_tensor_binaries_str);
        args.write_tensor_binaries = verif::cmdline_to_external_binary_mode(write_tensor_binaries_str);
        std::tie(args.use_netlist_input_output_list, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--use-netlist-input-output-list");
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
    if(args.unaligned_conv_activation) log_assert(!args.concurrent, "Concurrent Test Inference Mode does not support Convolution tests with unaligned inputs. Please run with --sequential flag");
    auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    log_info(tt::LogTest, "{} - Starting test for netlist {}.", strtok(std::ctime(&start_time), "\n"), args.netlist_path);

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
    if (args.use_netlist_input_output_list) {
        log_assert(args.input_names.empty() && args.output_names.empty(), "If --use-netlist-input-output-list is specified, it is not allowed to manually pass in the list of input and output queues.");
        verif::get_input_output_queue_names(args.netlist_path, args.input_names, args.output_names);
        log_info(tt::LogTest, "Input-queue-names parsed from the netlist:");
        for (auto input: args.input_names) {
            log_info(tt::LogTest, "queue_name: {}", input);
        }
        log_info(tt::LogTest, "Output-queue-names parsed from the netlist:");
        for (auto output: args.output_names) {
            log_info(tt::LogTest, "queue_name: {}", output);
        }
    }


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

    // O3 runtime removes all barriers, and runs without any epoch barriers
    if (args.opt_level > 2) {
        log_assert(args.io_timeout > 0, "Sync free mode does not guarantee queue empty/full for push/drain, a timeout value must be used!");
    }

    // ----------------------------------------------------------------
    // Backend initialization
    // ----------------------------------------------------------------
    tt_backend_config target_config = {
        .type = get_device_from_string(args.backend),
        .arch = workload.device_info.arch,
        .optimization_level = args.opt_level, 
        .output_dir=args.output_dir,
        .soc_descriptor_path=args.device_desc_path,
        .cluster_descriptor_path=args.cluster_desc_path,
        .perf_desc_args=input_args_flattened,
    };
    if (args.compile_only) {
        target_config.mode = DEVICE_MODE::CompileOnly;
    } else if (args.run_only) {
        target_config.mode = DEVICE_MODE::RunOnly;
    }
    
    if(args.harvesting_masks.size()) {
        target_config.harvested_rows = verif::parse_harvesting_masks(args.harvesting_masks);
    }
    tt_backend_config golden_config = {
        .type = get_device_from_string(args.golden),
        .arch = workload.device_info.arch,
        .optimization_level = args.opt_level, 
        .output_dir=args.output_dir,
        .soc_descriptor_path=args.device_desc_path,
        .cluster_descriptor_path=args.cluster_desc_path,
    };

    std::shared_ptr<tt_backend> target_backend, golden_backend;
    if (target_config.type != tt::DEVICE::Invalid) {
        target_backend = (args.tti) ? tt_backend::create(args.tti, target_config)
                                    : tt_backend::create(args.netlist_path, target_config);
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        golden_backend = (args.tti) ? tt_backend::create(args.tti, golden_config)
                                    : tt_backend::create(args.netlist_path, golden_config);
    }

    // Performance counter setup
    std::shared_ptr<tt_runtime> runtime;
    if (target_config.type == tt::DEVICE::Versim or target_config.type == tt::DEVICE::Silicon) {
        runtime = std::dynamic_pointer_cast<tt_runtime>(target_backend);
        if (target_config.type == tt::DEVICE::Versim && args.vcd_dump_cores.compare("") != 0) {
            runtime->config.device_params.vcd_dump_cores = verif_args::get_vcd_dump_cores_from_arg_string(args.vcd_dump_cores);
        }
    }

    tt_compile_result compile_result;
    if (target_config.type != tt::DEVICE::Invalid) {
        log_assert(target_backend->initialize(&compile_result) == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized" );
        if (runtime) {
            int total_num_samples = calculate_total_sample_count(target_backend, workload.queues, args.input_names, args.num_loops);
            runtime->perf_set_total_number_of_samples(total_num_samples);
        }
    }
    if (golden_config.type != tt::DEVICE::Invalid and !args.determinism_tests) {
        //Only init golden backend if not running determinism tests
        log_assert(golden_backend->initialize(&compile_result) == tt::DEVICE_STATUS_CODE::Success, "Expected Golden Backend to get initialized");
    }

    if (!args.compile_only)
    {        
        for (const std::string &input_name : args.input_names) {
            log_assert(workload.queues.find(input_name) != workload.queues.end(), "Specified name of an input queue {} which isn't a name of any queue in the netlist", input_name);
        }
        for (const std::string &output_name : args.output_names) {
            log_assert(workload.queues.find(output_name) != workload.queues.end(), "Specified name of an input queue {} which isn't a name of any queue in the netlist",  output_name);
        }
        std::vector<std::string> init_io_names = {};
        for (auto& io : workload.queues) {
            tt_queue_info& q_info = io.second.my_queue_info;
            if (q_info.input == "HOST") {
                if (args.tti and args.is_weight(q_info.name)) continue;
                init_io_names.push_back(q_info.name);
            }
        }

        const int sequence_length = verif::test_config::get<int>(config.test_args, "sequence_length", 128);

        std::map<std::string, std::string> program_parameters = {
            {"$p_microbatch_count", verif::test_config::get<string>(config.test_args, "microbatch_count", "1")},
            {"$p_loop_count", verif::test_config::get<string>(config.test_args, "microbatch_count", "1")},
            {"$p_cache_write_index", verif::test_config::get<string>(config.test_args, "cache_write_index", "0")},
            {"$p_inner_increment", verif::test_config::get<string>(config.test_args, "inner_increment", "0")},
            {"$p_inner_loop_count", verif::test_config::get<string>(config.test_args, "inner_loop_count", "1")},
            {"$p_outer_increment", verif::test_config::get<string>(config.test_args, "outer_increment", "0")},
            {"$p_outer_loop_count", verif::test_config::get<string>(config.test_args, "outer_loop_count", "1")},
        };
        // ----------------------------------------------------------------
        // Backend run
        // Each loop performs {drive inputs, run programs, check outputs}
        // ----------------------------------------------------------------
       

        std::unordered_map<std::string, tt_tensor> inputs_for_golden;
        std::unordered_map<std::string, std::vector<float>> outputs_for_golden;

        if(args.concurrent) {
            constexpr int ram_ptr = 0;
            run_concurrent_test(args, init_io_names, target_backend, golden_backend, workload, config, target_config, golden_config, inputs_for_golden, outputs_for_golden, program_parameters, pass);
            // Run golden after concurrent silicon run for varification
            if(golden_config.type != tt::DEVICE::Invalid && args.concurrent && !args.skip_check && !args.determinism_tests) {
                log_info(tt::LogTest, "Comparing Concurrent Mode Device Outputs with Golden");
                std::set<std::string> input_names(args.input_names.begin(), args.input_names.end());
                std::set<std::string> output_names(args.output_names.begin(), args.output_names.end());

                for(int n = 0; n < args.num_loops; ++n) {
                    for (const auto &io_name : init_io_names) {
                        bool is_first_input = n == 0; 
                        bool is_activation_input = std::find(input_names.begin(), input_names.end(), io_name) != input_names.end();

                        if(is_first_input or is_activation_input) {
                            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(io_name);
                            
                            if(args.stride > 0) {
                                q_desc.s_descriptor.stride = args.stride;
                                for(int x = 0; x < args.stride; x++){
                                    for(int y = 0; y < args.stride; y++){
                                        q_desc.s_descriptor.xy_offsets.push_back({x, y});
                                    }
                                }
                            }
                            auto tmp_queue_desc = q_desc;
                            if(std::find(args.untilized_inputs.begin(), args.untilized_inputs.end(), io_name) != args.untilized_inputs.end()) tmp_queue_desc.layout = IO_LAYOUT::Tilized;
                            verif::push_batched_tensor(*golden_backend, io_name, &inputs_for_golden[io_name + "_loop_" + std::to_string(n)], is_activation_input, ram_ptr, args.io_timeout, tmp_queue_desc);
                        }
                    }

                    for (std::string program_name : workload.program_order) {
                        log_assert(golden_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
                    }

                    if (args.opt_level <= 2) {
                        if (!args.determinism_tests and !args.concurrent) {
                            log_assert(golden_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI execute successfuly on golden backend");
                        }
                    }

                    for (const auto &output_name : output_names) {
                        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
                        q_desc.hstack_factor = args.host_hstack_factor;
                        q_desc.vstack_factor = args.host_vstack_factor;
                        q_desc.stack_row_major = args.host_stack_row_major;
                        std::vector<float> expected_flat;
                        verif::get_and_pop_flat_tensors(*golden_backend, output_name, expected_flat, ram_ptr, args.io_timeout, args.host_hstack_factor, args.host_vstack_factor, args.host_stack_row_major);
                        auto& device_output = outputs_for_golden[output_name + "_loop_" + std::to_string(n)];
                        log_assert(expected_flat.size() == device_output.size(), "expected amd observed size mismatch");
                        pass &= compare_tensors(expected_flat, device_output, verif::comparison::get_config(output_name, config.comparison_configs), q_desc);
                    }
                } 
            }
        }


        else {
            run_sequential_test(args, init_io_names, target_backend, golden_backend, workload, config, target_config, golden_config, inputs_for_golden, outputs_for_golden, program_parameters, pass);
        }
    }
    
    // ----------------------------------------------------------------
    // Backend teardown
    // ----------------------------------------------------------------
    if (target_config.type != tt::DEVICE::Invalid) {
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
