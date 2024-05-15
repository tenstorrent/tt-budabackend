// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llk_tensor.h"

namespace llk {
//! test_lib is the exposed library functions to interface and interact with tests
namespace test_lib {}  // namespace test_lib

bool verify_match_ratio(
    llk::Tensor expected,
    llk::Tensor calculated,
    float minimum_match_ratio,
    float match_ratio_error_threshold,
    float dropout_percentage,
    float dropout_percentage_error_threshold,
    bool skip_match_ratio_check,
    bool skip_dropout_check);

bool verify_pcc(llk::Tensor expected, llk::Tensor calculated, float minimum_pcc, bool skip_pcc_check);
}  // namespace llk
