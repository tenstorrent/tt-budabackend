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
void tests::multi_out::generate_stimulus_and_expected_tensors(
    TestConfig test_config,
    Tensor &input0,
    Tensor &expected_output0,
    Tensor &expected_output1,
    Tensor &expected_output2,
    Tensor &expected_output3) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << ": configured with seed=" << test_config.seed << endl;
    input0 =
        Tensor(test_config.tensor_configs["input0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);
    expected_output1 =
        Tensor(test_config.tensor_configs["output1"].dims, test_config.tensor_configs["output1"].data_format);
    expected_output2 =
        Tensor(test_config.tensor_configs["output2"].dims, test_config.tensor_configs["output2"].data_format);
    expected_output3 =
        Tensor(test_config.tensor_configs["output3"].dims, test_config.tensor_configs["output3"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
    // input0.populate_with_debug_data();
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);

    // Split input to num-outputs buffers
    // Add randomization
    llk::Tensor input0_0;
    llk::Tensor input0_1;
    llk::Tensor input0_2;
    llk::Tensor input0_3;
    input0_0 =
        Tensor(test_config.tensor_configs["output0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    input0_1 =
        Tensor(test_config.tensor_configs["output1"].dims, "input1", test_config.tensor_configs["input0"].data_format);
    input0_2 =
        Tensor(test_config.tensor_configs["output2"].dims, "input2", test_config.tensor_configs["input0"].data_format);
    input0_3 =
        Tensor(test_config.tensor_configs["output3"].dims, "input3", test_config.tensor_configs["input0"].data_format);

    // From ctest_params_template/ctest_stream_pack_multi_out_params.h
    uint input2output0_index[] = {0, 2, 4, 5};
    uint input2output1_index[] = {1, 3};
    uint input2output2_index[] = {6, 7, 8, 11, 13, 14, 15};
    uint input2output3_index[] = {9, 10, 12};

    for (auto w = 0; w < input0_0.tile_tensor.size(); w++) {
        for (auto z = 0; z < input0_0.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < input0_0.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < input0_0.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    input0_0.tile_tensor[w][z][ct][rt] = input0.tile_tensor[w][input2output0_index[z]][ct][rt];
                }
            }
        }
    }
    input0_0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_0_" + test_config.test_name);
    for (auto w = 0; w < input0_1.tile_tensor.size(); w++) {
        for (auto z = 0; z < input0_1.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < input0_1.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < input0_1.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    input0_1.tile_tensor[w][z][ct][rt] = input0.tile_tensor[w][input2output1_index[z]][ct][rt];
                }
            }
        }
    }
    input0_1.dump(to_stdout, test_config.test_args.dump_tensors, "input0_1_" + test_config.test_name);

    input0_2.dump(to_stdout, test_config.test_args.dump_tensors, "input0_2_" + test_config.test_name);
    for (auto w = 0; w < input0_2.tile_tensor.size(); w++) {
        for (auto z = 0; z < input0_2.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < input0_2.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < input0_2.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    input0_2.tile_tensor[w][z][ct][rt] = input0.tile_tensor[w][input2output2_index[z]][ct][rt];
                }
            }
        }
    }
    input0_2.dump(to_stdout, test_config.test_args.dump_tensors, "input0_2_" + test_config.test_name);

    input0_3.dump(to_stdout, test_config.test_args.dump_tensors, "input0_3_" + test_config.test_name);
    for (auto w = 0; w < input0_3.tile_tensor.size(); w++) {
        for (auto z = 0; z < input0_3.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < input0_3.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < input0_3.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    input0_3.tile_tensor[w][z][ct][rt] = input0.tile_tensor[w][input2output3_index[z]][ct][rt];
                }
            }
        }
    }
    input0_3.dump(to_stdout, test_config.test_args.dump_tensors, "input0_3_" + test_config.test_name);

    // Expected Generation
    string bcast_type = "none";
    if (test_config.extra_config.find("math-op") == test_config.extra_config.end()) {
        throw std::runtime_error("Cannot find math-op defined in extra config");
    }
    if (test_config.extra_config.find("bcast-type") != test_config.extra_config.end()) {
        bcast_type = test_config.extra_config["bcast-type"];
    }
    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output0,
        expected_output0.dims,
        input0_0,
        tensor_ops::get_broadcast_op_from_string(bcast_type));
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output1,
        expected_output1.dims,
        input0_1,
        tensor_ops::get_broadcast_op_from_string(bcast_type));
    expected_output1.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output1_" + test_config.test_name);
    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output2,
        expected_output2.dims,
        input0_2,
        tensor_ops::get_broadcast_op_from_string(bcast_type));
    expected_output2.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output2_" + test_config.test_name);
    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output3,
        expected_output3.dims,
        input0_3,
        tensor_ops::get_broadcast_op_from_string(bcast_type));
    expected_output3.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output3_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::multi_out::update_kernel_parameter_mapping(
    TestConfig &config, std::vector<std::map<string, int>> output_tile_configs) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
    string num_tiles = to_string(config.tensor_configs["input0"].dims.num_tiles());
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
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

    string num_outputs = config.extra_config["num-outputs"];
    string num_output0_tiles = to_string(config.tensor_configs["output0"].dims.num_tiles());
    string num_output1_tiles = to_string(config.tensor_configs["output1"].dims.num_tiles());
    string num_output2_tiles = to_string(config.tensor_configs["output2"].dims.num_tiles());
    string num_output3_tiles = to_string(config.tensor_configs["output3"].dims.num_tiles());

    set_sfpu_params_from_extra_config(common_kernel_params, config.extra_config);
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src0_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst0_format__", output0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_input_tiles__", num_tiles);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_outputs__", num_outputs);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_output0_tiles__", num_output0_tiles);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_output1_tiles__", num_output1_tiles);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_output2_tiles__", num_output2_tiles);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_output3_tiles__", num_output3_tiles);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
    for (int i = 0; i < output_tile_configs.size(); i++) {
        std::map<string, int> output_tile_config = output_tile_configs[i];
        update_kernel_parameters(
            config,
            coord,
            KernelType::PACK,
            "__tile" + to_string(i) + "_index_within_out_buf__",
            to_string(output_tile_config["index_within_out_buf"]));
        update_kernel_parameters(
            config,
            coord,
            KernelType::PACK,
            "__tile" + to_string(i) + "_out_buf__",
            to_string(output_tile_config["out_buf"]));
    }
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::multi_out::generate_test_configs(
    TestArgs args, string test_name, YAML::Node &test_descriptor) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario
    map<string, TestConfig> test_configs;
    // Build a default test_config from yaml specification. + provide user specified kernel_parameter mapping
    TestConfig config(args, test_name, test_descriptor);

    // Generate test configs or use directed from yaml
    if (not args.regression_mode) {
        // Directed test specified in yaml is run
        std::vector<std::map<string, int>> output_tile_configs = {
            {{"index_within_out_buf", 0}, {"out_buf", 0}},
            {{"index_within_out_buf", 0}, {"out_buf", 1}},
            {{"index_within_out_buf", 1}, {"out_buf", 0}},
            {{"index_within_out_buf", 1}, {"out_buf", 1}},
            {{"index_within_out_buf", 2}, {"out_buf", 0}},
            {{"index_within_out_buf", 3}, {"out_buf", 0}},
            {{"index_within_out_buf", 0}, {"out_buf", 2}},
            {{"index_within_out_buf", 1}, {"out_buf", 2}},
            {{"index_within_out_buf", 2}, {"out_buf", 2}},
            {{"index_within_out_buf", 0}, {"out_buf", 3}},
            {{"index_within_out_buf", 1}, {"out_buf", 3}},
            {{"index_within_out_buf", 3}, {"out_buf", 2}},
            {{"index_within_out_buf", 2}, {"out_buf", 3}},
            {{"index_within_out_buf", 4}, {"out_buf", 2}},
            {{"index_within_out_buf", 5}, {"out_buf", 2}},
            {{"index_within_out_buf", 6}, {"out_buf", 2}},
        };
        update_kernel_parameter_mapping(config, output_tile_configs);
        test_configs.insert({"directed_yaml", config});
    } else {
        std::vector<std::map<string, int>> output_tile_configs = {
            {{"index_within_out_buf", 0}, {"out_buf", 0}},
            {{"index_within_out_buf", 0}, {"out_buf", 1}},
            {{"index_within_out_buf", 1}, {"out_buf", 0}},
            {{"index_within_out_buf", 1}, {"out_buf", 1}},
            {{"index_within_out_buf", 2}, {"out_buf", 0}},
            {{"index_within_out_buf", 3}, {"out_buf", 0}},
            {{"index_within_out_buf", 0}, {"out_buf", 2}},
            {{"index_within_out_buf", 1}, {"out_buf", 2}},
            {{"index_within_out_buf", 2}, {"out_buf", 2}},
            {{"index_within_out_buf", 0}, {"out_buf", 3}},
            {{"index_within_out_buf", 1}, {"out_buf", 3}},
            {{"index_within_out_buf", 3}, {"out_buf", 2}},
            {{"index_within_out_buf", 2}, {"out_buf", 3}},
            {{"index_within_out_buf", 4}, {"out_buf", 2}},
            {{"index_within_out_buf", 5}, {"out_buf", 2}},
            {{"index_within_out_buf", 6}, {"out_buf", 2}},
        };
        vector<DataFormat> data_formats = {
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp8_b,
            DataFormat::Float16,
            DataFormat::Float16_b,
            DataFormat::Float32};

        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            DataFormat data_format = data_formats[rand() % data_formats.size()];

            // Update Configuration
            TestConfig new_config = config;
            new_config.seed = rand();
            update_tensor_config(new_config, "input0", config.tensor_configs["input0"].dims, data_format);
            update_tensor_config(new_config, "output0", config.tensor_configs["output0"].dims, data_format);
            update_tensor_config(new_config, "output1", config.tensor_configs["output1"].dims, data_format);
            update_tensor_config(new_config, "output2", config.tensor_configs["output2"].dims, data_format);
            update_tensor_config(new_config, "output3", config.tensor_configs["output3"].dims, data_format);

            update_kernel_parameter_mapping(new_config, output_tile_configs);

            string config_summary_string = generate_config_summary_string(new_config);
            cout << "Config: " << config_summary_string << endl;
            if (test_configs.find(config_summary_string) == test_configs.end()) {
                new_config.id = id;
                id++;
            }
            test_configs.insert({"id_" + to_string(new_config.id) + " : " + config_summary_string, new_config});
        }
    }
    return test_configs;
}

