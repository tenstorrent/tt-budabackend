// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "test_unit_common.hpp"
#include "perf_lib/op_model/op_model.hpp"

TEST(BackendPerf, OpModelAPI) {
    tt::tt_op_model_desc op_desc = {
        .type = "gelu",
        .arch = "wormhole_b0",
        .data_format = tt::DataFormat::Float16,
        .math_fidelity = tt::MathFidelity::HiFi4,
        .t = 2,
        .mblock_m = 4,
        .mblock_n = 4,
        .ublock_rt = 2,
        .ublock_ct = 2,
    };

    auto model_cycles = tt::OpModel::get_op_cycles(op_desc);
    auto be_api_cycles = tt::backend::get_op_model_execution_cycles(op_desc);
    EXPECT_EQ(model_cycles, be_api_cycles);
}

