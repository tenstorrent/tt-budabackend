// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/tile.hpp"

namespace tt::tile_lib {

namespace binary {
void add(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1);
void subtract(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1);
void multiply(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1);
}  // namespace binary
namespace unary {
void exp(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void log(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void sigmoid(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void sqrt(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void relu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void abs(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void lrelu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode, const float& slope);
void relu_with_threshold(
    tt::tt_tile& output_tile,
    const tt::tt_tile& input0,
    const Dim& vector_mode,
    const ReluMode& mode,
    const float& threshold);
void gelu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void gelu_derivative(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void reciprocal(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void tanh(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void dropout(
    tt::tt_tile& output_tile,
    const tt::tt_tile& input0,
    const Dim& vector_mode,
    const float& probability,
    const float& scale);
void square(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void power(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode, const int& exponent);
void sine(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void cosine(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode);
void transpose_xy(tt::tt_tile& output_tile, const tt::tt_tile& input0);
void datacopy(tt::tt_tile& output_tile, const tt::tt_tile& input0);
void vector_datacopy(
    tt::tt_tile& output_tile,
    const tt::tt_tile input0,
    const Dim& vector_type,
    const unsigned int input_index,
    const unsigned int output_index);
}  // namespace unary

}  // namespace tt::tile_lib
