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
void tests::matmul::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &input1, Tensor &expected_output0) {
    bool to_stdout = false;
    bool srca_transpose_xy_en = false;

    if(test_config.extra_config.find("srca-transpose") != test_config.extra_config.end()){
        srca_transpose_xy_en = (test_config.extra_config["srca-transpose"] == "XY");
    }
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << ", srca_transpose_xy_en=" << (uint32_t)srca_transpose_xy_en << endl;
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
    tensor_ops::matmul(expected_output0, input0, input1, srca_transpose_xy_en);
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::matmul::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    bool fp32_dest_acc_en = false;
    if(config.extra_config.find("dest-acc") != config.extra_config.end()){
        fp32_dest_acc_en = (config.extra_config["dest-acc"] == "fp32_acc");
    }

    if(fp32_dest_acc_en){
        //std::cout << "fp32_dest_acc_en mode set with device: " << config.device << std::endl;
        assert(config.device != "grayskull");
        assert(config.device != "");
    }

    bool srca_transpose_xy_en = false;

    if(config.extra_config.find("srca-transpose") != config.extra_config.end()){
        srca_transpose_xy_en = (config.extra_config["srca-transpose"] == "XY");
    }

    const string transpose_xy_srca = srca_transpose_xy_en ? "1" : "0";

    // Pull information from config.
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format, fp32_dest_acc_en);
    auto input1_unpacker_formats = get_unpacker_formats(config.tensor_configs["input1"].data_format, fp32_dest_acc_en);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format, fp32_dest_acc_en);

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

    int loop_count = config.tensor_configs["input0"].dims.z;
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = stoi(config.extra_config["loop-count"]);
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
        {"__arg_loop_count__", to_string(loop_count)},
    };
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src0_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src1_format__", input1_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__transpose_xy_srca__", transpose_xy_srca);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::MATH, "__src0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, "__src1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, "__dst0_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, "__dst1_format__", input1_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::matmul::generate_test_configs(
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
        vector<string> buffering_types = {"full", "db"};
        vector<DataFormat> data_formats = {
            DataFormat::Bfp4,
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp4_b,
            DataFormat::Bfp8_b,
            DataFormat::Float16_b};
        vector<string> operations = {"LF", "HF2", "HF3", "HF4"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            string buffering_type = buffering_types[rand() % buffering_types.size()];
            string operation = operations[rand() % operations.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];

            // Randomize configuration:
            int max_input_l1_buffer_size = llk::test_address_map::INPUT_DATA_OFFSET;
            int max_tiles_in_dst = 16;
            if (buffering_type.compare("db") == 0) {
                max_tiles_in_dst = 8;
            }
            int max_output_tiles = 16;
            int max_tiles = 128;
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;

            int num_output0_x_tiles;
            int num_output0_y_tiles;
            int num_input_blocks;
            int num_inner_tiles;
            int num_inner_tiles_per_block;
            int num_tiles_input0_buf;
            int num_tiles_input1_buf;
            bool not_even_blocks = true;
            bool fits_l1 = true;
            do {
                do {
                    num_output0_x_tiles = rand() % max_output_tiles + 1;
                    num_output0_y_tiles = max_output_tiles / num_output0_x_tiles;
                } while (max_output_tiles % (num_output0_x_tiles * num_output0_y_tiles) != 0);

                int max_tiles_inner_dim = std::min(max_tiles / num_output0_x_tiles, max_tiles / num_output0_y_tiles);
                num_inner_tiles = rand() % max_tiles_inner_dim + 1;
                num_input_blocks = rand() % num_inner_tiles + 1;
                not_even_blocks = num_inner_tiles % num_input_blocks;

                num_inner_tiles_per_block = num_inner_tiles / num_input_blocks;
                num_tiles_input0_buf = 2 * num_output0_y_tiles * num_inner_tiles_per_block;
                num_tiles_input1_buf = 2 * num_output0_x_tiles * num_inner_tiles_per_block;
                fits_l1 = ((tile_size * num_tiles_input0_buf) < max_input_l1_buffer_size) &&
                          ((tile_size * num_tiles_input1_buf) < max_input_l1_buffer_size);

            } while (not_even_blocks || !fits_l1);
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

            string base_math_kernel_name = config.core_configs[coord].kernel_names[KernelType::MATH]["base"];
            string base_pack_kernel_name = config.core_configs[coord].kernel_names[KernelType::PACK]["base"];
            if (buffering_type.compare("db") == 0) {
                base_pack_kernel_name = "ctest_db_pack_matmul";
                base_math_kernel_name = "ctest_db_math_matmul";
            }

            string base_unpack_kernel_name = config.core_configs[coord].kernel_names[KernelType::UNPACK]["base"];
            string new_full_unpack_kernel_name = base_unpack_kernel_name;

            string new_full_math_kernel_name = base_math_kernel_name + "_" + operation;
            string new_full_pack_kernel_name = base_pack_kernel_name;

            update_extra_config(new_config, "math-op", operation);
            update_extra_config(new_config, "block-count", to_string(num_input_blocks));
            update_extra_config(new_config, "loop-count", to_string(1));
            update_extra_config(new_config, "dst-sync-type", buffering_type);
            update_extra_config(new_config, "num-tiles-input0-buf", to_string(num_tiles_input0_buf));
            update_extra_config(new_config, "num-tiles-input1-buf", to_string(num_tiles_input1_buf));
            update_kernel_used(
                new_config, coord, KernelType::UNPACK, base_unpack_kernel_name, new_full_unpack_kernel_name);
            update_kernel_used(new_config, coord, KernelType::MATH, base_math_kernel_name, new_full_math_kernel_name);
            update_kernel_used(new_config, coord, KernelType::PACK, base_pack_kernel_name, new_full_pack_kernel_name);
            update_kernel_parameter_mapping(new_config);

            if ((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp8_b)) {
                update_comparison_config(new_config, ComparisonConfig({.minimum_pcc = 0.8f}));
            }

            if (data_format == DataFormat::Bfp4 || data_format == DataFormat::Bfp4_b){
                update_comparison_config(new_config, ComparisonConfig({.minimum_pcc = 0.8f, .skip_match_ratio_check = true}));
            }   
            
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
std::map<std::string, bool> tests::matmul::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "matmul";
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
