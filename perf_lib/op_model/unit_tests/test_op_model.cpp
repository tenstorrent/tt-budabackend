// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include "gtest/gtest.h"
#include "netlist/tt_backend_api_types.hpp"
#include "op_model.hpp"
#include "test_unit_common.hpp"
#include "perf_lib/op_model/scripts/estimate_evaluation/evaluate_op_model_estimates.hpp"

void check_estimates_with_perf_sweep_data(
    const tt::ARCH arch,
    const std::string op_type,
    const std::string& op_name,
    const std::string& perf_sweep_results_file_name,
    float lower_bound,
    float upper_bound) {
    std::string op_name_dash = op_name;

    for (size_t char_index = 0; char_index < op_name_dash.size(); char_index++) {
        if (op_name_dash[char_index] == '_') {
            op_name_dash[char_index] = '-';
        }
    }

    // tests validating estimates on generated data should always use latest parameter version;
    // we won't be keeping generated data for older parameter versions
    std::uint32_t latest_param_version= tt::OpModel::VERSIONS.at(arch);
    std::string arch_name = get_string_lowercase(arch);

    std::ifstream input_file(
        tt::buda_home() + "/perf_lib/op_model/scripts/perf_data/" + arch_name + "/" + perf_sweep_results_file_name);
    ASSERT_TRUE(input_file.is_open());

    std::vector<std::string> columns = tt::load_perf_sweep_result_columns(input_file);
    std::vector<std::unordered_map<std::string, std::string>> perf_sweep_results =
        tt::load_perf_sweep_results(input_file, columns);

    input_file.close();

    for (const auto& perf_sweep_result : perf_sweep_results) {
        float runtime = tt::calculate_runtime(perf_sweep_result, op_name);
        uint32_t estimate = tt::get_op_cycles_for_perf_sweep_result(perf_sweep_result, op_type, op_name, arch_name, latest_param_version, "");
        float ratio = float(estimate) / runtime;

        EXPECT_GT(ratio, lower_bound);
        EXPECT_LT(ratio, upper_bound);
    }
}

TEST(OpModel, UnaryCycles) {
    std::vector<std::string> arch_names = {"grayskull", "wormhole_b0", "blackhole"};
    std::vector<std::string> ops = {
        "exp",
        "gelu",
        "gelu_derivative",
        "log",
        "nop",
        "reciprocal",
        "sigmoid",
        "sqrt",
        "lrelu",
        "abs",
        "tanh",
        "square",
        "sine",
        "cosine",
        "dropout"};

    tt::tt_op_model_desc op_desc = {
        .data_format = tt::DataFormat::Float32,
        .math_fidelity = tt::MathFidelity::HiFi4,
        .t = 2,
        .mblock_m = 4,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 2,
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        for (auto op : ops) {
            log_info(tt::LogTest, "Testing arch: {}, op: {}", arch, op);
            op_desc.type = op;
            op_desc.approx_mode = false;
            auto w1 = tt::OpModel::get_op_param(op_desc, "ublock_weight");
            auto w2 = tt::OpModel::get_op_param(op_desc, "tile_weight");
            std::uint32_t observed = tt::OpModel::get_op_cycles(op_desc);
            std::uint32_t expected =
                op_desc.t * (op_desc.mblock_m * op_desc.mblock_n * w1 +
                            op_desc.mblock_m * op_desc.mblock_n * op_desc.ublock_rt * op_desc.ublock_ct * w2);
            EXPECT_GE(observed, expected) << "Expected low " << expected << " cycles, got " << observed;
            EXPECT_LT(observed, expected * 2) << "Expected high " << expected * 2 << " cycles, got " << observed;

            op_desc.approx_mode = true;
            std::uint32_t observed_approx = tt::OpModel::get_op_cycles(op_desc);
            EXPECT_LE(observed_approx, observed) << "Approx " << observed_approx << " cycles must be LTE to non-approx" << observed;
        }
    }
}

