// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "utils/math.h"
#include "llk_tensor_ops.h"
#include "llk_types.h"
#include "test.h"
#include "tests_lib.h"

using namespace std;
using namespace llk;
using namespace tests;
using namespace tests::test_config_api;

//! Generate expected tensors and input tensors from test_config
void tests::untilize::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    input0 = Tensor(test_config.tensor_configs["input0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
    //input0.populate_with_debug_data();
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);

    // Expected Generation
    string bcast_type = "none";
    if (test_config.extra_config.find("math-op") == test_config.extra_config.end()) {
        throw std::runtime_error("Cannot find math-op defined in extra config");
    }
    if (test_config.extra_config.find("bcast-type") != test_config.extra_config.end()) {
        bcast_type = test_config.extra_config["bcast-type"];
    }
    float scale_factor = 1.0f;
    if (test_config.extra_config["math-op"].compare("dropout") == 0) {
        if (test_config.extra_config.find("dropout-percentage") == test_config.extra_config.end()) {
            throw std::runtime_error("Dropout operation needs dropout-percentage defined in extra config");
        } else {
            scale_factor = 1/ (1 - std::stof(test_config.extra_config["dropout-percentage"]));
        }
    }
    
    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output0,
        expected_output0.dims,
        input0,
        tensor_ops::get_broadcast_op_from_string(bcast_type), 
        scale_factor
        );
    expected_output0.untilize_layout();
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::untilize::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
    string input_data_src_format = to_str(config.tensor_configs["input0"].data_format);
    string input_data_dst_format = input_data_src_format == "Float32" ? "Float16" : input_data_src_format;
    string output_data_dst_format = to_str(config.tensor_configs["output0"].data_format);
    string output_data_src_format = output_data_dst_format == "Float32" ? "Float16" : output_data_dst_format;
    string loop_count = "1";
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = config.extra_config["loop-count"];
    }
    string block_count = "1";
    if (config.extra_config.find("block-count") != config.extra_config.end()) {
        block_count = config.extra_config["block-count"];
    }
    string block_tile_dim = to_string(config.tensor_configs["input0"].dims.ct / (stoul(block_count)*stoul(loop_count)));
    if (config.extra_config.find("block-tile-dim") != config.extra_config.end()) {
        block_tile_dim = config.extra_config["block-tile-dim"];
    }

    string num_tiles_x = to_string(config.tensor_configs["input0"].dims.rt);
    string num_tiles_y = block_tile_dim;

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", num_tiles_y},
        {"__dst_tile_cols__", num_tiles_x},
        {"__block_cnt__", block_count},
        {"__arg_loop_count__", loop_count},
    };

    int tile_size = 2048;
    if (config.tensor_configs["output0"].data_format == DataFormat::Float32) {
       tile_size = 4096;
    }
    update_overlay_config(config.overlay_config, "output0_data_buf_tile_size", to_string(tile_size));

    set_sfpu_params_from_extra_config(common_kernel_params, config.extra_config);
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src_format__", input_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst_format__", input_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::untilize::generate_test_configs(
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
        vector<string> dst_sync_types = {"stream"};
        vector<string> bcast_types = {"none"};
        vector<DataFormat> data_formats = {
            DataFormat::Float16, DataFormat::Float16_b, DataFormat::Float32};
        vector<string> operations = {
            "datacopy"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            string dst_sync_type = dst_sync_types[rand() % dst_sync_types.size()];
            ;
            string operation = operations[rand() % operations.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];
            string bcast_type = "none";

            if (operation.compare("datacopy") == 0) {
                bcast_type = bcast_types[rand() % bcast_types.size()];
            }

            // Randomize configuration:
            int max_tiles = 40;
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;
            while (tile_size * max_tiles >= (llk::test_address_map::INPUT_DATA_OFFSET)) {
                max_tiles -= 1;
            }
            // loop_count
            int max_loop_count = 4;
            int loop_count = rand() % max_loop_count + 1;

            int max_block_count = 2;
            int block_count = rand() % max_block_count + 1;

            // block_tile_dim
            int max_tiles_in_block = max_tiles/(loop_count*block_count);
            int num_tiles_in_block = rand() % max_tiles_in_block + 1;

            int num_block_input0_x_tiles;
            int num_block_input0_y_tiles;
            do {
                num_block_input0_x_tiles = rand() % num_tiles_in_block + 1;
                num_block_input0_y_tiles = num_tiles_in_block / num_block_input0_x_tiles;
            } while (num_tiles_in_block % (num_block_input0_x_tiles * num_block_input0_y_tiles) != 0);

            int total_tiles = num_tiles_in_block * block_count * loop_count;
            int input0_x = num_block_input0_x_tiles * 32;
            int input0_y = num_block_input0_y_tiles * 32 * loop_count*block_count;

            // Update Configuration
            TestConfig new_config = config;
            new_config.seed = rand();
            TensorDims dim(1, 1, input0_y, input0_x);
            update_tensor_config(new_config, "input0", dim, data_format);
            update_tensor_config(new_config, "output0", dim, data_format);

            if ((operation.compare("log") == 0) or (operation.compare("sqrt") == 0)) {
                update_stimulus_config_in_tensor_config(new_config, "input0", "min", "0.2");
            } else if (operation.compare("reciprocal") == 0) {
                update_stimulus_config_in_tensor_config(new_config, "input0", "min", "0.5");
            }
            string base_unpack_kernel_name = config.core_configs[coord].kernel_names[KernelType::UNPACK]["base"];
            string new_full_unpack_kernel_name = base_unpack_kernel_name;
            if (operation.compare("transpose_xy") == 0) {
                new_full_unpack_kernel_name = new_full_unpack_kernel_name + "_" + operation;
            }
            string base_math_kernel_name = config.core_configs[coord].kernel_names[KernelType::MATH]["base"];
            string base_pack_kernel_name = config.core_configs[coord].kernel_names[KernelType::PACK]["base"];
            string new_full_pack_kernel_name = base_pack_kernel_name;
            if (dst_sync_type.compare("db") == 0) {
                base_pack_kernel_name = "ctest_db_pack";
                new_full_pack_kernel_name = "ctest_db_pack";
                base_math_kernel_name = "ctest_db_math_eltwise_unary";
            } else if (dst_sync_type.compare("stream") == 0) {
                base_pack_kernel_name = "ctest_stream_pack_untilize";
                base_math_kernel_name = "ctest_stream_math_eltwise_unary";
                if ((operation.compare("datacopy") != 0) and (operation.compare("transpose_xy") != 0)) {
                    new_full_pack_kernel_name = "ctest_stream_pack_sfpu";
                } else {
                    new_full_pack_kernel_name = base_pack_kernel_name;
                }
            }
            string new_full_math_kernel_name = base_math_kernel_name + "_" + operation;

            if (bcast_type.compare("none") != 0) {
                new_full_math_kernel_name = base_math_kernel_name + "_" + bcast_type + "_" + operation;
                new_full_unpack_kernel_name = new_full_unpack_kernel_name + "_" + bcast_type;
            }

            //if (dst_sync_type.compare("stream") == 0) {
            //    int output0_data_buf_tile_size = 1 + rand() % 8;
            //    update_overlay_config(
            //        new_config.overlay_config, "output0_data_buf_size", to_string(output0_data_buf_tile_size));
            //}
            int input0_data_buf_size = 2 * num_tiles_in_block;
            update_overlay_config(new_config.overlay_config, "input0_data_buf_size", to_string(input0_data_buf_size));
            float dropout_percentage = 0.0f;
            if (operation.compare("dropout") == 0) {
                int dropout_percentage_int = rand() % 65536;
                dropout_percentage = (float)dropout_percentage_int / 65536.0f;
                update_extra_config(new_config, "dropout-percentage", to_string(dropout_percentage));
            }
            update_extra_config(new_config, "math-op", operation);
            update_extra_config(new_config, "bcast-type", bcast_type);
            update_extra_config(new_config, "dst-sync-type", dst_sync_type);
            update_extra_config(new_config, "block-count", to_string(block_count));
            update_extra_config(new_config, "block-tile-dim", to_string(num_block_input0_y_tiles));
            update_extra_config(new_config, "loop-count", to_string(loop_count));
            update_kernel_used(
                new_config, coord, KernelType::UNPACK, base_unpack_kernel_name, new_full_unpack_kernel_name);
            update_kernel_used(new_config, coord, KernelType::MATH, base_math_kernel_name, new_full_math_kernel_name);
            update_kernel_used(new_config, coord, KernelType::PACK, base_pack_kernel_name, new_full_pack_kernel_name);

            if (operation.compare("dropout") == 0) {
                update_comparison_config(
                    new_config,
                    ComparisonConfig(
                        {.dropout_percentage = dropout_percentage,
                         .dropout_percentage_error_threshold = 0.1f,
                         .skip_dropout_check = false,
                         .skip_pcc_check = true,
                         .skip_match_ratio_check = true}));
            }
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
std::map<std::string, bool> tests::untilize::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "untilize";
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
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, input0, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {
            std::map<std::string, uint32_t> tensor_address_map = {
                {input0.name, test_address_map::INPUT_A_DATA_BASE}, 
                {output0.name, test_address_map::OUTPUT_DATA_BASE}
            };
            set_tensor_addresses(test_config, tensor_address_map);
            prepare_test(device, test_config);

            // Write Inputs
            write_tensor(device, input0, test_config);
            device.run();
            device.wait_completion(500000);  // might need long time for sfpu to finish. timeout takes longer
            // Read Output
            read_tensor(device, output0, test_config, true);
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
