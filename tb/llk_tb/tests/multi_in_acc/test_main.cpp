// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "llk_tensor_ops.h"
#include "llk_types.h"
#include "test.h"
#include "tests_lib.h"

using namespace std;
using namespace llk;
using namespace tests;
using namespace tests::test_config_api;

//! Generate expected tensors and input tensors from test_config
void tests::multi_in_acc::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor (&input0)[4], Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << ": configured with seed=" << test_config.seed << endl;
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);
    Tensor t_zero = Tensor(
        test_config.tensor_configs["input0"].dims,
        test_config.tensor_configs["input0"].data_format);
    t_zero.populate_with_constant_data(0.0);

    for (uint n = 0; n < 4; n++) {
        string curr_input = "input" + to_string(n);
        int input_seed = rand();

        input0[n] = Tensor(
            test_config.tensor_configs[curr_input].dims,
            curr_input,
            test_config.tensor_configs[curr_input].data_format);

        //input0[n].populate_with_uniform_distribution_data(
        //    stof(test_config.tensor_configs[curr_input].stimulus_config["min"]),
        //    stof(test_config.tensor_configs[curr_input].stimulus_config["max"]),
        //    input_seed);
        input0[n].populate_with_constant_data((float(n+1)));
        input0[n].dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);
    }

    // Expected Generation
    string bcast_type = "none";
    //if (test_config.extra_config.find("math-op") == test_config.extra_config.end()) {
    //    throw std::runtime_error("Cannot find math-op defined in extra config");
    //}
    if (test_config.extra_config.find("bcast-type") != test_config.extra_config.end()) {
        bcast_type = test_config.extra_config["bcast-type"];
    }
    for (uint n = 0; n < 4; n++) {
       tensor_ops::binary_broadcast(
           tensor_ops::get_binary_op_from_string("add"),
           expected_output0,
           expected_output0.dims,
           n==0 ? t_zero : expected_output0,
           tensor_ops::get_broadcast_op_from_string("none"),
           input0[n],
           tensor_ops::get_broadcast_op_from_string("none"));
    }

    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::multi_in_acc::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
    string num_tiles = to_string(config.tensor_configs["input0"].dims.num_tiles());

    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format);
    auto input1_unpacker_formats = get_unpacker_formats(config.tensor_configs["input1"].data_format);
    auto input2_unpacker_formats = get_unpacker_formats(config.tensor_configs["input2"].data_format);
    auto input3_unpacker_formats = get_unpacker_formats(config.tensor_configs["input3"].data_format);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
    string input1_data_src_format = to_str(input1_unpacker_formats.first);
    string input1_data_dst_format = to_str(input1_unpacker_formats.second);
    string input2_data_src_format = to_str(input2_unpacker_formats.first);
    string input2_data_dst_format = to_str(input2_unpacker_formats.second);
    string input3_data_src_format = to_str(input3_unpacker_formats.first);
    string input3_data_dst_format = to_str(input3_unpacker_formats.second);
    string output0_data_src_format = to_str(output0_packer_formats.first);
    string output0_data_dst_format = to_str(output0_packer_formats.second);

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", num_tiles},
        {"__dst_tile_cols__", "1"},
        {"__block_tile_dim__", "1"},
        {"__block_cnt__", "1"},
        {"__arg_loop_count__", "1"},
    };

    string num_inputs = config.extra_config["num-inputs"];

    set_sfpu_params_from_extra_config(common_kernel_params, config.extra_config);
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src0_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src1_format__", input1_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src2_format__", input2_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst2_format__", input2_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src3_format__", input3_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst3_format__", input3_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__num_inputs__", num_inputs);
    update_kernel_parameters(config, coord, KernelType::MATH, "__num_inputs__", num_inputs);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::multi_in_acc::generate_test_configs(
    TestArgs args, string test_name, YAML::Node &test_descriptor) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario
    map<string, TestConfig> test_configs;
    // Build a default test_config from yaml specification. + provide user specified kernel_parameter mapping
    TestConfig config(args, test_name, test_descriptor);
    update_kernel_parameter_mapping(config);

    // Generate test configs or use directed from yaml
    if (not args.regression_mode) {
        // Directed test specified in yaml is run
        test_configs.insert({"directed_yaml", config});
    } else {
        ///////////////////////////////////////////////
        // Example of a Full Sweep (Coverage sweeps) //
        ///////////////////////////////////////////////
        vector<TensorDims> dims = {
            TensorDims(1, 1, 32, 32),
            TensorDims(1, 2, 32, 32),
            TensorDims(1, 16, 32, 32),
            TensorDims(1, 1, 128, 32),
            TensorDims(1, 1, 32, 128),
            TensorDims(1, 1, 128, 128),
        };
        vector<DataFormat> data_format_vector = {DataFormat::Bfp8, DataFormat::Float16};
        vector<string> operation_vector = {"gelu", "exponential", "log", "sqrt", "reciprocal", "tanh", "datacopy"};
        int id = 0;
        for (auto &dim : dims) {
            for (auto &data_format : data_format_vector) {
                for (auto &operation : operation_vector) {
                    TestConfig new_config = config;
                    new_config.seed = rand();
                    update_tensor_config(new_config, "input0", dim, data_format);
                    update_tensor_config(new_config, "input1", dim, data_format);
                    update_tensor_config(new_config, "input2", dim, data_format);
                    update_tensor_config(new_config, "input3", dim, data_format);
                    update_tensor_config(new_config, "output0", dim, data_format);
                    if ((operation.compare("log") == 0) or (operation.compare("sqrt") == 0)) {
                        update_stimulus_config_in_tensor_config(new_config, "input0", "min", "0");
                    } else if (operation.compare("reciprocal") == 0) {
                        update_stimulus_config_in_tensor_config(new_config, "input0", "min", "0.5");
                    }
                    string base_kernel_name = "ctest_math";
                    string new_full_kernel_name = base_kernel_name + "_" + operation;
                    update_extra_config(new_config, "math-op", operation);
                    update_kernel_used(new_config, coord, KernelType::MATH, base_kernel_name, new_full_kernel_name);
                    update_kernel_parameter_mapping(new_config);

                    string config_summary_string = generate_config_summary_string(new_config);
                    cout << "Config: " << config_summary_string << endl;
                    if (test_configs.find(config_summary_string) == test_configs.end()) {
                        new_config.id = id;
                        id++;
                    }
                    test_configs.insert({"id_" + to_string(new_config.id) + " : " + config_summary_string, new_config});
                }
            }
        }
    }
    return test_configs;
}