TEST(OpModel, ReduceOpCycles) {
    std::vector<std::string> op_attrs = {"r", "c"};
    std::vector<std::string> arch_names = {"grayskull", "wormhole_b0", "blackhole"};

    tt::tt_op_model_desc op_desc = {
        .type = "reduce",
        .data_format = tt::DataFormat::Bfp8,
        .math_fidelity = tt::MathFidelity::HiFi2,
        .t = 5,
        .mblock_m = 2,
        .mblock_n = 7,
        .ublock_rt = 3,
        .ublock_ct = 3,
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        for (const auto& op_attr : op_attrs) {
            op_desc.op_attr = op_attr;
            auto w1 = tt::OpModel::get_op_param(op_desc, "ublock_weight");
            auto w2 = tt::OpModel::get_op_param(op_desc, "tile_weight");
            std::uint32_t observed = tt::OpModel::get_op_cycles(op_desc);
            std::uint32_t expected = 
                op_desc.t * (op_desc.mblock_m * op_desc.mblock_n * w1 +
                             op_desc.mblock_m * op_desc.mblock_n * op_desc.ublock_rt * op_desc.ublock_ct * w2);
            EXPECT_GE(observed, expected) << "Expected low " << expected << " cycles, got " << observed;
            EXPECT_LT(observed, expected * 2) << "Expected high " << expected * 2 << " cycles, got " << observed;
        }
    }
}

TEST(OpModel, ReduceZOpCycles) {
    std::vector<std::string> op_attrs = {"z"};
    std::vector<std::string> arch_names = {"grayskull", "wormhole_b0", "blackhole"};

    tt::tt_op_model_desc op_desc = {
        .type = "reduce",
        .data_format = tt::DataFormat::Bfp8,
        .math_fidelity = tt::MathFidelity::HiFi2,
        .t = 1,
        .mblock_m = 2,
        .mblock_n = 7,
        .ublock_rt = 3,
        .ublock_ct = 3,
        .reduce_z = 25,
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        for (const auto& op_attr : op_attrs) {
            op_desc.op_attr = op_attr;
            auto w1 = tt::OpModel::get_op_param(op_desc, "ublock_weight");
            auto w2 = tt::OpModel::get_op_param(op_desc, "tile_weight");
            auto w3 = tt::OpModel::get_op_param(op_desc, "z_weight");
            std::uint32_t observed = tt::OpModel::get_op_cycles(op_desc);
            std::uint32_t expected = 
                op_desc.t * op_desc.reduce_z * (op_desc.mblock_m * op_desc.mblock_n * w1 +
                             op_desc.mblock_m * op_desc.mblock_n * op_desc.ublock_rt * op_desc.ublock_ct * w2 + w3);
            EXPECT_GE(observed, expected) << "Expected low " << expected << " cycles, got " << observed;
            EXPECT_LT(observed, expected * 2) << "Expected high " << expected * 2 << " cycles, got " << observed;
        }
    }
}

TEST(OpModel, MatmulOpCycles) {
    std::vector<std::string> arch_names = {"grayskull", "wormhole_b0", "blackhole"};
    std::unordered_map<std::string, std::uint32_t> math_weight = {
        {"grayskull", 36},
        {"wormhole_b0", 18},
        {"blackhole", 18},
    };

    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 4,
        .mblock_m = 2,
        .mblock_n = 8,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 8,
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        std::uint32_t math_cycles = math_weight.at(arch);
        std::uint32_t ublock_executions = op_desc.mblock_k * op_desc.mblock_m * op_desc.mblock_n * 
                                          op_desc.ublock_kt * op_desc.ublock_rt * op_desc.ublock_ct;
        std::uint32_t theoretical = op_desc.t * ublock_executions * math_cycles;

        op_desc.data_format = tt::DataFormat::Bfp8_b;
        std::uint32_t bfp8_cycles = tt::OpModel::get_op_cycles(op_desc);
        EXPECT_GE(bfp8_cycles, theoretical) << "Arch " << arch << " theoretical cycles " << theoretical << " cycles, got " << bfp8_cycles;
        EXPECT_LT(bfp8_cycles, theoretical * 2) << "Arch " << arch << "theoretical cycles 2x " << theoretical * 2 << " cycles, got " << bfp8_cycles;

        op_desc.data_format = tt::DataFormat::Float16;
        std::uint32_t fp16_cycles = tt::OpModel::get_op_cycles(op_desc);
        EXPECT_GE(fp16_cycles, bfp8_cycles) << "Arch " << arch << " bfp8 cycles " << bfp8_cycles << " cycles, got " << fp16_cycles;
        EXPECT_LT(fp16_cycles, bfp8_cycles * 2) << "Arch " << arch << "bfp8 cycles 2x " << bfp8_cycles * 2 << " cycles, got " << fp16_cycles;
    }
}

