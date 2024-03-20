// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

enum SfpuType {
    tanh,
    hardtanh,
    gelu,
    exponential,
    exp_with_base,
    sigmoid,
    reciprocal,
    sqrt,
    lrelu,
    power,
    square,
    tanh_derivative,
    log,
    log_with_base,
    equal_zero,
    not_equal_zero,
    less_than_zero,
    greater_than_equal_zero,
    less_than_equal_zero,
    greater_than_zero,
    clamp,
    gelu_derivative,
    dropout,
    abs,
    sign,
    max,
    sine,
    cosine,
    relu_max,
    relu_min,
    cast_fp32_to_fp16a, // unused on GS
    unused,
};