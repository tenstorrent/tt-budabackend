// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

#include "llk_types.h"
#include "test_args.h"
#include "test_config.h"
#include "test_kernel_params.h"
#include "yaml-cpp/yaml.h"

namespace tests {

/*! Functions here are just helper functions that end up calling device or test config apis and make it cleaner at the
 *  test level
 */
//! Common Relu config getters
std::pair<string, string> randomize_relu_packer_config ();
std::pair<string, string> get_relu_packer_config (TestConfig config);
//! Format Helper Functions
bool is_exp_8b_format(DataFormat data_format);
std::pair<DataFormat, DataFormat> get_unpacker_formats(DataFormat data_format, bool fp32_dest_acc_en = false);
std::pair<DataFormat, DataFormat> get_packer_formats(DataFormat data_format, bool fp32_dest_acc_en = false);
//! Write a tensor
void write_tensor(
    llk::Device &device, llk::Tensor &tensor, TestConfig &test_config, llk::xy_pair num_blocks = llk::xy_pair(1, 1));
//! Read a tensor
void read_tensor(llk::Device &device, llk::Tensor &tensor, TestConfig &test_config, bool untilized = false);
//! Generates a parameter file for specific kernel param
void generate_kernel_param_file(KernelParams kernel_params, std::string templateFile, std::string outputFile);
//! Generates parameter files for llks from the config passed in
void generate_kernel_param_files(tests::TestConfig &test_config);
//! Compile and setup kernels used for the test
void compile_and_setup_kernels(tests::TestConfig &test_config, llk::Device &device);
//! prepares the test by setting up simulator and the test specifications. Does not generate test vectors
void prepare_test(llk::Device &device, tests::TestConfig &test_config);
//! checks results by reading the output tensor and comparing it to expected
bool check_result(
    tests::TestArgs &args,
    std::string test_name,
    llk::Device &device,
    llk::Tensor &output_tensor,
    llk::Tensor &expected_output_tensor,
    tests::ComparisonConfig comparison_config);

//! Helper Function for setting kernel params for sfpu_params
void set_sfpu_params_from_extra_config(
    std::map<std::string, std::string> &kernel_params, std::map<std::string, std::string> &extra_config);

}  // namespace tests
