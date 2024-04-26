// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt {

std::vector<std::string> load_perf_sweep_result_columns(std::ifstream& input_file);
std::vector<std::unordered_map<std::string, std::string>> load_perf_sweep_results(
    std::ifstream& input_file, std::vector<std::string>& columns);
uint32_t get_op_cycles_for_perf_sweep_result(
    const std::unordered_map<std::string, std::string>& perf_sweep_result,
    const std::string& op_type,
    const std::string& op_name,
    const std::string& arch,
    std::uint32_t param_version,
    const std::string& sparse_formula);
float calculate_runtime(
    const std::unordered_map<std::string, std::string>& perf_sweep_result, const std::string& op_name);

}  // namespace tt
