// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_comparison_config.h"

#include "llk_verify.h"

using namespace tests;

void tests::read_comparison_config_from_yaml(ComparisonConfig& comparison_config, YAML::Node& test_descriptor) {
    if ((test_descriptor["test-config"]) and (test_descriptor["test-config"]["comparison-config"])) {
        auto yaml_comparison_config = test_descriptor["test-config"]["comparison-config"];
        if (yaml_comparison_config["dropout-percentage"]) {
            comparison_config.dropout_percentage = yaml_comparison_config["dropout-percentage"].as<float>();
        }
        if (yaml_comparison_config["dropout-percentage-error-threshold"]) {
            comparison_config.dropout_percentage_error_threshold =
                yaml_comparison_config["dropout-percentage-error-threshold"].as<float>();
        }
        if (yaml_comparison_config["minimum-pcc"]) {
            comparison_config.minimum_pcc = yaml_comparison_config["minimum-pcc"].as<float>();
        }
        if (yaml_comparison_config["minimum-match-ratio"]) {
            comparison_config.minimum_match_ratio = yaml_comparison_config["minimum-match-ratio"].as<float>();
        }
        if (yaml_comparison_config["match-ratio-error-threshold"]) {
            comparison_config.match_ratio_error_threshold =
                yaml_comparison_config["match-ratio-error-threshold"].as<float>();
        }
        if (yaml_comparison_config["skip-dropout-check"]) {
            comparison_config.skip_dropout_check = yaml_comparison_config["skip-dropout-check"].as<bool>();
        }
        if (yaml_comparison_config["skip-pcc-check"]) {
            comparison_config.skip_pcc_check = yaml_comparison_config["skip-pcc-check"].as<bool>();
        }
        if (yaml_comparison_config["skip-match-ratio-check"]) {
            comparison_config.skip_match_ratio_check = yaml_comparison_config["skip-match-ratio-check"].as<bool>();
        }
    }
}

YAML::Emitter& operator<<(YAML::Emitter& out, const tests::ComparisonConfig& comparison_config) {
    out << YAML::Key << "comparison-config";
    out << YAML::BeginMap;
    out << YAML::Key << "dropout-percentage";
    out << comparison_config.dropout_percentage;
    out << YAML::Key << "dropout-percentage-error-threshold";
    out << comparison_config.dropout_percentage_error_threshold;
    out << YAML::Key << "minimum-pcc";
    out << comparison_config.minimum_pcc;
    out << YAML::Key << "minimum-match-ratio";
    out << comparison_config.minimum_match_ratio;
    out << YAML::Key << "match-ratio-error-threshold";
    out << comparison_config.match_ratio_error_threshold;
    out << YAML::Key << "skip-dropout-check";
    out << comparison_config.skip_dropout_check;
    out << YAML::Key << "skip-pcc-check";
    out << comparison_config.skip_pcc_check;
    out << YAML::Key << "skip-match-ratio-check";
    out << comparison_config.skip_match_ratio_check;
    out << YAML::EndMap;
    return out;
}