// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "llk_tensor_ops.h"
#include "llk_types.h"
#include "test.h"
#include "tests_lib.h"
#include "utils/math.h"

using namespace std;
using namespace llk;
using namespace tests;
using namespace tests::test_config_api;

//! Generate expected tensors and input tensors from test_config
void tests::eltwise_unary::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    input0 = Tensor(
        test_config.tensor_configs["input0"].dims,
        test_config.tensor_configs["input0"].name,
        test_config.tensor_configs["input0"].data_format);
    expected_output0 = Tensor(
        test_config.tensor_configs["output0"].dims,
        "expected_output0",
        test_config.tensor_configs["output0"].data_format);
    llk::Tensor expected_output0_pre_relu;
    expected_output0_pre_relu = Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
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
            scale_factor = 1 / (1 - std::stof(test_config.extra_config["dropout-percentage"]));
        }
    }

    tensor_ops::unary_broadcast(
        tensor_ops::get_unary_op_from_string(test_config.extra_config["math-op"]),
        expected_output0_pre_relu,
        expected_output0_pre_relu.dims,
        input0,
        tensor_ops::get_broadcast_op_from_string(bcast_type),
        scale_factor);

    string relu_type = "none";
    float relu_threshold = 0.0f;
    if (test_config.extra_config.find("relu-type") != test_config.extra_config.end()) {
        relu_type = test_config.extra_config["relu-type"];
        if ((test_config.extra_config["relu-type"].compare("max_threshold") == 0)||
            (test_config.extra_config["relu-type"].compare("min_threshold") == 0)) {
            if (test_config.extra_config.find("relu-threshold") == test_config.extra_config.end()) {
                throw std::runtime_error("Relu with min/max threshold operation needs relu-threshold defined in extra config");
            } else {
                relu_threshold = std::stof(test_config.extra_config["relu-threshold"]);
            }
        }
    }
    tensor_ops::relu(
        tensor_ops::get_relu_op_from_string(relu_type),
        expected_output0,
        expected_output0_pre_relu,
        relu_threshold
    );
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::eltwise_unary::update_kernel_parameter_mapping(TestConfig &config) {
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

    // Pull information from config.
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format, fp32_dest_acc_en);
    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format, fp32_dest_acc_en);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
    string output0_data_src_format = to_str(output0_packer_formats.first);
    string output0_data_dst_format = to_str(output0_packer_formats.second);
    std::cout << "output0_data_dst_format = " << output0_data_dst_format<< std::endl;

    string loop_count = "1";
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = config.extra_config["loop-count"];
    }
    string block_count = "1";
    if (config.extra_config.find("block-count") != config.extra_config.end()) {
        block_count = config.extra_config["block-count"];
    }
    string block_tile_dim = to_string(config.tensor_configs["input0"].dims.num_tiles() / stoul(block_count));
    if (config.extra_config.find("block-tile-dim") != config.extra_config.end()) {
        block_tile_dim = config.extra_config["block-tile-dim"];
    }
    if (config.extra_config.find("num-tiles-output0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "output0", atol(config.extra_config["num-tiles-output0-buf"].c_str()));
    }
    if (config.extra_config.find("num-tiles-input0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "input0", atol(config.extra_config["num-tiles-input0-buf"].c_str()));
    }
    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__dst_tile_rows__", "1"},
        {"__dst_tile_cols__", block_tile_dim},
        {"__block_cnt__", block_count},
        {"__arg_loop_count__", loop_count},
    };

    if (config.tensor_configs["input0"].data_format == DataFormat::Bfp4
        || config.tensor_configs["input0"].data_format == DataFormat::Bfp4_b
        || config.tensor_configs["output0"].data_format == DataFormat::Bfp4
        || config.tensor_configs["output0"].data_format == DataFormat::Bfp4_b){
        update_comparison_config(config, ComparisonConfig({.skip_match_ratio_check = true}));
    } else if(config.tensor_configs["input0"].data_format == DataFormat::Bfp2
        || config.tensor_configs["input0"].data_format == DataFormat::Bfp2_b
        || config.tensor_configs["output0"].data_format == DataFormat::Bfp2
        || config.tensor_configs["output0"].data_format == DataFormat::Bfp2_b){
        
        update_comparison_config(config, ComparisonConfig({.minimum_pcc=0.8f, .skip_match_ratio_check = true}));
    }
    set_sfpu_params_from_extra_config(common_kernel_params, config.extra_config);
    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::MATH, "__src_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::MATH, "__dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::eltwise_unary::generate_test_configs(
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
        vector<string> dst_sync_types = {"full", "db"};
        vector<string> bcast_types = {"none", "broadcast_row", "broadcast_col", "broadcast_scalar"};
        vector<DataFormat> data_formats = {
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp8_b,
            DataFormat::Float16_b,
            DataFormat::Float32};
        vector<string> operations = {
            "dropout",
            "gelu",
            "gelu_derivative",
            "exponential",
            "log",
            "sigmoid",
            "sqrt",
            "reciprocal",
            "tanh",
            "transpose_xy",
            "datacopy",
            "zero_output"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            string dst_sync_type = dst_sync_types[rand() % dst_sync_types.size()];
            string operation = operations[rand() % operations.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];
            string bcast_type = "none";

            if (operation.compare("datacopy") == 0) {
                bcast_type = bcast_types[rand() % bcast_types.size()];
            }

            // Randomize configuration:
            // l1 --> max buffer size --> 128kB
            // loops -->
            int max_input_l1_buffer_size = llk::test_address_map::INPUT_DATA_OFFSET;
            int max_output_l1_buffer_size = llk::test_address_map::OUTPUT_DATA_SIZE;

            int max_tiles =
                32;  // Makng the default too large will cause widespread failure since it takes a long time.
            if ((operation.compare("datacopy") == 0) or (operation.compare("transpose_xy") == 0) or
                (operation.compare("dropout") == 0) or (operation.compare("sqrt") == 0) or
                operation.compare("sigmoid") == 0) {
                max_tiles = 128;
            }
            int max_tiles_in_dst = 16;
            if (dst_sync_type.compare("db") == 0) {
                max_tiles_in_dst = 8;
            }
            // This is because the stream buffer must fit within l1.  The stream buffer has to double buffer for inputs
            // For eltwise_unary, output and inputs can both hit 256kB
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;
            while (tile_size * max_tiles_in_dst * 2 >= max_output_l1_buffer_size) {
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
                base_pack_kernel_name = "ctest_stream_pack";
                base_math_kernel_name = "ctest_stream_math_eltwise_unary";
                if ((operation.compare("datacopy") != 0) and (operation.compare("transpose_xy") != 0) and
                    (operation.compare("zero_output") != 0)) {
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

            int num_tiles_output0_buf = 2 * num_tiles_in_block;
            if (dst_sync_type.compare("stream") == 0) {
                num_tiles_output0_buf = 1 + rand() % 8;
            }
            int num_tiles_input0_buf = 2 * num_tiles_in_block;
            float dropout_percentage = 0.0f;
            if (operation.compare("dropout") == 0) {
                int dropout_percentage_int = rand() % 65536;
                dropout_percentage = (float)dropout_percentage_int / 65536.0f;
            }
            if (operation.compare("zero_output") == 0) {
                new_full_pack_kernel_name = base_pack_kernel_name + "_zero";
                base_math_kernel_name = "cblank";
                new_full_math_kernel_name = "cblank";
                base_unpack_kernel_name = "cblank";
                new_full_unpack_kernel_name = "cblank";
            }
            update_extra_config(new_config, "dropout-percentage", to_string(dropout_percentage));
            update_extra_config(new_config, "math-op", operation);
            update_extra_config(new_config, "bcast-type", bcast_type);
            update_extra_config(new_config, "dst-sync-type", dst_sync_type);
            update_extra_config(new_config, "block-count", to_string(block_count));
            update_extra_config(new_config, "block-tile-dim", to_string(num_tiles_in_block));
            update_extra_config(new_config, "num-tiles-output0-buf", to_string(num_tiles_output0_buf));
            update_extra_config(new_config, "num-tiles-input0-buf", to_string(num_tiles_input0_buf));
            update_extra_config(new_config, "loop-count", to_string(loop_count));
            auto randomized_relu_config = randomize_relu_packer_config();
            update_extra_config(new_config, "relu-type", randomized_relu_config.first);
            update_extra_config(new_config, "relu-threshold", randomized_relu_config.second);
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
std::map<std::string, bool> tests::eltwise_unary::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "eltwise_unary";
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
        Tensor input0;
        Tensor expected_output0;
        generate_stimulus_and_expected_tensors(test_config, input0, expected_output0);
        Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

        bool config_result = false;
        // Prepare Test for test_config
        try {
            std::map<std::string, uint32_t> tensor_address_map = {
                {"input0", test_address_map::INPUT_DATA_BASE}, {"output0", test_address_map::OUTPUT_DATA_BASE}};
            set_tensor_addresses(test_config, tensor_address_map);
            // Configure streams for tensor
            prepare_test(device, test_config);
            // Write Inputs
            write_tensor(device, input0, test_config);
            device.run();
            if (test_config.extra_config["math-op"].compare("zero_output") == 0) {
                device.wait_completion(350000, {llk::ThreadType::Ncrisc, llk::ThreadType::Pack});
            } else {
                device.wait_completion(700000);  // might need long time for sfpu to finish. timeout takes longer
            }
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