TEST(OpModel, MatmulOpV2Cycles) {
    std::vector<std::string> arch_names = {"wormhole_b0", "blackhole"};
    std::vector<std::pair<uint32_t, uint32_t>> k_dims = {{32, 1}, {16, 2}, {8, 4}, {4, 8}, {2, 16}, {1, 32}};
    std::vector<uint32_t> actual_cycles = {197027, 126764, 88479, 70939, 59613, 55571};

    float rtol = 0.10;

    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 3,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 4
    };


    for (const auto& arch : arch_names) {
        int test_id = 0;
        op_desc.arch = arch;
        for (const auto &k : k_dims) {
            op_desc.mblock_k = k.first;
            op_desc.ublock_kt = k.second;
            std::uint32_t op_cycles = tt::OpModel::get_op_cycles(op_desc);
            // std::cout << "k: " << k.first << " kt: " << k.second << " cycles: " << op_cycles << std::endl;
            EXPECT_NEAR(op_cycles, actual_cycles[test_id], actual_cycles[test_id] * rtol) << "Test " << test_id << " failed";
            test_id++;
        }
    }
}

TEST(OpModel, MatmulOpFidelityCycles) {
    std::vector<std::string> arch_names = {"grayskull", "wormhole_b0", "blackhole"};

    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .data_format = tt::DataFormat::Float16,
        .t = 1,
        .mblock_m = 3,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 4,
        .mblock_k = 4,
        .ublock_kt = 1
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        op_desc.math_fidelity = tt::MathFidelity::LoFi;
        std::uint32_t lofi_cycles = tt::OpModel::get_op_cycles(op_desc);

        op_desc.math_fidelity = tt::MathFidelity::HiFi2_A;
        std::uint32_t hifi2_a_cycles = tt::OpModel::get_op_cycles(op_desc);

        op_desc.math_fidelity = tt::MathFidelity::HiFi2_B;
        std::uint32_t hifi2_b_cycles = tt::OpModel::get_op_cycles(op_desc);

        op_desc.math_fidelity = tt::MathFidelity::HiFi3;
        std::uint32_t hifi3_cycles = tt::OpModel::get_op_cycles(op_desc);

        op_desc.math_fidelity = tt::MathFidelity::HiFi4;
        std::uint32_t hifi4_cycles = tt::OpModel::get_op_cycles(op_desc);

        EXPECT_EQ(hifi2_a_cycles, hifi2_b_cycles);
        EXPECT_LT(lofi_cycles, hifi2_a_cycles);
        EXPECT_LT(hifi2_a_cycles, hifi3_cycles);
        EXPECT_LT(hifi3_cycles, hifi4_cycles);
    }
}

TEST(OpModel, MatmulOpL1AccV2Cycles) {
    std::vector<std::string> arch_names = {"wormhole_b0", "blackhole"};
    std::vector<std::pair<uint32_t, uint32_t>> k_dims = {{32, 1}, {16, 2}, {8, 4}, {4, 4}};

    tt::tt_op_model_desc op_desc = {
        .type = "matmul",
        .data_format = tt::DataFormat::Bfp8_b,
        .math_fidelity = tt::MathFidelity::LoFi,
        .t = 1,
        .mblock_m = 8,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 4};

    // Test that L1 accumulate cycles are less than Dest accumulate
    // as long as m_k high, the L1 accumulate should be faster
     for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        int test_id = 0;
        for (const auto& k : k_dims) {
            op_desc.mblock_k = k.first;
            op_desc.ublock_kt = k.second;
            op_desc.l1_accumulate = false;
            std::uint32_t dest_acc_cycles = tt::OpModel::get_op_cycles(op_desc);
            op_desc.l1_accumulate = true;
            std::uint32_t l1_acc_cycles = tt::OpModel::get_op_cycles(op_desc);
            // std::cout << "k: " << k.first << " kt: " << k.second << " cycles: " << l1_acc_cycles << "," << dest_acc_cycles << std::endl;
            EXPECT_LE(l1_acc_cycles, dest_acc_cycles) << "Test " << test_id << " failed";
            test_id++;
        }
        // Test that L1 accumulate cycles for float16 and bfp8 are different using different models
        op_desc.l1_accumulate = true;
        for (const auto& k : k_dims) {
            op_desc.mblock_k = k.first;
            op_desc.ublock_kt = k.second;
            op_desc.data_format = tt::DataFormat::Float16_b;
            std::uint32_t fp16_cycles = tt::OpModel::get_op_cycles(op_desc);
            op_desc.data_format = tt::DataFormat::Bfp8_b;
            std::uint32_t bfp8_cycles = tt::OpModel::get_op_cycles(op_desc);
            EXPECT_NE(fp16_cycles, bfp8_cycles) << "Test " << test_id << " failed";
            test_id++;
        }
     }
}

