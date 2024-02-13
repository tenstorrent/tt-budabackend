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

    tt::tt_op_model_desc op_desc = {
        .type = "nop",
        .t = 4,
        .mblock_m = 2,
        .mblock_n = 8,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 8,
        .version = 1
    };
    expected = yaml_data["nop"]["base"]["bar"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "bar");
    EXPECT_EQ(expected, observed);

    op_desc.type = "add";
    expected = yaml_data["add"]["base"]["baz"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "baz");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, OpModelParamVersions) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    YAML::Node yaml_data_v1 = YAML::LoadFile(param_dir + "/params_v1.yaml");
    YAML::Node yaml_data_v2 = YAML::LoadFile(param_dir + "/params_v2.yaml");
    YAML::Node yaml_data_v3 = YAML::LoadFile(param_dir + "/params_v3.yaml");

    uint32_t expected, observed;

    tt::tt_op_model_desc op_desc = {
        .type = "add",
        .t = 4,
        .mblock_m = 2,
        .mblock_n = 8,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 8,
        .version = 1
    };

    // add - exists in all 3 versions
    expected = yaml_data_v1["add"]["base"]["bar"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "bar");
    EXPECT_EQ(expected, observed);

    op_desc.version = 2;
    expected = yaml_data_v2["add"]["base"]["bar"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "bar");
    EXPECT_EQ(expected, observed);

    op_desc.version = 3;
    expected = yaml_data_v3["add"]["base"]["bar"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "bar");
    EXPECT_EQ(expected, observed);

    // exp - added in version 3
    op_desc.type = "exp";
    op_desc.version = 1;
    expected = yaml_data_v1["nop"]["base"]["baz"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "baz");
    EXPECT_EQ(expected, observed);

    op_desc.version = 2;
    expected = yaml_data_v2["nop"]["base"]["baz"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "baz");
    EXPECT_EQ(expected, observed);

    op_desc.version = 3;
    expected = yaml_data_v3["exp"]["base"]["baz"].as<uint32_t>();
    observed = op_params.get_param(op_desc, "baz");
    EXPECT_EQ(expected, observed);
}

TEST(OpParams, OpModelParamNonExistingVersion) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    std::ifstream params_file(param_dir + "/params_v5.yaml");
    // Make sure the file actually doesn't exist as this is the case we are testing
    ASSERT_FALSE(params_file.is_open());

    tt::tt_op_model_desc op_desc = {
        .type = "add",
        .t = 4,
        .mblock_m = 2,
        .mblock_n = 8,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 8,
        .version = 5
    };

    std::uint32_t observed = op_params.get_param(op_desc, "bar");
    EXPECT_EQ(observed, 0);
}

TEST(OpParams, OpModelParamInvalidVersion) {
    std::string param_dir = test_path() + "yaml/test_params";
    auto op_params = tt::OpModelParams(param_dir, 3);

    tt::tt_op_model_desc op_desc = {
        .type = "add",
        .t = 4,
        .mblock_m = 2,
        .mblock_n = 8,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 8,
        .version = 0
    };

    std::uint32_t observed = op_params.get_param(op_desc, "bar");
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
