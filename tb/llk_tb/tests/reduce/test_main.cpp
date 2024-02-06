// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <thread>
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
void tests::reduce::generate_stimulus_and_expected_tensors(
    TestConfig test_config, Tensor &input0, Tensor &expected_output0) {
    bool to_stdout = false;
    srand(test_config.seed);  // Seed randomizer from test_config seed.
    cout << test_config.test_name << " stimulus: configured with seed=" << test_config.seed << endl;
    input0 =
        Tensor(test_config.tensor_configs["input0"].dims, "input0", test_config.tensor_configs["input0"].data_format);
    expected_output0 =
        Tensor(test_config.tensor_configs["output0"].dims, test_config.tensor_configs["output0"].data_format);

    // Stimulus Generation.  All configuration comes as a string
    int input0_seed = rand();
    input0.populate_with_uniform_distribution_data(
        stof(test_config.tensor_configs["input0"].stimulus_config["min"]),
        stof(test_config.tensor_configs["input0"].stimulus_config["max"]),
        input0_seed);
    input0.dump(to_stdout, test_config.test_args.dump_tensors, "input0_" + test_config.test_name);

    // Expected Generation
    float mult_coefficient = 1;
    if (test_config.extra_config.find("reduce-op") == test_config.extra_config.end()) {
        throw std::runtime_error("Cannot find math-op defined in extra config");
    }
    llk::tensor_ops::reduce_math_op math_op;
    if (test_config.extra_config.find("reduce-math-op") != test_config.extra_config.end()) {
        math_op = tensor_ops::get_reduce_math_op_from_string(test_config.extra_config["reduce-math-op"]);
    } else {
        throw std::runtime_error("Cannot find reduce-math-op defined in extra config");
    }

    int tensor_dim_size = test_config.tensor_configs["input0"].dims.x;
    if (test_config.extra_config["reduce-op"] == "col") {
        tensor_dim_size = test_config.tensor_configs["input0"].dims.y;
    } else if (test_config.extra_config["reduce-op"] == "scalar") {
        tensor_dim_size = test_config.tensor_configs["input0"].dims.x * test_config.tensor_configs["input0"].dims.y;
    }

    if (math_op == llk::tensor_ops::reduce_math_op::AVG) {
        mult_coefficient = 1.0f / (float)tensor_dim_size;
    } else if (math_op == llk::tensor_ops::reduce_math_op::SUM) {
        mult_coefficient = 1.0f;
    } else if (math_op == llk::tensor_ops::reduce_math_op::MAX) {
        // TODO:
    }
    tensor_ops::reduce(
        tensor_ops::get_reduce_op_from_string(test_config.extra_config["reduce-op"]),
        math_op,
        expected_output0,
        input0,
        mult_coefficient);
    expected_output0.dump(to_stdout, test_config.test_args.dump_tensors, "expected_output0_" + test_config.test_name);
}

