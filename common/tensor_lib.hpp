// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "model/tensor.hpp"

namespace tt::tensor_lib {

namespace basic_ops {
void eltwise_binary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const tt::tt_tensor& input1,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const tt::tt_tile&)> binary_tile_callback);
void eltwise_unary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const Dim& vector_mode,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const Dim&)> unary_tile_callback);
void eltwise_nary(
    tt::tt_tensor& output_tensor,
    const std::vector<tt::tt_tensor>& inputs,
    std::function<void(tt::tt_tile&, const std::vector<tt::tt_tile>&)> nary_tile_callback);
void depthwise_binary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor input0,
    const tt::tt_tensor input1,
    std::uint32_t block_cnt,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const tt::tt_tile&)> binary_tile_callback);
}  // namespace basic_ops
namespace split_merge_ops {
// Functions for taking the input vectors and merging it into the tensor
tt_tensor hmerge(const vector<tt_tensor>& inputs, bool shape_only);
tt_tensor vmerge(const vector<tt_tensor>& inputs, bool shape_only);
// Functions for splitting tensors along a dim
vector<tt_tensor> hsplit(tt_tensor& input, unsigned int split_factor, bool shape_only);
vector<tt_tensor> vsplit(tt_tensor& input, unsigned int split_factor, bool shape_only);
vector<tt_tensor> zsplit(tt_tensor& input, bool shape_only);
vector<tt_tensor> wsplit(tt_tensor& input, bool shape_only);
}  // namespace split_merge_ops
// Special case ops that need to add weird params or need more inputs etc...
enum class transpose_modes { Full, TileLocationsOnly, WithinTileOnly };
void datacopy(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0);
void power(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, int32_t exponent);
void lrelu(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, float slope);
void dropout(
    tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, float probability, float scale);

void relu_with_threshold(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const Dim& vector_mode,
    const ReluMode& mode,
    const float& threshold);

void vector_datacopy(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor input0,
    const Dim& vector_type,
    const unsigned int input_index,
    const unsigned int output_index,
    const bool is_input_layout_flat);

tt::tt_tensor pad_rc_val(const tt::tt_tensor& input_tensor, uint32_t r, uint32_t c, float val);
tt::tt_tensor unpad(const tt::tt_tensor& input_tensor, uint32_t r, uint32_t c);
}  // namespace tt::tensor_lib
