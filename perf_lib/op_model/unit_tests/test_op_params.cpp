// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include "gtest/gtest.h"
#include "test_unit_common.hpp"

TEST(OpParams, OpModelParamCorrectness) {
    std::string yaml_file = test_path() + "yaml/test_params.yaml";
    auto op_params = tt::OpModelParams(yaml_file);

    YAML::Node yaml_data = YAML::LoadFile(yaml_file);
    uint32_t expected, observed;

    expected = yaml_data["foo"]["bar"].as<uint32_t>();
    observed = op_params.get_param("foo", "bar");
    EXPECT_EQ(expected, observed);

    expected = yaml_data["foo"]["baz"].as<uint32_t>();
    observed = op_params.get_param("foo", "baz");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, UndefinedParamDefaults) {
    std::string yaml_file = test_path() + "yaml/test_defaults.yaml";
    auto op_params = tt::OpModelParams(yaml_file);

    YAML::Node yaml_data = YAML::LoadFile(yaml_file);
    uint32_t expected, observed;

    // Undefined op type should default to "default"
    expected = yaml_data["default"]["tile_weights"].as<uint32_t>();
    observed = op_params.get_param("this_op_does_not_exist", "tile_weights");
    EXPECT_EQ(expected, observed);

    // Undefined param type should default to "default"
    expected = yaml_data["default"]["default"].as<uint32_t>();
    observed = op_params.get_param("this_op_does_not_exist_either", "made_up_name");
    EXPECT_EQ(expected, observed);
}