// Test Specific.  Can be based off config or just directly written to.
void tests::reduce::update_kernel_parameter_mapping(TestConfig &config) {
    xy_pair coord = llk::xy_pair(0, 0);  // TODO: Handle multi-core scenario? but up to user

    // Pull information from config.
    string loop_count = "1";
    if (config.extra_config.find("loop-count") != config.extra_config.end()) {
        loop_count = config.extra_config["loop-count"];
    }
    int num_input_block_rows = 1;
    if (config.extra_config.find("num-block-y") != config.extra_config.end()) {
        num_input_block_rows = stoi(config.extra_config["num-block-y"]);
    }
    int num_output_block_rows = num_input_block_rows;

    int num_input_block_columns = 1;
    if (config.extra_config.find("num-block-x") != config.extra_config.end()) {
        num_input_block_columns = stoi(config.extra_config["num-block-x"]);
    }
    int num_output_block_columns = num_input_block_columns;

    bool fp32_dest_acc_en = false;
    if(config.extra_config.find("dest-acc") != config.extra_config.end()){
        fp32_dest_acc_en = (config.extra_config["dest-acc"] == "fp32_acc");
    }

    if(fp32_dest_acc_en){
        //std::cout << "fp32_dest_acc_en mode set with device: " << config.device << std::endl;
        assert(config.device != "grayskull");
        assert(config.device != "");
    }

    string tensor_dim_size = to_string(config.tensor_configs["input0"].dims.x);
    if (config.extra_config["reduce-op"] == "col") {
        tensor_dim_size = to_string(config.tensor_configs["input0"].dims.y);
    } else if (config.extra_config["reduce-op"] == "scalar") {
        tensor_dim_size =
            to_string(std::sqrt(config.tensor_configs["input0"].dims.x * config.tensor_configs["input0"].dims.y));
        num_output_block_rows = 1;
        num_output_block_columns = 1;
    }
    if (config.extra_config["reduce-math-op"] == "sum") {
        tensor_dim_size = "1";
    }

    if (config.extra_config.find("num-tiles-input0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "input0", atol(config.extra_config["num-tiles-input0-buf"].c_str()));
    }
    if (config.extra_config.find("num-tiles-output0-buf") != config.extra_config.end()) {
        update_stream_buffer_size_for_tensor(
            config, "output0", atol(config.extra_config["num-tiles-output0-buf"].c_str()));
    }
    auto input0_unpacker_formats = get_unpacker_formats(config.tensor_configs["input0"].data_format, fp32_dest_acc_en);

    if(fp32_dest_acc_en){
        //reduce kernel uses mov instruction, which will not rebias exp
        //have to make sure exp width between src a/b and dest match
        assert(is_exp_8b_format(input0_unpacker_formats.second));
    }

    auto output0_packer_formats = get_packer_formats(config.tensor_configs["output0"].data_format, fp32_dest_acc_en);

    string input0_data_src_format = to_str(input0_unpacker_formats.first);
    string input0_data_dst_format = to_str(input0_unpacker_formats.second);
    string output0_data_src_format = to_str(output0_packer_formats.first);
    string output0_data_dst_format = to_str(output0_packer_formats.second);

    string num_tiles_x = to_string(config.tensor_configs["input0"].dims.rt);
    string num_tiles_y = to_string(config.tensor_configs["input0"].dims.ct);
    string input_num_tile_rows_per_block = to_string(config.tensor_configs["input0"].dims.ct / num_input_block_rows);
    string input_num_tile_cols_per_block = to_string(config.tensor_configs["input0"].dims.rt / num_input_block_columns);
    string output_num_tile_rows_per_block = to_string(config.tensor_configs["output0"].dims.ct / num_output_block_rows);
    string output_num_tile_cols_per_block =
        to_string(config.tensor_configs["output0"].dims.rt / num_output_block_columns);

    // Update the kernel parameters in config
    map<string, string> common_kernel_params = {
        {"__arg_loop_count__", loop_count},
    };

    update_kernel_parameters(config, coord, KernelType::UNPACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__tensor_dim_size__", tensor_dim_size);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__src_format__", input0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__dst_format__", input0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::UNPACK, "__num_block_rows__", to_string(num_input_block_rows));
    update_kernel_parameters(
        config, coord, KernelType::UNPACK, "__num_block_columns__", to_string(num_input_block_columns));
    update_kernel_parameters(
        config, coord, KernelType::UNPACK, "__num_tile_rows_per_block__", input_num_tile_rows_per_block);
    update_kernel_parameters(
        config, coord, KernelType::UNPACK, "__num_tile_columns_per_block__", input_num_tile_cols_per_block);
    update_kernel_parameters(config, coord, KernelType::MATH, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::MATH, "__num_block_rows__", to_string(num_input_block_rows));
    update_kernel_parameters(
        config, coord, KernelType::MATH, "__num_block_columns__", to_string(num_input_block_columns));
    update_kernel_parameters(
        config, coord, KernelType::MATH, "__num_tile_rows_per_block__", input_num_tile_rows_per_block);
    update_kernel_parameters(
        config, coord, KernelType::MATH, "__num_tile_columns_per_block__", input_num_tile_cols_per_block);
    update_kernel_parameters(config, coord, KernelType::PACK, common_kernel_params);
    update_kernel_parameters(config, coord, KernelType::PACK, "__src_format__", output0_data_src_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__dst_format__", output0_data_dst_format);
    update_kernel_parameters(config, coord, KernelType::PACK, "__num_block_rows__", to_string(num_output_block_rows));
    update_kernel_parameters(
        config, coord, KernelType::PACK, "__num_block_columns__", to_string(num_output_block_columns));
    update_kernel_parameters(
        config, coord, KernelType::PACK, "__num_tile_rows_per_block__", output_num_tile_rows_per_block);
    update_kernel_parameters(
        config, coord, KernelType::PACK, "__num_tile_columns_per_block__", output_num_tile_cols_per_block);
    auto relu_packer_config = get_relu_packer_config(config);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_mode__", relu_packer_config.first);
    update_kernel_parameters(config, coord, KernelType::PACK, "__relu_threshold__", relu_packer_config.second);
}

//! Generate a map of tests configurations that need to be run during sweeping
map<string, TestConfig> tests::reduce::generate_test_configs(
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
        vector<string> reduce_ops = {"row", "col", "scalar"};
        vector<DataFormat> data_formats = {
            DataFormat::Bfp8,
            DataFormat::Float16,
            DataFormat::Bfp8_b,
            DataFormat::Float16_b,
            DataFormat::Float32};
        // vector<string> reduce_math_ops = {"avg", "max", "sum"}; // TODO: Fix later, only sum and avg supported atm
        vector<string> reduce_math_ops = {"avg", "sum"};
        vector<string> list_of_fidelity_phases = {"LF", "HF2", "HF3", "HF4"};
        int id = 0;
        while (test_configs.size() < args.num_configs_to_randomize) {
            string reduce_op = reduce_ops[rand() % reduce_ops.size()];
            DataFormat data_format = data_formats[rand() % data_formats.size()];
            string reduce_math_op = reduce_math_ops[rand() % reduce_math_ops.size()];
            string fidelity_phases = list_of_fidelity_phases[rand() % list_of_fidelity_phases.size()];

            // Randomize configuration:
            int max_input_l1_buffer_size = llk::test_address_map::INPUT_DATA_OFFSET;
            int max_output_l1_buffer_size = llk::test_address_map::OUTPUT_DATA_SIZE;
            int max_tiles_in_dst = 16;
            int max_tiles = 256;
            int tile_size = llk::num_bytes(data_format) * 32 * 32 + llk::num_bytes_extra(data_format) + 16;

            // Input tensor config
            int total_tiles = rand() % max_tiles + 1;
            int num_input0_x_tiles;
            int num_input0_y_tiles;
            bool fits_dst = true;
            bool fits_l1 = true;
            int num_block_y;
            int num_block_x;
            bool not_even_blocks = true;
            bool not_even_tiles = true;
            int num_tiles_input0_buf;
            int num_tiles_output0_buf;
            do {
                fits_dst = true;
                fits_l1 = true;
                num_input0_x_tiles = rand() % total_tiles + 1;
                num_input0_y_tiles = total_tiles / num_input0_x_tiles;
                not_even_tiles = total_tiles % (num_input0_x_tiles * num_input0_y_tiles);

                num_block_y = rand() % num_input0_y_tiles + 1;
                if (reduce_op.find("col") == 0) {
                    num_block_x = 1;
                } else {
                    num_block_x = rand() % num_input0_x_tiles + 1;
                }
                not_even_blocks = (num_input0_y_tiles % num_block_y) || (num_input0_x_tiles % num_block_x);

                num_tiles_input0_buf = num_input0_x_tiles * num_input0_y_tiles / (num_block_x * num_block_y) * 2;
                int num_tiles_non_reduce_dim = 0;
                if (reduce_op.find("col") == 0) {
                    num_tiles_non_reduce_dim = num_input0_x_tiles / num_block_x;
                    num_tiles_output0_buf = 2 * num_tiles_non_reduce_dim;
                    // output buffer is smaller than output l1 space
                    fits_l1 &= ((num_block_y == 1) || ((num_input0_x_tiles / num_block_x) < max_tiles_in_dst));
                } else if (reduce_op.find("row") == 0) {
                    num_tiles_non_reduce_dim = num_input0_y_tiles / num_block_y;
                    num_tiles_output0_buf = 2 * num_tiles_non_reduce_dim;
                    fits_l1 &= ((num_block_x == 1) || ((num_input0_y_tiles / num_block_y) < max_tiles_in_dst));
                } else {
                    num_tiles_output0_buf = 2 * 1;
                }
                fits_l1 &= ((tile_size * num_tiles_input0_buf) < max_input_l1_buffer_size);
                fits_l1 &= ((tile_size * num_tiles_output0_buf) < max_output_l1_buffer_size);

                fits_dst &= (num_tiles_non_reduce_dim < max_tiles_in_dst);
            } while (not_even_tiles || not_even_blocks || !fits_dst || !fits_l1);
            int input0_x = num_input0_x_tiles * 32;
            int input0_y = num_input0_y_tiles * 32;

            // Update Configuration
            TestConfig new_config = config;
            new_config.seed = rand();
            TensorDims dim(1, 1, input0_y, input0_x);

            TensorDims output_dim;
            if (reduce_op.find("row") == 0) {
                output_dim = TensorDims(1, 1, input0_y, 32);
            } else if (reduce_op.find("col") == 0) {
                output_dim = TensorDims(1, 1, 32, input0_x);
            } else {
                output_dim = TensorDims(1, 1, 32, 32);
            }
            if (reduce_math_op.find("avg") == 0) {
                update_stimulus_config_in_tensor_config(new_config, "input0", "min", "0.0");
            }
            update_tensor_config(new_config, "input0", dim, data_format);
            update_tensor_config(new_config, "output0", output_dim, data_format);

            string base_unpack_kernel_name = config.core_configs[coord].kernel_names[KernelType::UNPACK]["base"];
            string new_full_unpack_kernel_name = base_unpack_kernel_name + "_" + reduce_op;
            string base_math_kernel_name = config.core_configs[coord].kernel_names[KernelType::MATH]["base"];
            string new_full_math_kernel_name =
                base_math_kernel_name + "_" + reduce_op + "_" + reduce_math_op + "_" + fidelity_phases;
            string base_pack_kernel_name = config.core_configs[coord].kernel_names[KernelType::PACK]["base"];
            string new_full_pack_kernel_name = base_pack_kernel_name + "_" + reduce_op;
            update_extra_config(new_config, "reduce-op", reduce_op);
            update_extra_config(new_config, "reduce-math-op", reduce_math_op);
            update_extra_config(new_config, "fidelity", fidelity_phases);
            update_extra_config(new_config, "num-block-x", to_string(num_block_x));
            update_extra_config(new_config, "num-block-y", to_string(num_block_y));
            update_extra_config(new_config, "num-tiles-input0-buf", to_string(num_tiles_input0_buf));
            update_extra_config(new_config, "num-tiles-output0-buf", to_string(num_tiles_output0_buf));

            if (data_format == DataFormat::Bfp4 || data_format == DataFormat::Bfp4_b){
                update_comparison_config(new_config, ComparisonConfig({.skip_match_ratio_check = true}));
            } else if (data_format == DataFormat::Bfp2 || data_format == DataFormat::Bfp2_b){
                update_comparison_config(config, ComparisonConfig({.minimum_pcc=0.8f, .skip_match_ratio_check = true}));
            }
            
            update_kernel_used(new_config, coord, KernelType::UNPACK, base_unpack_kernel_name, new_full_unpack_kernel_name);
            update_kernel_used(new_config, coord, KernelType::MATH, base_math_kernel_name, new_full_math_kernel_name);
            update_kernel_used(new_config, coord, KernelType::PACK, base_pack_kernel_name, new_full_pack_kernel_name);
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

void tests::reduce::test_loop_body(
    string test_name, string config_string, TestConfig test_config, map<string, bool> &test_results) {
    std::string output_yaml_name = test_config_api::get_uniquified_output_folder(test_config) + "test.yaml";
    std::cout << "Dumping config yaml: " << output_yaml_name << std::endl;
    dump_config_as_yaml(test_config, output_yaml_name);
    cout << " -- RUNNING LLK_TEST::" << test_name << "::" << config_string << endl;
    // Tensor allocation and generation of stimilus from test_config
    Device device;
    xy_pair target_coord(0, 0);  // TODO: Multi-core support needed for writing and reading tensors....

    Tensor input0;
    Tensor expected_output0;
    generate_stimulus_and_expected_tensors(test_config, input0, expected_output0);
    Tensor output0(expected_output0.dims, "output0", expected_output0.data_format);

    bool config_result = false;
    // Prepare Test for test_config
    try {
        std::map<std::string, uint32_t> tensor_address_map = {
            {input0.name, test_address_map::INPUT_DATA_BASE}, {output0.name, test_address_map::OUTPUT_DATA_BASE}};
        set_tensor_addresses(test_config, tensor_address_map);
        prepare_test(device, test_config);

        int num_block_y = 1;
        if (test_config.extra_config.find("num-block-y") != test_config.extra_config.end()) {
            num_block_y = stoi(test_config.extra_config["num-block-y"]);
        }
        int num_block_x = 1;
        if (test_config.extra_config.find("num-block-x") != test_config.extra_config.end()) {
            num_block_x = stoi(test_config.extra_config["num-block-x"]);
        }
        // Write Inputs
        write_tensor(device, input0, test_config, llk::xy_pair(num_block_x, num_block_y));
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
//! Test Entrypoint for llk_tb testing infra
/*! see test_lib.h/test_lib.cpp for reference functions
 */
std::map<std::string, bool> tests::reduce::test_main(TestArgs args) {
    // Basic test infra instantiation
    string test_name = "reduce";
    string test_descriptor_file_path = args.test_descriptor;
    YAML::Node test_descriptor = YAML::LoadFile(test_descriptor_file_path);

    // Generate test configs to be run
    map<string, TestConfig> test_configs = generate_test_configs(args, test_name, test_descriptor);
    map<string, bool> test_results;
    bool result = true;
    std::vector<std::thread> ths(args.jobs);
    int test_index = 0;
    for (auto const &it : test_configs) {
        // int job_index = test_index % args.jobs;
        // if (ths[job_index].joinable()) {
        //     ths[job_index].join();
        // }

        // Run test configs in a loop or in separate threads if needed
        string config_string = it.first;
        TestConfig test_config = it.second;

        // ths[job_index] = std::thread(test_loop_body, test_name, config_string, &test_config, &test_results);
        test_loop_body(test_name, config_string, test_config, test_results);
    }

    // for (auto &th : ths) {
    //     if (th.joinable()) {
    //         th.join();
    //     }
    // }
    return test_results;
}
