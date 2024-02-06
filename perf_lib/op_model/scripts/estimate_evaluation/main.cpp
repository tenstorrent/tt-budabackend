// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "evaluate_op_model_estimates.hpp"
#include "netlist/tt_backend_api_types.hpp"
#include "perf_lib/op_model/op_model.hpp"
#include "utils/logger.hpp"

// This program loads results of the perf sweep run for a specified op and arch, generates estimate
// for each of the examples and outputs a file where each row contains the data from the perf sweep run
// together with the estimate and ratio between estimate and real runtime.

int main(int argc, char* argv[]) {
    if (argc < 7) {
        log_fatal(
            "Required arguments: perf sweep results file, output file, op type, op name, architecture, param version");
    }

    std::string input_file_name = argv[1];
    std::string output_file_name = argv[2];
    std::string op_type = argv[3];
    std::string op_name = argv[4];

    std::string arch_name = argv[5];
    std::uint32_t param_version = static_cast<uint32_t>(std::stoi(argv[6]));
    std::string sparse_formula = "";

    log_assert(
        op_type == "binary" || op_type == "unary" || op_name == "matmul_sparse",
        "Estimate evaluation supported only for the following ops: binary ops, matmul_sparse.");

    if (op_name == "matmul_sparse") {
        if (argc < 8) {
            log_fatal("Required argument: formula for calculating sparse matmul estimate (v1/v2)");
        }

        sparse_formula = argv[7];
    }

    std::ifstream input_file(input_file_name);
    if (!input_file.is_open()) {
        log_fatal("Failed to open {}.", input_file_name);
    }

    std::vector<std::string> columns = tt::load_perf_sweep_result_columns(input_file);
    std::vector<std::unordered_map<std::string, std::string>> perf_sweep_results =
        tt::load_perf_sweep_results(input_file, columns);

    std::ofstream output_file(output_file_name);
    if (!output_file.is_open()) {
        log_fatal("Failed to open {}.", output_file_name);
    }

    for (const auto& column : columns) {
        output_file << column << ",";
    }

    output_file << "estimate,ratio" << std::endl;

    for (const auto& perf_sweep_result : perf_sweep_results) {
        float runtime = tt::calculate_runtime(perf_sweep_result, op_name);
        uint32_t estimate = tt::get_op_cycles_for_perf_sweep_result(
            perf_sweep_result, op_type, op_name, arch_name, param_version, sparse_formula);
        float ratio = estimate / runtime;

        for (const auto& column : columns) {
            output_file << perf_sweep_result.at(column) << ",";
        }
        output_file << estimate << "," << ratio << std::endl;
    }

    input_file.close();
    output_file.close();

    return 0;
}