//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::multi_in_acc::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "multi_in_acc";
    Device device;
    string test_descriptor_file_path = args.test_descriptor;
    YAML::Node test_descriptor = YAML::LoadFile(test_descriptor_file_path);

    xy_pair target_coord(0, 0);  // TODO: Multi-core support needed for writing and reading tensors....

    // Generate test configs to be run
    map<string, TestConfig> test_configs = generate_test_configs(args, test_name, test_descriptor);
    map<string, bool> test_results;
    bool result = true;
    bool to_stdout = false;
    for (auto const &it : test_configs) {
        // Run test configs in a loop or in separate threads if needed
        string config_string = it.first;
        TestConfig test_config = it.second;
        std::string output_yaml_name = test_config_api::get_uniquified_output_folder(test_config) + "test.yaml";
        std::cout << "Dumping config yaml: " << output_yaml_name << std::endl;
        dump_config_as_yaml(test_config, output_yaml_name);
        cout << " -- RUNNING LLK_TEST::" << test_name << "::" << config_string << endl;
        // Tensor allocation and generation of stimilus from test_config
        Tensor input0[4];
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, input0, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);
        bool config_result = false;
        // Prepare Test for test_config
        try {
            auto test_data_offset = test_descriptor["overlay-config"]["test_data_offset"]
                                        .as<int>();  // TODO: Replace this when relay streams no needed
            std::map<std::string, uint32_t> tensor_address_map = {
                {output0.name, test_address_map::INPUT_DATA_BASE + 4 * (2 * test_data_offset)}};

            for (uint n = 0; n < 4; n++) {
                tensor_address_map[input0[n].name] = test_address_map::INPUT_DATA_BASE + n * (2 * test_data_offset);
            }
            set_tensor_addresses(test_config, tensor_address_map);
            prepare_test(device, test_config);

            // Write Inputs
            for (uint n = 0; n < 4; n++) {
                write_tensor(device, input0[n], test_config);
            }
            device.run();
            device.wait_completion();  // Wait for all 5 RISCs to complete
            // Read Output
            // device.read_tensor(output0, target_coord, test_address_map::OUTPUT_DATA_BASE + test_data_offset);
            read_tensor(device, output0, test_config);
            output0.dump(to_stdout, test_config.test_args.dump_tensors, "output0_" + test_config.test_name);
            // Check results
            config_result = check_result(
                test_config.test_args,
                test_config.test_name,
                device,
                output0,
                expected_output0,
                test_config.comparison_config);
        } catch (...) {
        }
        device.stop();
        test_results.insert({config_string, config_result});
    }
    return test_results;
}
