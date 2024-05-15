// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_set>

#include "common/model/constants.hpp"

enum class ComparisonType {
    Invalid,     // Default type is invalid -- Must be set explicitly
    Exact,       // Shape and value matches exactly
    AllClose,    // AllClose uses threshold matching
    AllCloseHw,  // Uses PCC and Match Ratio
};

enum class ComparisonVerbosity {
    Concise = 0,   // Uses PCC and Match Ratio
    AllFails = 1,  // Shows all failure diffs
    Verbose = 2,   // Show all Diffs (Correct or not)
};

enum class ComparisonMethod {
    TilizedTensor = 0, // Tilize output tensors and compare 32x32 tiles
    FlatTensor = 1, // Compare 32x32 horizontal slices of raw (untilized) outputs
};

struct VerifComparisonConfig {
    bool user_configured = false;
    ComparisonType type = ComparisonType::Invalid;
    ComparisonVerbosity verbosity = ComparisonVerbosity::Concise;
    ComparisonMethod method = ComparisonMethod::FlatTensor;
    double atol = tt::constants::DEFAULT_ATOL;
    double rtol = tt::constants::DEFAULT_RTOL;
    double check_pct = tt::constants::DEFAULT_PCT_MATCHED;
    double check_pcc = tt::constants::DEFAULT_PCC_THRESH;
    pair<int, int> check_tile_rows_range = pair<int, int> (0, tt::constants::TILE_HEIGHT - 1);
    pair<int, int> check_tile_cols_range = pair<int, int> (0, tt::constants::TILE_WIDTH  - 1);
    std::unordered_set<std::string> check_dims_only_names = {};
    std::unordered_set<std::string> check_dims_only_types = {};
    std::string lhs_header = "Golden";
    std::string rhs_header = "Observed";
    VerifComparisonConfig(ComparisonType type, ComparisonVerbosity verbosity) : type(type), verbosity(verbosity){};
    VerifComparisonConfig(){};
};
