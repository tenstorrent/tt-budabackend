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
void tests::eltwise_nary::generate_stimulus_and_expected_tensors(
    TestConfig test_config, std::vector<Tensor> &inputs, Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    int input_idx = 0;
    for (Tensor &input: inputs) {
        string input_name = "input" + to_string(input_idx);
        input = Tensor(test_config.tensor_configs[input_name].dims, input_name, test_config.tensor_configs[input_name].data_format);

        // Stimulus Generation.  All configuration comes as a string
        int input_seed = rand();
        input.populate_with_uniform_distribution_data(
            stof(test_config.tensor_configs[input_name].stimulus_config["min"]),
            stof(test_config.tensor_configs[input_name].stimulus_config["max"]),
            input_seed);

        input.dump(to_stdout, test_config.test_args.dump_tensors, input_name + "_" + test_config.test_name);
        input_idx++;
    }

    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);
    expected_output0.populate_with_constant_data(0.0);
    // Expected Generation
    string bcast_type = "none";
    if (test_config.extra_config.find("math-op") == test_config.extra_config.end()) {
        throw std::runtime_error("Cannot find math-op defined in extra config");
    }
    if (test_config.extra_config.find("bcast-type") != test_config.extra_config.end()) {
        bcast_type = test_config.extra_config["bcast-type"];
    }

    for (Tensor input: inputs) {
        tensor_ops::binary_broadcast(
            tensor_ops::get_binary_op_from_string(test_config.extra_config["math-op"]),
            expected_output0,
            expected_output0.dims,
            expected_output0,
            tensor_ops::get_broadcast_op_from_string("none"),
            input,
            tensor_ops::get_broadcast_op_from_string(bcast_type));
    }
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::eltwise_nary::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
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

    string loop_count = "1";
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = config.extra_config["loop-count"];
    }
    string block_count = "1";
    if (config.extra_config.find("block-count") != config.extra_config.end()) {
        block_count = config.extra_config["block-count"];
    }
    string block_tile_dim = to_string(config.tensor_configs["input0"].dims.num_tiles());
    if (config.extra_config.find("block-tile-dim") != config.extra_config.end()) {
        block_tile_dim = config.extra_config["block-tile-dim"];
    }
    if (config.extra_config.find("num-tiles-output0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "output0", atol(config.extra_config["num-tiles-output0-buf"].c_str()));
    }

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", "1"},
        {"__dst_tile_cols__", block_tile_dim},
        {"__block_cnt__", block_count},
        {"__arg_loop_count__", loop_count},
    };
    string num_inputs = config.extra_config["num-inputs"];
    for (int input = 0; input < stol(num_inputs); input++) {
        string key = "num-tiles-input" + to_string(input) + "-buf";
        if (config.extra_config.find(key) != config.extra_config.end()) {
            update_stream_buffer_size_for_tensor(
                config, "input" + to_string(input), atol(config.extra_config[key].c_str()));
        }
    }
    update_overlay_config(
        config.overlay_config,
        "num_inputs",
        num_inputs);

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
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::MATH, "__num_inputs__", num_inputs);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::eltwise_nary::generate_test_configs(
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
        // FIXME: For now only do non broadcast
        // vector<string> bcast_types = {"none", "broadcast_row", "broadcast_col", "broadcast_scalar"};
        vector<string> bcast_types = {"none"};
        vector<DataFormat> data_formats = {
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp8_b,
            DataFormat::Float16_b,
            DataFormat::Float32};
        vector<string> operations = {"add", "sub"};
        vector<string> dst_sync_types = {"full"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            int MAX_INPUTS = 4;
            int num_inputs = (rand() % MAX_INPUTS) + 1; // num inputs can be any random number between 1-4
            string dst_sync_type = dst_sync_types[rand() % dst_sync_types.size()];
            string operation = operations[rand() % operations.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];
            string bcast_type = bcast_types[rand() % bcast_types.size()];

            // Randomize configuration:
            // l1 --> max buffer size --> 128kB
            // loops -->
            int max_input_l1_buffer_size = llk::test_address_map::INPUT_DATA_OFFSET;
            int max_output_l1_buffer_size = llk::test_address_map::OUTPUT_DATA_SIZE;
            int max_tiles = 128;  // Can be more. but don't want to blow up size for no reason.
            int max_tiles_in_dst = 16;
            // This is because the stream buffer must fit within l1.  The stream buffer has to double buffer for inputs
            // For eltwise_binary, inputs buffer has to be 128kB or less, and input == output shape
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;
            while (tile_size * max_tiles_in_dst * 2 >= max_input_l1_buffer_size) {
                max_tiles_in_dst--;
            }
            // block_tile_dim -- How many tiles per dst we want
            int num_tiles_in_block = rand() % max_tiles_in_dst + 1;

            int max_loops = max_tiles;
            int max_loop_count = max_tiles / num_tiles_in_block;

            // Tensor size must be a multiple of block_count/loop_count/num_tiles_in_block
            int loop_count;
            int block_count;
            loop_count = rand() % max_loop_count + 1;
            block_count = rand() % (max_loop_count / loop_count) + 1;

            int total_tiles = num_tiles_in_block * block_count * loop_count;
            int num_input0_x_tiles;
            int num_input0_y_tiles;
            do {
                num_input0_x_tiles = rand() % total_tiles + 1;
                num_input0_y_tiles = total_tiles / num_input0_x_tiles;
            } while (total_tiles % (num_input0_x_tiles * num_input0_y_tiles) != 0);
            int input0_x = num_input0_x_tiles * 32;
            int input0_y = num_input0_y_tiles * 32;

            // Update Configuration
            TestConfig new_config = config;
            new_config.seed = rand();
            TensorDims dim(1, 1, input0_y, input0_x);
            for (int input = 0; input < MAX_INPUTS; input++) {
                if (input < num_inputs) {
                    update_tensor_config(new_config, "input" + to_string(input), dim, data_format);
                } else {
                    new_config.tensor_configs.erase("input" + to_string(input));
                }
            }
            update_tensor_config(new_config, "output0", dim, data_format);

            string base_unpack_kernel_name = config.core_configs[coord].kernel_names[KernelType::UNPACK]["base"];
            string new_full_unpack_kernel_name = base_unpack_kernel_name;

            string base_math_kernel_name = config.core_configs[coord].kernel_names[KernelType::MATH]["base"];
            string base_pack_kernel_name = config.core_configs[coord].kernel_names[KernelType::PACK]["base"];
            if (dst_sync_type.compare("stream") == 0) {
                base_pack_kernel_name = "ctest_stream_pack";
                base_math_kernel_name = "ctest_stream_math_eltwise_nary";
            }
            string new_full_math_kernel_name = base_math_kernel_name + "_" + operation;
            if (bcast_type.compare("none") != 0) {
                new_full_math_kernel_name = base_math_kernel_name + "_" + bcast_type + "_" + operation;
                new_full_unpack_kernel_name = new_full_unpack_kernel_name + "_" + bcast_type;
            }
            int num_tiles_output0_buf = 2 * num_tiles_in_block;
            if (dst_sync_type.compare("stream") == 0) {
                num_tiles_output0_buf = 1 + rand() % 8;
            }

            update_extra_config(new_config, "math-op", operation);
            update_extra_config(new_config, "num-inputs", to_string(num_inputs));
            update_extra_config(new_config, "bcast-type", bcast_type);
            update_extra_config(new_config, "dst-sync-type", dst_sync_type);
            update_extra_config(new_config, "block-count", to_string(block_count));
            update_extra_config(new_config, "block-tile-dim", to_string(num_tiles_in_block));
            update_extra_config(new_config, "num-tiles-output0-buf", to_string(num_tiles_output0_buf));

            for (int input = 0; input < MAX_INPUTS; input++) {
                if (input < num_inputs) {
                    update_extra_config(new_config, "num-tiles-input" + to_string(input) +"-buf", to_string( 2 * num_tiles_in_block));
                } else {
                    update_extra_config(new_config, "num-tiles-input" + to_string(input) +"-buf", "0");
                }
            }
            update_extra_config(new_config, "loop-count", to_string(loop_count));

            if (data_format == DataFormat::Bfp4 || data_format == DataFormat::Bfp4_b){
                update_comparison_config(new_config, ComparisonConfig({.skip_match_ratio_check = true}));
            } else if (data_format == DataFormat::Bfp2 || data_format == DataFormat::Bfp2_b){
                update_comparison_config(config, ComparisonConfig({.minimum_pcc=0.8f, .skip_match_ratio_check = true}));
            }
            update_kernel_used(
                new_config, coord, KernelType::UNPACK, base_unpack_kernel_name, new_full_unpack_kernel_name);
            update_kernel_used(new_config, coord, KernelType::MATH, base_math_kernel_name, new_full_math_kernel_name);
            update_kernel_used(new_config, coord, KernelType::PACK, base_pack_kernel_name, base_pack_kernel_name);
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
    return test_configs;
}

//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::eltwise_nary::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "eltwise_nary";
    Device device;
    string test_descriptor_file_path = args.test_descriptor;
    YAML::Node test_descriptor = YAML::LoadFile(test_descriptor_file_path);

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
        std::vector<Tensor> inputs(stol(test_config.extra_config["num-inputs"]));
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, inputs, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {

            std::map<std::string, uint32_t> tensor_address_map = {
                {output0.name, test_address_map::INPUT_A_DATA_BASE + test_address_map::INPUT_DATA_OFFSET * inputs.size()}};
            int input_address = test_address_map::INPUT_A_DATA_BASE;
            for (Tensor &input: inputs) {
                tensor_address_map[input.name] = input_address;
                input_address = input_address + test_address_map::INPUT_DATA_OFFSET;
            }
            set_tensor_addresses(test_config, tensor_address_map);
            prepare_test(device, test_config);

            // Write Inputs
            for (Tensor &input: inputs) {
                write_tensor(device, input, test_config);
            }
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
