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
#include "test_config.h"
#include "tests_lib.h"

using namespace std;
using namespace llk;
using namespace tests;
using namespace tests::test_config_api;

//! Generate expected tensors and input tensors from test_config
void tests::hlkc_test::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &input1, Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    input0 =
        Tensor(test_config.tensor_configs["input0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    input1 =
        Tensor(test_config.tensor_configs["input1"].dims, "input1", test_config.tensor_configs["input1"].data_format);
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);
    int input1_seed = rand();
    input1.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input1"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input1"].stimulus_config["max"]),
        input1_seed);
    input1.dump(to_stdout, test_config.test_args.dump_tensors, "input1_" + test_config.test_name);

    // Expected Generation
    tensor_ops::matmul(expected_output0, input0, input1);
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::hlkc_test::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format);
    auto input1_unpacker_formats = get_unpacker_formats(config.tensor_configs["input1"].data_format);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
    string input1_data_src_format = to_str(input1_unpacker_formats.first);
    string input1_data_dst_format = to_str(input1_unpacker_formats.second);
    string output0_data_src_format = to_str(output0_packer_formats.first);
    string output0_data_dst_format = to_str(output0_packer_formats.second);

    int num_blocks = 1;
    if (config.extra_config.find("block-count") != config.extra_config.end()) {
        num_blocks = stoi(config.extra_config["block-count"]);
    }
    string num_tiles_inner = to_string(config.tensor_configs["input0"].dims.rt / num_blocks);
    string num_tiles_x = to_string(config.tensor_configs["output0"].dims.rt);
    string num_tiles_y = to_string(config.tensor_configs["output0"].dims.ct);
    string input_data_format = to_str(config.tensor_configs["input0"].data_format);
    string output_data_format = to_str(config.tensor_configs["output0"].data_format);

    if (config.extra_config.find("num-tiles-input0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "input0", atol(config.extra_config["num-tiles-input0-buf"].c_str()));
    }
    if (config.extra_config.find("num-tiles-input1-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "input1", atol(config.extra_config["num-tiles-input1-buf"].c_str()));
    }

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", num_tiles_y},
        {"__dst_tile_cols__", num_tiles_x},
        {"__block_tile_dim__", num_tiles_inner},
        {"__block_cnt__", to_string(num_blocks)},
        {"__arg_loop_count__", "1"},
    };

    string relu_type = "none";
    if (config.extra_config.find("relu-type") != config.extra_config.end()) {
        relu_type = config.extra_config["relu-type"];
    }

    string relu_mode = std::to_string((uint32_t)llk::tensor_ops::get_relu_op_from_string(relu_type));

    string relu_threshold = "0";
    if (config.extra_config.find("relu-threshold") != config.extra_config.end()) {
        relu_threshold = config.extra_config["relu-threshold"];
    }

    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src0_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src1_format__", input1_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_mode);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_threshold);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::hlkc_test::generate_test_configs(
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
    }
    return test_configs;
}

//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::hlkc_test::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "hlkc_test";
    Device device;
    string test_descriptor_file_path = args.test_descriptor;
    YAML::Node test_descriptor = YAML::LoadFile(test_descriptor_file_path);

    xy_pair target_coord(0, 0);  // TODO: Multi-core support needed for writing and reading tensors....

    // Generate test configs to be run
    map<string, TestConfig> test_configs = generate_test_configs(args, test_name, test_descriptor);
    map<string, bool> test_results;
    bool result = true;
    for (auto const &it : test_configs) {
        // Run test configs in a loop or in separate threads if needed
        string config_string = it.first;
        TestConfig test_config = it.second;
        std::string output_yaml_name = test_config_api::get_uniquified_output_folder(test_config) + "test.yaml";
        std::cout << "Dumping config yaml: " << output_yaml_name << std::endl;
        dump_config_as_yaml(test_config, output_yaml_name);
        cout << " -- RUNNING LLK_TEST::" << test_name << "::" << config_string << endl;
        // Tensor allocation and generation of stimilus from test_config
        Tensor input0;
        Tensor input1;
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, input0, input1, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {
            std::map<std::string, uint32_t> tensor_address_map = {
                {input0.name, test_address_map::INPUT_A_DATA_BASE},
                {input1.name, test_address_map::INPUT_B_DATA_BASE},
                {output0.name, test_address_map::OUTPUT_DATA_BASE}};
            set_tensor_addresses(test_config, tensor_address_map);
            prepare_test(device, test_config);

            // Writing multi-block for matmul needs to split data differently
            int num_blocks = 1;
            if (test_config.extra_config.find("block-count") != test_config.extra_config.end()) {
                num_blocks = stoi(test_config.extra_config["block-count"]);
            }
            // Write Inputs
            write_tensor(device, input0, test_config, llk::xy_pair(num_blocks, 1));
            write_tensor(device, input1, test_config, llk::xy_pair(1, num_blocks));
            device.run();
            device.wait_completion(300000);  // Wait for all 5 RISCs to complete
            // Read Output
            read_tensor(device, output0, test_config);
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
