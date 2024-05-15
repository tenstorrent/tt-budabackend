// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdlib>

#include "utils/hash.h"
#include "yaml-cpp/yaml.h"

namespace tests {

struct ComparisonConfig {
    //! dropout_percentage - certain percentage of the calculated values can be 0 and not match
    float dropout_percentage = 0.0f;
    //! dropout_percentage_error_threshold - how many percent diff can calculated dropout percentage be from expected
    float dropout_percentage_error_threshold = 0.05f;

    //! minimum_pcc - lowest pcc value can be before we error out
    float minimum_pcc = 0.90f;  // -1 < x < 1
    //! minimum_match_ratio - lowest match ratio can be before we error out
    float minimum_match_ratio = 0.80f;  // 0 < x < 1
    //! match_ratio_error_threshold - How many percentage points can calculated different from expected before
    //! considered not a match
    float match_ratio_error_threshold = 0.30f;  // 0 < x < 1

    bool skip_dropout_check = true;
    bool skip_pcc_check = false;
    bool skip_match_ratio_check = false;
};

void read_comparison_config_from_yaml(ComparisonConfig& comparison_config, YAML::Node& test_descriptor);
}  // namespace tests

YAML::Emitter& operator<<(YAML::Emitter& out, const tests::ComparisonConfig& comparison_config);

namespace std {
template <>
struct hash<tests::ComparisonConfig> {
    inline size_t operator()(const tests::ComparisonConfig& comparison_config) const {
        // size_t value = your hash computations over x
        size_t current_hash = comparison_config.dropout_percentage;
        ::hash_combine(current_hash, comparison_config.dropout_percentage_error_threshold);
        ::hash_combine(current_hash, comparison_config.minimum_pcc);
        ::hash_combine(current_hash, comparison_config.minimum_match_ratio);
        ::hash_combine(current_hash, comparison_config.match_ratio_error_threshold);
        ::hash_combine(current_hash, comparison_config.skip_dropout_check);
        ::hash_combine(current_hash, comparison_config.skip_pcc_check);
        ::hash_combine(current_hash, comparison_config.skip_match_ratio_check);
        return current_hash;
    }
};
}  // namespace std