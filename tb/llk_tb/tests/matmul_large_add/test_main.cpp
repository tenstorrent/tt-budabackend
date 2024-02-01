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
void tests::matmul_large_add::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &input1, Tensor &expected_output0, Tensor &intermediate1) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    input0 =
        Tensor(test_config.tensor_configs["input0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    input1 =
        Tensor(test_config.tensor_configs["input1"].dims, "input1", test_config.tensor_configs["input1"].data_format);
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);
    intermediate1 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);
    intermediate1.populate_with_constant_data(0.0);
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
void tests::matmul_large_add::update_kernel_parameter_mapping(TestConfig &config) {
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
    if (config.extra_config.find("block-tile-dim") != config.extra_config.end()) {
        num_tiles_inner = stoi(config.extra_config["block-tile-dim"]);
    }
    string num_tiles_x = to_string(config.tensor_configs["output0"].dims.rt);
    string num_tiles_y = to_string(config.tensor_configs["output0"].dims.ct);

    int arg_loop_count = 1;

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", num_tiles_y},
        {"__dst_tile_cols__", num_tiles_x},
        {"__block_tile_dim__", num_tiles_inner},
        {"__block_cnt__", to_string(num_blocks)},
        {"__arg_loop_count__", to_string(arg_loop_count)},
    };

    int num_tiles = stoi(num_tiles_x) * stoi(num_tiles_y);
    update_overlay_config(config.overlay_config, "num_intermediate0_data_buf_msgs", to_string(num_blocks * num_tiles));

    update_stream_buffer_size_for_tensor(config, "intermediate0", num_tiles);

    update_overlay_config(
        config.overlay_config,
        "num_intermediate1_data_buf_msgs",
        to_string((1 + arg_loop_count) * num_tiles));  // 1 is for the initial zero tile load

    update_stream_buffer_size_for_tensor(config, "intermediate1", num_tiles);

    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src0_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src1_format__", input1_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__intermediate0_src_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__intermediate0_dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__intermediate1_src_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__intermediate1_dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__intermediate0_src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__intermediate0_dst_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__intermediate1_src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__intermediate1_dst_format__", output0_data_src_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::matmul_large_add::generate_test_configs(
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
        vector<DataFormat> data_formats = {
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp8_b,
            DataFormat::Float16,
            DataFormat::Float16_b,
            DataFormat::Float32};
        vector<string> operations = {"LF", "HF2", "HF3", "HF4"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            string operation = operations[rand() % operations.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];

            // Randomize configuration:
            int max_tiles_in_dst = 8;
            int max_tiles = 16;
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;
            while (tile_size * max_tiles >= 65536) {
                max_tiles -= 1;
            }
            int max_loops = 1;  // max_tiles / max_tiles_in_dst;
            // loop count and block count set to 1 for now
            int loop_count;
            do {
                loop_count = rand() % max_loops + 1;
            } while (max_loops % loop_count);
            int max_block_count = max_loops / loop_count;
            int block_count = rand() % max_block_count + 1;

            int num_output0_x_tiles;
            int num_output0_y_tiles;
            do {
                num_output0_x_tiles =
                    rand() % (max_tiles / 2) + 1;  // div max_tiles by 2 to make sure num_input_blocks will be > 2
                num_output0_y_tiles = (max_tiles / 2) / num_output0_x_tiles;
            } while (max_tiles % (num_output0_x_tiles * num_output0_y_tiles) != 0);

            int max_tiles_inner_dim =
                std::min(max_tiles / num_output0_x_tiles, max_tiles / num_output0_y_tiles);  // min 2
            int num_inner_tiles = rand() % (max_tiles_inner_dim - 1) + 2;
            int num_input_blocks;
            do {
                num_input_blocks = rand() % num_inner_tiles + 1;
            } while ((num_inner_tiles % num_input_blocks) ||
                     (num_input_blocks < 2));  // if num_input_blocks==1 then num_intermediate0_data_buf_msgs will be 0
                                               // which hangs the overlay
            int num_tiles_input0 = num_inner_tiles * num_output0_y_tiles;
            int num_tiles_input1 = num_inner_tiles * num_output0_x_tiles;

            // Update Configuration
            TestConfig new_config = config;
            new_config.seed = rand();
            TensorDims input0_dim(1, 1, num_output0_y_tiles * 32, num_inner_tiles * 32);
            TensorDims input1_dim(1, 1, num_inner_tiles * 32, num_output0_x_tiles * 32);
            TensorDims output0_dim(1, 1, num_output0_y_tiles * 32, num_output0_x_tiles * 32);
            update_tensor_config(new_config, "input0", input0_dim, data_format);
            update_tensor_config(new_config, "input1", input1_dim, data_format);
            update_tensor_config(new_config, "output0", output0_dim, data_format);

            string base_unpack_kernel_name = config.core_configs[coord].kernel_names[KernelType::UNPACK]["base"];
            string new_full_unpack_kernel_name = base_unpack_kernel_name;

            string base_math_kernel_name = config.core_configs[coord].kernel_names[KernelType::MATH]["base"];
            string new_full_math_kernel_name = base_math_kernel_name + "_" + operation;

            update_extra_config(new_config, "math-op", operation);
            update_extra_config(new_config, "block-count", to_string(num_input_blocks));
            update_extra_config(new_config, "loop-count", to_string(loop_count));
            update_kernel_used(
                new_config, coord, KernelType::UNPACK, base_unpack_kernel_name, new_full_unpack_kernel_name);
            update_kernel_used(new_config, coord, KernelType::MATH, base_math_kernel_name, new_full_math_kernel_name);
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
std::map<std::string, bool> tests::matmul_large_add::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "matmul_large_add";
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
        Tensor intermediate1;

        generate_stimulus_and_expected_tensors(test_config, input0, input1, expected_output0, intermediate1);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {
            auto test_data_offset =
                test_descriptor["overlay-config"]["test_data_offset"]
                    .as<int>();  // This is needed because we are outputting an intermediate buffer at another location
            std::map<std::string, uint32_t> tensor_address_map = {
                {input0.name, test_address_map::INPUT_A_DATA_BASE},
                {input1.name, test_address_map::INPUT_B_DATA_BASE},
                {output0.name, test_address_map::OUTPUT_DATA_BASE + 3 * test_data_offset}};
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
            device.wait_completion(
                100000,
                {llk::ThreadType::Unpack, llk::ThreadType::Math, llk::ThreadType::Pack});  // Wait for TRISCs to finish
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
