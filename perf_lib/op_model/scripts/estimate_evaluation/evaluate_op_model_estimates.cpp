// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "netlist/tt_backend_api_types.hpp"
#include "perf_lib/op_model/op_model.hpp"
#include "utils/logger.hpp"

namespace tt {

std::vector<std::string> load_perf_sweep_result_columns(std::ifstream& input_file) {
    std::vector<std::string> columns;

    std::string header_line;
    if (std::getline(input_file, header_line)) {
        std::vector<std::string> column_names;
        std::stringstream header_ss(header_line);
        std::string column_name;

        while (std::getline(header_ss, column_name, ',')) {
            if (column_name.back() == '\r') {
                column_name.pop_back();
            }
            columns.push_back(column_name);
        }
    } else {
        log_fatal("Perf sweep results file empty!");
    }

    return columns;
}

std::vector<std::unordered_map<std::string, std::string>> load_perf_sweep_results(std::ifstream& input_file, std::vector<std::string>& columns) {
    std::vector<std::unordered_map<std::string, std::string>> results;

    std::string input_line;
    while (std::getline(input_file, input_line)) {
        std::unordered_map<std::string, std::string> perf_sweep_result;
        std::istringstream row_ss(input_line);
        std::string field;
        size_t index = 0;

        while (std::getline(row_ss, field, ',')) {
            if (field.back() == '\r') {
                field.pop_back();
            }
            perf_sweep_result[columns[index]] = field;
            index++;
        }

        results.push_back(perf_sweep_result);
    } 

    return results;
}

uint32_t get_op_cycles_for_perf_sweep_result(
    const std::unordered_map<std::string, std::string>& perf_sweep_result,
    const std::string& op_type,
    const std::string& op_name,
    const std::string& arch,
    std::uint32_t param_version,
    const std::string& sparse_formula) {
    log_assert(
        op_type == "unary" || op_type == "binary" || op_name == "matmul_sparse",
        "Estimate evaluation supported only for the following ops: unary ops, binary ops and matmul sparse.");

    std::string fidelity_string = perf_sweep_result.at("math_fidelity");
    MathFidelity fidelity = MathFidelity::Invalid;
    if (fidelity_string == "LoFi") {
        fidelity = MathFidelity::LoFi;
    } else if (fidelity_string == "HiFi2") {
        fidelity = MathFidelity::HiFi2;
    } else if (fidelity_string == "HiFi3") {
        fidelity = MathFidelity::HiFi3;
    } else if (fidelity_string == "HiFi4") {
        fidelity = MathFidelity::HiFi4;
    } else {
        log_fatal("Math Fidelity not supported in netlist");
    }

    tt_op_model_desc op_desc = {
        .type = op_name,
        .arch = arch,
        .math_fidelity = fidelity,
        .t = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("output_t"))),
        .mblock_m = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("output_mb_r"))),
        .mblock_n = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("output_mb_c"))),
        .ublock_rt = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("output_ub_r"))),
        .ublock_ct = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("output_ub_c"))),
        .version = param_version,
    };

    if (op_name == "matmul_sparse") {
        log_assert(sparse_formula != "", "Formula version must be passed for sparse estimate evaluation - v1 or v2.");

        op_desc.type = "matmul";
        op_desc.data_format = get_format_from_string(perf_sweep_result.at("input1_df"));
        op_desc.mblock_k = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("mblock_k")));
        op_desc.ublock_kt = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("ublock_kt")));
        op_desc.sparse_indices = static_cast<uint32_t>(std::stoi(perf_sweep_result.at("nz_tiles")));
        op_desc.sparse_nz_ublocks = sparse_formula == "v2" ? std::stoi(perf_sweep_result.at("nz_ublocks")) : -1;
        op_desc.sparse_nz_strips = sparse_formula == "v2" ? std::stoi(perf_sweep_result.at("nz_strips")) : -1;
    } else {
        op_desc.data_format = get_format_from_string(perf_sweep_result.at("input0_df"));
        if (op_name.find("approx") != std::string::npos) {
            size_t suffix_position = op_name.find("_approx");
            op_desc.type = op_name.substr(0, suffix_position);
            op_desc.approx_mode = true;
        }

        if (perf_sweep_result.find("Vector-Mode") != perf_sweep_result.end()) {
            op_desc.vector_mode = get_sfpu_vector_mode_from_string(perf_sweep_result.at("Vector-Mode"));
        }
    }

    return OpModel::get_op_cycles(op_desc);
}

float calculate_runtime(const std::unordered_map<std::string, std::string>& perf_sweep_result, const std::string& op_name) {
    if (op_name == "matmul_sparse") {
        // for sparse matmul, we collect verbose sweep output and get runtime of the last input,
        // since there are non-negligible waits between the inputs that we don't want to account for
        return std::stof(perf_sweep_result.at("runtime"));
    } else {
        // for other ops, we divide the total runtime with the number of inputs
        const float total_runtime = std::stof(perf_sweep_result.at("total-runtime"));
        const float first_input_id = std::stof(perf_sweep_result.at("first-input-idx-recorded"));
        const float last_input_id = std::stof(perf_sweep_result.at("last-input-idx-recorded"));
        return total_runtime / (last_input_id - first_input_id + 1);
    }
}

}