TEST(OpModel, DepthwiseCycles) {
    std::vector<std::string> arch_names = {"wormhole_b0", "blackhole"};
    std::vector<std::string> ops = {"depthwise"};

    std::vector<std::uint32_t> v_mblock_m = {1, 2, 3};
    std::vector<std::pair<uint32_t, uint32_t>> k_dims = {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1}};
    std::vector<uint32_t> actual_cycles = {4453, 9036, 13358, 17917, 22554, 26969, 31365, 35823, 40866,
                                           9767, 17568, 26079, 34747, 44078, 52347, 61538, 69896, 78713,
                                           15507, 30360, 45530, 60700, 75929, 90784, 106438, 121948, 136513};

    tt::tt_op_model_desc op_desc = {
        .type = "depthwise",
        .data_format = tt::DataFormat::Float16,
        .math_fidelity = tt::MathFidelity::HiFi2,
        .t = 1,
        .mblock_n = 10,
        .ublock_rt = 1,
        .ublock_ct = 3,
    };

    float rtol = 0.10;

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        int test_id = 0;
        for (const auto mblock_m : v_mblock_m) {
            for (const auto &k : k_dims) {
                op_desc.mblock_k = k.first;
                op_desc.ublock_kt = k.second;
                op_desc.mblock_m = mblock_m;
                std::uint32_t op_cycles = tt::OpModel::get_op_cycles(op_desc);
                EXPECT_NEAR(op_cycles, actual_cycles[test_id], actual_cycles[test_id] * rtol) << "Test " << test_id << " failed";
                test_id++;
            }
        }
    }
}

TEST(OpModel, BinaryPerfSweepCheck) {
    std::vector<tt::ARCH> architectures = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE_B0};
    std::vector<std::vector<std::string>> ops_info = {
        {"binary", "add", "add-op-performance.csv"},
        {"binary", "subtract", "subtract-op-performance.csv"},
        {"binary", "multiply", "multiply-op-performance.csv"},
        {"binary", "maximum", "maximum-op-performance.csv"},
    };

    for (const auto& arch : architectures) {
        for (const auto& op_info : ops_info) {
            check_estimates_with_perf_sweep_data(arch, op_info[0], op_info[1], op_info[2], 0.8, 1.1);
        }
    }
}

TEST(OpModel, UnaryPerfSweepCheck) {
    std::vector<tt::ARCH> architectures = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE_B0};
    std::vector<std::vector<std::string>> ops_info = {
        {"unary", "exp_approx", "exp-approx-op-performance.csv"},
        {"unary", "gelu_derivative_approx", "gelu-derivative-approx-op-performance.csv"},
        {"unary", "reciprocal_approx", "reciprocal-approx-op-performance.csv"},
    };

    for (const auto& arch : architectures) {
        for (const auto& op_info : ops_info) {
            check_estimates_with_perf_sweep_data(arch, op_info[0], op_info[1], op_info[2], 0.8, 1.1);
        }
    }
}

TEST(OpModel, PowerPerfSweepCheck) {
    std::vector<tt::ARCH> architectures = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE_B0};

    for (const auto& arch : architectures) {
        check_estimates_with_perf_sweep_data(arch, "unary", "power", "power-op-performance.csv", 0.6, 1.3);
    }
}

