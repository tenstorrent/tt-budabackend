// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tests_lib.h"

#include <glog/logging.h>

#include <fstream>

#include "experimental/filesystem"
#include "llk_assemble_kernels.h"
#include "llk_tensor_ops.h"

std::pair<string, string> tests::randomize_relu_packer_config () {

    vector<string> relu_types = {"none"};
    string relu_type = relu_types[rand() % relu_types.size()];

    uint16_t relu_threshold = 0;
    return std::make_pair(relu_type, to_string(relu_threshold));

}
std::pair<string, string> tests::get_relu_packer_config (TestConfig config) {
    string relu_type = "none";
    if (config.extra_config.find("relu-type") != config.extra_config.end()) {
        relu_type = config.extra_config["relu-type"];
    }
    string relu_mode = std::to_string((uint32_t)llk::tensor_ops::get_relu_op_from_string(relu_type));
    string relu_threshold = "0";
    if (config.extra_config.find("relu-threshold") != config.extra_config.end()) {
        relu_threshold = config.extra_config["relu-threshold"];
    }
    return std::make_pair(relu_mode, relu_threshold);
}

bool tests::is_exp_8b_format(DataFormat data_format){
    return (DataFormat::Float32   == data_format) ||
           (DataFormat::Tf32      == data_format) ||
           (DataFormat::Float16_b == data_format) ||
           (DataFormat::Bfp8_b    == data_format) ||
           (DataFormat::Bfp4_b    == data_format) ||
           (DataFormat::Bfp2_b    == data_format);
}

std::pair<DataFormat, DataFormat> tests::get_unpacker_formats(DataFormat data_format, bool fp32_dest_acc_en) {
    if ((data_format == DataFormat::Float32) || (data_format == DataFormat::Tf32)) {
        // Current gen float32 gets unpacked to float16
        // if hw supports math dest in fp32, we can have src A/B in Tf32 if tile is in fp32
        DataFormat unpack_dst_format = fp32_dest_acc_en ? DataFormat::Tf32 : DataFormat::Float16_b;
        if(data_format == DataFormat::Tf32){
            assert(fp32_dest_acc_en && "Dest must be in FP32 for TF32 as unpack src/dest format");
        }
        //TF32 in memory is exactly same as FP32, but with LSBs zeroed out
        return std::make_pair(DataFormat::Float32, unpack_dst_format);
    } else if (data_format == DataFormat::Bfp4 || data_format == DataFormat::Bfp2) {
        // Bfp4 gets unpacked to bfp8
        return std::make_pair(data_format, DataFormat::Bfp8);
    } else if (data_format == DataFormat::Bfp4_b || data_format == DataFormat::Bfp2_b) {
        // Bfp4 gets unpacked to bfp8
        return std::make_pair(data_format, DataFormat::Bfp8_b);
    } else {
        return std::make_pair(data_format, data_format);
    }
}
std::pair<DataFormat, DataFormat> tests::get_packer_formats(DataFormat data_format, bool fp32_dest_acc_en) {
    if(fp32_dest_acc_en){
        //llk tests are single ops
        //if we expect multiple ops, and current op output data format is fp32 that feeds subsequent unpacker
        //we should set pack src format of current op to be TF32 so that we have rounding of mantissa (only avail on packer)
        DataFormat pack_src_format = (data_format == DataFormat::Tf32) ? DataFormat::Tf32 : DataFormat::Float32;
        return std::make_pair(pack_src_format, data_format);
    } else if (data_format == DataFormat::Float32) {
        // Current gen float32 gets unpacked to float16
        return std::make_pair(DataFormat::Float16_b, data_format);
    } else if (data_format == DataFormat::Bfp4 || data_format == DataFormat::Bfp2) {
        // Bfp4 packs Bfp8
        return std::make_pair(DataFormat::Bfp8, data_format);
    } else if (data_format == DataFormat::Bfp4_b || data_format == DataFormat::Bfp2_b) {
        // Bfp4 packs Bfp8
        return std::make_pair(DataFormat::Bfp8_b, data_format);
    } else {
        return std::make_pair(data_format, data_format);
    }
}
void tests::write_tensor(llk::Device &device, llk::Tensor &tensor, TestConfig &test_config, llk::xy_pair num_blocks) {
    if (test_config.tensor_configs[tensor.name].stream_config.initialized) {
        device.prepare_tensor_for_streaming(
            tensor, test_config_api::get_uniquified_output_folder(test_config) + "streaming_tensors/", num_blocks);
    } else {
        device.write_tensor(tensor, llk::xy_pair(0, 0), test_config.tensor_configs[tensor.name].address, num_blocks);
    }
}
void tests::read_tensor(llk::Device &device, llk::Tensor &tensor, TestConfig &test_config, bool untilized) {
    if (test_config.tensor_configs[tensor.name].stream_config.initialized) {
        assert((!untilized) && "We can't stream untilized block of data");
        device.read_tensor_for_streaming(
            tensor, test_config_api::get_uniquified_output_folder(test_config) + "streaming_tensors/");
    } else {
        device.read_tensor(tensor, llk::xy_pair(0, 0), test_config.tensor_configs[tensor.name].address, untilized);
    }
}
void tests::generate_kernel_param_file(KernelParams kernel_params, std::string templateFile, std::string outputFile) {
    std::experimental::filesystem::path input_path(templateFile);
    std::experimental::filesystem::path output_path(outputFile);
    if (not std::experimental::filesystem::exists(input_path.parent_path())) {
        std::experimental::filesystem::create_directories(input_path.parent_path());
    }
    if (not std::experimental::filesystem::exists(output_path.parent_path())) {
        std::experimental::filesystem::create_directories(output_path.parent_path());
    }

    std::ifstream input_param_file(templateFile);
    std::ofstream output_param_file(outputFile);
    std::string line;
    while (std::getline(input_param_file, line)) {
        for (const auto &it : kernel_params.param_mappings) {
            auto &key = it.first;
            auto &value = it.second;
            auto start_pos = line.find(key);
            while (start_pos != std::string::npos) {
                line.replace(line.find(key), key.length(), value);
                start_pos = line.find(key);
            }
        }

        auto start_double_underscore_pos = line.find("__");
        if (start_double_underscore_pos != std::string::npos) {
            std::cout << "Line: " << line << std::endl;
            std::cout << "Pos: " << start_double_underscore_pos << std::endl;
            throw std::runtime_error("There is a remaining param that isn't replaced");
        }
        output_param_file << line << std::endl;
    }
}

