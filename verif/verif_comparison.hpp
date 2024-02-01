// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tuple>

#include "model/tensor.hpp"
#include "verif_comparison_types.hpp"
#include "verif_utils.hpp"

using namespace std;

namespace verif::comparison {
ComparisonVerbosity get_comparison_verbosity(string input);
ComparisonType get_comparison_type(string input);
//! Check if Tensor dims are equal :)
bool are_tensor_dims_equal(const tt_tensor &lhs, const tt_tensor &rhs);
//! Checks if the data is equivalent with a combination of allclose/pcc or exact match
bool compare_tensor_data(const tt_tensor &lhs, const tt_tensor &rhs, const VerifComparisonConfig &comp);
//! Checks if the data is allclose similar to numpy implementation and returns result/percent_close
std::tuple<bool, double> allclose(
    const tt_tile &lhs,
    const tt_tile &rhs,
    const double rtol,
    const double atol,
    double pct_matched,
    bool print_diffs,
    const std::pair<int, int> &check_tile_rows_range,
    const std::pair<int, int> &check_tile_cols_range);

//! Checks if the data's correlation using PCC returns result/PCC
std::tuple<bool, double> pcc_compare(
    const tt_tile &lhs,
    const tt_tile &rhs,
    const double rtol,
    const double atol,
    const double pass_pcc,
    const std::pair<int, int> &check_tile_rows_range,
    const std::pair<int, int> &check_tile_cols_range);

double inf_to_num(double in);
bool inf_compare(float a, float b);

/*!
 *  Function to compare tensors
 *     -- Use LOGGER_LEVEL=Debug to see tile by tile results
 *     -- Will only error and return on first tile mismatched in tensor -- Will not abort execution
 */
bool compare_tensors(vector<float>& lhs, vector<float>& rhs, const VerifComparisonConfig &comp, tt_dram_io_desc q_desc);
bool compare_tensors(const tt_tensor &lhs, const tt_tensor &rhs, const VerifComparisonConfig &comp);
bool compare_tensors_exact(const tt_tensor &lhs, const tt_tensor &rhs);
bool compare_flat_tensors(vector<float>& lhs, vector<float>& rhs, const VerifComparisonConfig &comp);
//! Will read only the default config from the yaml file
VerifComparisonConfig read_from_yaml_file(const std::string &filepath);
//! Will read default config + any overrides which are keyed off a tag
std::unordered_map<std::string, VerifComparisonConfig> read_configs_map_from_yaml_file(const std::string &filepath);
//! Will read default config + any overrides which are keyed off a tag, a defaults file can be used to fill in missing
//! configs
std::unordered_map<std::string, VerifComparisonConfig> read_configs_map_with_defaults(
    const std::string &filepath, const std::string &default_filepath);
//! Returns a default config if key doesn't exist
VerifComparisonConfig get_config(
    const std::string key, const std::unordered_map<std::string, VerifComparisonConfig> &configs);

}  // namespace verif::comparison
