// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include "gtest/gtest.h"
#include "netlist/tt_backend_api_types.hpp"
#include "op_model.hpp"
#include "test_unit_common.hpp"
#include "perf_lib/op_model/scripts/estimate_evaluation/evaluate_op_model_estimates.hpp"

/* Test suite for sparse matmul op model
   expected cycles are from silicon measurements on grayskull
   https://tenstorrent.sharepoint.com/:x:/s/Software/EYSdBDWlMnxDoniKinpObWkB8YugHIP9O-4S1stLsV123g?e=otfrix
**/

TEST(OpModelSparseMatmul, ResnetOp0) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "grayskull",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 14,
        .mblock_m = 28,
        .mblock_n = 1,
        .ublock_rt = 7,
        .ublock_ct = 1,
        .mblock_k = 2,
        .sparse_indices = 7041,
    };
    float rtol = 0.20;
    std::uint32_t measured_silicon_cycles = 678480;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}

TEST(OpModelSparseMatmul, ResnetOp1) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "grayskull",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 18,
        .mblock_n = 2,
        .ublock_rt = 7,
        .ublock_ct = 1,
        .mblock_k = 28,
        .sparse_indices = 375,
    };
    float rtol = 0.40; // TODO: tighten this up, the current model error is really high
    std::uint32_t measured_silicon_cycles = 176619;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}


TEST(OpModelSparseMatmul, ResnetOp2) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "grayskull",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 18,
        .mblock_n = 2,
        .ublock_rt = 7,
        .ublock_ct = 1,
        .mblock_k = 7,
        .sparse_indices = 247,
    };
    float rtol = 0.20;
    std::uint32_t measured_silicon_cycles = 83399;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}

TEST(OpModelSparseMatmul, ResnetOp3) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "grayskull",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 45,
        .mblock_n = 1,
        .ublock_rt = 5,
        .ublock_ct = 1,
        .mblock_k = 7,
        .sparse_indices = 785,
    };
    float rtol = 0.20;
    std::uint32_t measured_silicon_cycles = 112465;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}

TEST(OpModelSparseMatmul, ResnetOp4) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "grayskull",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 25,
        .mblock_n = 1,
        .ublock_rt = 1,
        .ublock_ct = 8,
        .mblock_k = 14,
        .sparse_indices = 88,
    };
    float rtol = 0.30; // TODO: tighten this up, the current model error is really high
    std::uint32_t measured_silicon_cycles = 80734;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}


TEST(OpModelSparseMatmulV2, ResnetOp1) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "wormhole_b0",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 4,
        .mblock_m = 49,
        .mblock_n = 1,
        .ublock_rt = 8,
        .ublock_ct = 1,
        .mblock_k = 8,
        .ublock_kt = 49,
        .sparse_indices = 3109,
        .sparse_nz_ublocks = 235,
        .sparse_nz_strips = 11,
    };
    float rtol = 0.20; 
    std::uint32_t measured_silicon_cycles = 314284;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}


TEST(OpModelSparseMatmulV2, ResnetOp2) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "wormhole_b0",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 63,
        .mblock_n = 1,
        .ublock_rt = 7,
        .ublock_ct = 1,
        .mblock_k = 49,
        .ublock_kt = 8,
        .sparse_indices = 1267,
        .sparse_nz_ublocks = 249,
        .sparse_nz_strips = 49,
    };
    float rtol = 0.20; 
    std::uint32_t measured_silicon_cycles = 443438;
    std::uint32_t sparse_formula_cycles = tt::OpModel::get_op_cycles(op_desc);
    EXPECT_GT(float(sparse_formula_cycles), (1-rtol)*float(measured_silicon_cycles));
    EXPECT_LT(float(sparse_formula_cycles), (1+rtol)*float(measured_silicon_cycles));
}

TEST(OpModelSparseMatmulV2, PerfSweepCheck) {
    std::string input_file_name =
        tt::buda_home() + "/perf_lib/op_model/scripts/perf_data/wormhole_b0/matmul-sparse-op-performance.csv";
    std::ifstream input_file(input_file_name);
    ASSERT_TRUE(input_file.is_open());

    std::vector<std::string> columns = tt::load_perf_sweep_result_columns(input_file);
    std::vector<std::unordered_map<std::string, std::string>> perf_sweep_results =
        tt::load_perf_sweep_results(input_file, columns);

    input_file.close();

    std::uint32_t latest_param_version = tt::OpModel::VERSIONS.at(tt::ARCH::WORMHOLE_B0);

    for (const auto& perf_sweep_result : perf_sweep_results) {
        float runtime = tt::calculate_runtime(perf_sweep_result, "matmul_sparse");
        uint32_t estimate = tt::get_op_cycles_for_perf_sweep_result(
            perf_sweep_result, "matmul", "matmul_sparse", "wormhole_b0", latest_param_version, "v2");
        float ratio = float(estimate) / runtime;

        EXPECT_GT(ratio, 0.71);
        EXPECT_LT(ratio, 1.73);
    }
}

TEST(OpModelSparseMatmulV2, UnsupportedFormatFidelityCombination) {
    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .arch = "wormhole_b0",
        .data_format = tt::DataFormat::Float16,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 63,
        .mblock_n = 1,
        .ublock_rt = 7,
        .ublock_ct = 1,
        .mblock_k = 49,
        .ublock_kt = 8,
        .sparse_indices = 1267,
        .sparse_nz_ublocks = 249,
        .sparse_nz_strips = 49,
    };
    auto v2_result = tt::OpModel::get_op_cycles(op_desc);

    op_desc.sparse_nz_strips = -1;
    op_desc.sparse_nz_ublocks = -1;
    auto v1_result = tt::OpModel::get_op_cycles(op_desc);

    // Unsupported format/fidelity combination for V2 formula; fallback to v1
    EXPECT_EQ(v2_result, v1_result);
}
