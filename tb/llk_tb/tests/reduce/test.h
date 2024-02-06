// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test_args.h"
#include "test_config.h"
#include "yaml-cpp/yaml.h"

namespace tests {
namespace reduce {
void generate_stimulus_and_expected_tensors(
    tests::TestConfig test_config, llk::Tensor &input0, llk::Tensor &expected_output0);
void update_kernel_parameter_mapping(tests::TestConfig &config);
std::map<std::string, tests::TestConfig> generate_test_configs(
    tests::TestArgs args, std::string test_name, YAML::Node &test_descriptor);
std::map<std::string, bool> test_main(tests::TestArgs args);
void test_loop_body(string test_name, string config_string, TestConfig test_config, map<string, bool> &test_results);
}  // namespace reduce
}  // namespace tests