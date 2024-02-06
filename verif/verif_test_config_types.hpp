// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "verif_stimulus_types.hpp"
#include "verif_comparison_types.hpp"

struct VerifTestConfig {
    std::unordered_map<std::string, std::string> test_args = {};
    std::unordered_map<std::string, VerifComparisonConfig> comparison_configs = {};
    std::unordered_map<std::string, VerifStimulusConfig> stimulus_configs = {};
    std::unordered_map<std::string, std::vector<std::string>> io_config = {};
};