//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::multi_out::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "multi_out";
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
        Tensor input0;
        Tensor expected_output0;
        Tensor expected_output1;
        Tensor expected_output2;
        Tensor expected_output3;
        generate_stimulus_and_expected_tensors(
            test_config, input0, expected_output0, expected_output1, expected_output2, expected_output3);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);
        Tensor output1(expected_output1.dims, "output1", expected_output1.data_format);
        Tensor output2(expected_output2.dims, "output2", expected_output2.data_format);
        Tensor output3(expected_output3.dims, "output3", expected_output3.data_format);
        bool config_result = false;
        // Prepare Test for test_config
        try {
            auto output_data_offset = test_descriptor["overlay-config"]["output_data_offset"]
                                          .as<int>();  // TODO: Replace this when relay streams no needed
            std::map<std::string, uint32_t> tensor_address_map = {
                {input0.name, test_address_map::INPUT_DATA_BASE},
                {output0.name, test_address_map::OUTPUT_DATA_BASE},
                {output1.name, test_address_map::OUTPUT_DATA_BASE + 1 * output_data_offset},
                {output2.name, test_address_map::OUTPUT_DATA_BASE + 2 * output_data_offset},
                {output3.name, test_address_map::OUTPUT_DATA_BASE + 3 * output_data_offset}};
            set_tensor_addresses(test_config, tensor_address_map);
            prepare_test(device, test_config);

            // Write Inputs
            write_tensor(device, input0, test_config);
            device.run();
            device.wait_completion();  // Wait for all 5 RISCs to complete
            // Read Output
            read_tensor(device, output0, test_config);
            output0.dump(to_stdout, test_config.test_args.dump_tensors, "output0_" + test_config.test_name);
            read_tensor(device, output1, test_config);
            output1.dump(to_stdout, test_config.test_args.dump_tensors, "output1_" + test_config.test_name);
            read_tensor(device, output2, test_config);
            output2.dump(to_stdout, test_config.test_args.dump_tensors, "output2_" + test_config.test_name);
            read_tensor(device, output3, test_config);
            output3.dump(to_stdout, test_config.test_args.dump_tensors, "output3_" + test_config.test_name);
            // Check results
            config_result = check_result(
                test_config.test_args,
                test_config.test_name,
                device,
                output0,
                expected_output0,
                test_config.comparison_config);
            config_result &= check_result(
                test_config.test_args,
                test_config.test_name,
                device,
                output1,
                expected_output1,
                test_config.comparison_config);
            config_result &= check_result(
                test_config.test_args,
                test_config.test_name,
                device,
                output2,
                expected_output2,
                test_config.comparison_config);
            config_result &= check_result(
                test_config.test_args,
                test_config.test_name,
                device,
                output3,
                expected_output3,
                test_config.comparison_config);
        } catch (...) {
        }
        device.stop();
        test_results.insert({config_string, config_result});
    }
    return test_results;
}