void tests::generate_kernel_param_files(tests::TestConfig &test_config) {
    std::string param_file_outdir = test_config_api::get_uniquified_output_folder(test_config) + "ctest_kernels/" +
                                    test_config.test_name + "_params/";
    std::string param_file_indir = test_config.test_args.gitroot + "/tb/llk_tb/tests/ctest_params_template";
    std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> kernels_used;
    for (auto const &core_config_it : test_config.core_configs) {
        llk::xy_pair coord = core_config_it.first;
        auto core_config = core_config_it.second;
        auto kernel_parameters = core_config.kernel_parameters;

        for (auto const &kernel_names_it : core_config.kernel_names) {
            tests::KernelType kernel_type = kernel_names_it.first;
            auto names_map = kernel_names_it.second;
            auto base_kernel_name = names_map["base"];
            auto full_kernel_name = names_map["full"];
            // Generate parameter file for this kernel
            std::string input_param_file = param_file_indir + "/" + base_kernel_name + "_params.h";
            std::string output_param_file = param_file_outdir + "/" + base_kernel_name + "_params.h";
            if (kernel_parameters.find(kernel_type) != kernel_parameters.end()) {
                generate_kernel_param_file(kernel_parameters[kernel_type], input_param_file, output_param_file);
            } else {
                LOG(INFO) << "Cannot find " << kernel_type << " for " << base_kernel_name << " in kernel_parameters";
                tests::KernelParams empty_params;
                generate_kernel_param_file(empty_params, input_param_file, output_param_file);
            }
        }
    }
}

void tests::compile_and_setup_kernels(tests::TestConfig &test_config, llk::Device &device) {
    std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> kernels_used;
    for (auto const &core_config_it : test_config.core_configs) {
        llk::xy_pair coord = core_config_it.first;
        auto core_config = core_config_it.second;

        std::vector<KernelType> kernel_types = {KernelType::UNPACK, KernelType::MATH, KernelType::PACK};

        std::vector<std::vector<std::string>> kernels_per_coord;
        for (auto const &kernel_type : kernel_types) {
            if (core_config.kernel_names.find(kernel_type) != core_config.kernel_names.end()) {
                auto names_map = core_config.kernel_names[kernel_type];
                auto base_kernel_name = names_map["base"];
                auto full_kernel_name = names_map["full"];

                kernels_per_coord.push_back({full_kernel_name});
            } else {
                throw std::runtime_error(
                    "Cannot find the kernel used for " + tests::get_string_from_kernel_type(kernel_type));
            }
        }
        kernels_used.insert({coord, kernels_per_coord});
    }
    device.setup_kernels(
        test_config.test_args.gitroot,
        kernels_used,
        test_config.test_name,
        test_config_api::get_uniquified_output_folder(test_config) + "ctest_kernels/",
        test_config.hlkc_test,
        test_config.perf_dump);
}

