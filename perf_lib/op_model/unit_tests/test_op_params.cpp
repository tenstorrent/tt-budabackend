// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include "gtest/gtest.h"
#include "test_unit_common.hpp"

TEST(OpParams, OpModelParamCorrectness) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 1);

    YAML::Node yaml_data = YAML::LoadFile(param_dir + "/params_v1.yaml");
    uint32_t expected, observed;

    expected = yaml_data["foo"]["bar"].as<uint32_t>();
    observed = op_params.get_param("foo", 1, "bar");
    EXPECT_EQ(expected, observed);

    expected = yaml_data["foo"]["baz"].as<uint32_t>();
    observed = op_params.get_param("foo", 1, "baz");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, UndefinedParamDefaults) {
    std::string param_dir = test_path() + "yaml/test_defaults";
    auto op_params = tt::OpModelParams(param_dir, 1);

    YAML::Node yaml_data = YAML::LoadFile(param_dir + "/params_v1.yaml");
    uint32_t expected, observed;

    // Undefined op type should default to "default"
    expected = yaml_data["default"]["tile_weights"].as<uint32_t>();
    observed = op_params.get_param("this_op_does_not_exist", 1, "tile_weights");
    EXPECT_EQ(expected, observed);

    // Undefined param type should default to "default"
    expected = yaml_data["default"]["default"].as<uint32_t>();
    observed = op_params.get_param("this_op_does_not_exist_either", 1, "made_up_name");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, OpModelParamVersions) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    YAML::Node yaml_data_v1 = YAML::LoadFile(param_dir + "/params_v1.yaml");
    YAML::Node yaml_data_v2 = YAML::LoadFile(param_dir + "/params_v2.yaml");
    YAML::Node yaml_data_v3 = YAML::LoadFile(param_dir + "/params_v3.yaml");

    uint32_t expected, observed;

    // foo - exists in all 3 versions
    expected = yaml_data_v1["foo"]["bar"].as<uint32_t>();
    observed = op_params.get_param("foo", 1, "bar");
    EXPECT_EQ(expected, observed);

    expected = yaml_data_v2["foo"]["bar"].as<uint32_t>();
    observed = op_params.get_param("foo", 2, "bar");
    EXPECT_EQ(expected, observed);

    expected = yaml_data_v3["foo"]["bar"].as<uint32_t>();
    observed = op_params.get_param("foo", 3, "bar");
    EXPECT_EQ(expected, observed);

    // new_op - added in version 2
    expected = yaml_data_v1["default"]["baz"].as<uint32_t>();
    observed = op_params.get_param("new_op", 1, "baz");
    EXPECT_EQ(expected, observed);

    expected = yaml_data_v2["default"]["baz"].as<uint32_t>();
    observed = op_params.get_param("new_op", 2, "baz");
    EXPECT_EQ(expected, observed);

    expected = yaml_data_v3["new_op"]["baz"].as<uint32_t>();
    observed = op_params.get_param("new_op", 3, "baz");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, OpModelParamNonExistingVersion) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    std::ifstream params_file(param_dir + "/params_v5.yaml");
    // Make sure the file actually doesn't exist as this is the case we are testing
    ASSERT_FALSE(params_file.is_open());

    std::uint32_t observed = op_params.get_param("foo", 5, "bar");
    EXPECT_EQ(observed, 0);
}

TEST(OpParams, OpModelInvalidVersion) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    std::uint32_t observed = op_params.get_param("foo", 0, "bar");
    EXPECT_EQ(observed, 0);
}

TEST(OpParams, OpModelParamNoInitialVersion) {
    std::string param_dir = test_path() + "yaml/test_versions/test_no_v1";
    EXPECT_THROW((tt::OpModelParams{param_dir, 2}), std::runtime_error);
}

TEST(OpParams, OpModelParamSkippedVersion) {
    std::string param_dir = test_path() + "yaml/test_versions/test_skipped_version";
    EXPECT_THROW((tt::OpModelParams{param_dir, 3}), std::runtime_error);
}

TEST(OpParams, OpModelParamFileNameTypo) {
    std::string param_dir = test_path() + "yaml/test_versions/test_file_name_typo";
    EXPECT_THROW((tt::OpModelParams{param_dir, 3}), std::runtime_error);
}