TEST(OpModel, SigmoidPerfSweepCheck) {
    std::vector<tt::ARCH> architectures = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE_B0};

    for (const auto& arch : architectures) {
        check_estimates_with_perf_sweep_data(arch, "unary", "sigmoid", "sigmoid-op-performance.csv", 0.6, 1.3);
    }
}

TEST(OpModel, VectorModeCycles) {
    std::vector<std::string> arch_names = {"wormhole_b0", "blackhole"};
    tt::tt_op_model_desc op_desc = {
        .data_format = tt::DataFormat::Float32,
        .math_fidelity = tt::MathFidelity::HiFi4,
        .t = 2,
        .mblock_m = 4,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 2,
    };
    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        op_desc.type = "exp";
        op_desc.approx_mode = false;

        op_desc.vector_mode = tt::SfpuVectorMode::RC;
        std::uint32_t observed_rc = tt::OpModel::get_op_cycles(op_desc);

        op_desc.vector_mode = tt::SfpuVectorMode::R;
        std::uint32_t observed_r = tt::OpModel::get_op_cycles(op_desc);

        op_desc.vector_mode = tt::SfpuVectorMode::C;
        std::uint32_t observed_c = tt::OpModel::get_op_cycles(op_desc);

        EXPECT_LT(observed_c, observed_rc) << "C " << observed_c << " cycles must be lower than RC " << observed_rc << " cycles";
        EXPECT_LT(observed_r, observed_c) << "R " << observed_r << " cycles must be lower than C " << observed_c << " cycles";
    }
}

TEST(OpModel, EltwiseIntPerfSweepCheck) {
    std::vector<std::vector<std::string>> ops_info = {
        {"unary", "nop", "nop-int8-op-performance.csv"},
        {"unary", "nop", "nop-int32-op-performance.csv"},
        {"binary", "add", "add-int8-op-performance.csv"},
        {"binary", "add", "add-int32-op-performance.csv"},
        {"binary", "subtract", "subtract-int8-op-performance.csv"},
        {"binary", "multiply", "multiply-int8-op-performance.csv"},
        {"binary", "multiply", "multiply-int8-op-performance.csv"},
        {"binary", "maximum", "maximum-int8-op-performance.csv"},
        {"binary", "maximum", "maximum-int32-op-performance.csv"},
        {"binary", "quantization", "quantization-int8-op-performance.csv"},
        {"binary", "requantization", "requantization-int8-op-performance.csv"},
        {"binary", "requantization", "requantization-int32-op-performance.csv"},
        {"binary", "dequantization", "dequantization-int8-op-performance.csv"},
        {"binary", "dequantization", "dequantization-int32-op-performance.csv"},
    };

    for (const auto& op_info : ops_info) {
        check_estimates_with_perf_sweep_data(tt::ARCH::WORMHOLE_B0, op_info[0], op_info[1], op_info[2], 0.8, 1.2);
    }
}

TEST(OpModel, MatmulIntPerfSweepCheck) {
    std::vector<std::vector<std::string>> ops_info = {
        {"matmul", "matmul", "matmul-int32-op-performance.csv"},
    };

    for (const auto& op_info : ops_info) {
        check_estimates_with_perf_sweep_data(tt::ARCH::WORMHOLE_B0, op_info[0], op_info[1], op_info[2], 0.5, 1.2);
    }
}

// This test just makes sure that perf estimates don't crash
// for ops that don't have their own params but fallback to default estimates.
TEST(OpModel, FallbackToDefaultParamsTest) {
    std::vector<std::string> arch_names = {"wormhole_b0", "blackhole"};
    std::vector<std::string> ops = {
        "embedding",
        "ethernet_datacopy",
        "tilizer",
        "quantization",
        "dequantization",
        "requantization",
        "maximum",
        "fused_op",
        "topk"
    };

    tt::tt_op_model_desc op_desc = {
        .data_format = tt::DataFormat::Float32,
        .math_fidelity = tt::MathFidelity::HiFi4,
        .t = 2,
        .mblock_m = 4,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 2,
    };

    for (const auto& arch : arch_names) {
        op_desc.arch = arch;
        for (auto op : ops) {
            op_desc.type = op;
            tt::OpModel::get_op_cycles(op_desc);
        }
    }
}