void tests::prepare_test(llk::Device &device, tests::TestConfig &test_config) {
    VLOG(2) << __FUNCTION__ << "::Running " << test_config.test_name << std::endl;

    // Search and setup any streaming for a tensor if it is initialized.
    for (auto &it : test_config.tensor_configs) {
        string tensor_string = it.first;
        auto &tensor_config = it.second;
        if (tensor_config.stream_config.initialized) {
            test_config_api::update_tensor_config_for_streaming(
                tensor_config, test_config_api::get_uniquified_output_folder(test_config) + "streaming_tensors/");
        }
    }
    device.init(
        test_config.test_args.gitroot,
        test_config.device + "_1x1_tensix_only_arch.yaml");

    // Generate overlay blob
    device.generate_overlay_blob(
        test_config.test_args.gitroot,
        test_config_api::get_uniquified_output_folder(test_config),
        test_config.overlay_config.config);

    YAML::Node testdef_yaml;
    auto stream_configs = test_config_api::get_stream_configs(test_config);
    get_testdef_yaml_from_stream_configs(testdef_yaml, stream_configs);

    // Setup Simulator
    // TODO: Multi-Core??
    device.start(
        test_config.test_args.gitroot,
        test_config_api::get_uniquified_output_folder(test_config),
        test_config.test_args.plusargs,
        test_config.test_args.dump_cores,
        testdef_yaml,
        test_config.overlay_config.graph_name);

    // Generate kernel params
    generate_kernel_param_files(test_config);
    compile_and_setup_kernels(test_config, device);
}

bool tests::check_result(
    tests::TestArgs &args,
    std::string test_name,
    llk::Device &device,
    llk::Tensor &output_tensor,
    llk::Tensor &expected_output_tensor,
    tests::ComparisonConfig comparison_config) {
    bool result = true;
    // Output Dumping + verify
    output_tensor.untilize();
    output_tensor.dump(false, args.dump_tensors, "output_tensor." + test_name);
    result &= verify_match_ratio(
        expected_output_tensor,
        output_tensor,
        comparison_config.minimum_match_ratio,
        comparison_config.match_ratio_error_threshold,
        comparison_config.dropout_percentage,
        comparison_config.dropout_percentage_error_threshold,
        comparison_config.skip_match_ratio_check,
        comparison_config.skip_dropout_check);
    result &= verify_pcc(
        expected_output_tensor, output_tensor, comparison_config.minimum_pcc, comparison_config.skip_pcc_check);
    return result;
}

void tests::set_sfpu_params_from_extra_config(
    std::map<std::string, std::string> &kernel_params, std::map<std::string, std::string> &extra_config) {
    string sfpu_param0 = "0";
    if (extra_config.find("sfpu-param0") != extra_config.end()) {
        sfpu_param0 = extra_config["sfpu-param0"];
    }
    string sfpu_param1 = "0";
    if (extra_config.find("sfpu-param1") != extra_config.end()) {
        sfpu_param1 = extra_config["sfpu-param1"];
    }
    string sfpu_param2 = "0";
    if (extra_config.find("sfpu-param2") != extra_config.end()) {
        sfpu_param2 = extra_config["sfpu-param2"];
    }
    string sfpu_param3 = "0";
    if (extra_config.find("sfpu-param3") != extra_config.end()) {
        sfpu_param3 = extra_config["sfpu-param3"];
    }
    string sfpu_param4 = "0";
    if (extra_config.find("sfpu-param4") != extra_config.end()) {
        sfpu_param4 = extra_config["sfpu-param4"];
    }
    string sfpu_param5 = "0";
    if (extra_config.find("sfpu-param5") != extra_config.end()) {
        sfpu_param5 = extra_config["sfpu-param5"];
    }

    if (extra_config.find("math-op") != extra_config.end()) {
        if (extra_config["math-op"].compare("dropout") == 0) {
            assert(
                (extra_config.find("dropout-percentage") != extra_config.end()) &&
                "Cannot find dropout percentage for dropout op");
            float dropout_percentage = stof(extra_config["dropout-percentage"]);
            int dropout_seed = rand() % 65536;
            uint32_t dropout_percentage_int = dropout_percentage * 65536;
            float scale = 1 / (1 - dropout_percentage);
            sfpu_param0 = to_string(dropout_percentage_int);
            sfpu_param1 = to_string(ttl::convert_float_to_1_8_7(scale));
            sfpu_param2 = to_string(ttl::convert_float_to_1_8_7(dropout_seed));
        }
    }

    kernel_params["__sfpu_param0__"] = sfpu_param0;
    kernel_params["__sfpu_param1__"] = sfpu_param1;
    kernel_params["__sfpu_param2__"] = sfpu_param2;
    kernel_params["__sfpu_param3__"] = sfpu_param3;
    kernel_params["__sfpu_param4__"] = sfpu_param4;
    kernel_params["__sfpu_param5__"] = sfpu_param5;
}
