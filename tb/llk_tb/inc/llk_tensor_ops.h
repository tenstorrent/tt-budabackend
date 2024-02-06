// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>

#include "llk_tensor.h"

namespace llk {
namespace tensor_ops {

enum class broadcast_op { NONE, COL, ROW, SCALAR };
extern std::unordered_map<std::string, broadcast_op> BROADCAST_OP_ENUM;
broadcast_op get_broadcast_op_from_string(std::string op_string);

enum class binary_op { ELWADD, ELWSUB, ELWMUL, ELWDIV, ELWACC };
extern std::unordered_map<std::string, binary_op> BINARY_OP_ENUM;
binary_op get_binary_op_from_string(std::string op_string);

enum class unary_op {
    DATACOPY,
    DROPOUT,
    EXPONENTIAL,
    GELU,
    GELU_DERIVATIVE,
    LOG,
    RECIPROCAL,
    SIGMOID,
    SQRT,
    TANH,
    TRANSPOSE_XY,
    ZERO_OUTPUT
};
extern std::unordered_map<std::string, unary_op> UNARY_OP_ENUM;
unary_op get_unary_op_from_string(std::string op_string);

enum class unary_sfpi_op {
    TEST1,
    TEST2,
    TEST3,
    TEST4,
    TEST5,
    TEST6,
    TEST7,
    TEST8,
    TEST9,
    TEST10,
    TEST11,
    TEST12,
    TEST13,
    TEST14,
};

extern std::unordered_map<std::string, unary_sfpi_op> UNARY_SFPI_OP_ENUM;
unary_sfpi_op get_unary_sfpi_op_from_string(std::string op_string);

enum class relu_op {
    NONE,
    ZERO,
    MIN_THRESHOLD,
    MAX_THRESHOLD
};
extern std::unordered_map<std::string, relu_op> RELU_OP_ENUM;
relu_op get_relu_op_from_string(std::string relu_op_string);

enum class reduce_math_op { AVG, MAX, SUM };
extern std::unordered_map<std::string, reduce_math_op> REDUCE_MATH_OP_ENUM;
reduce_math_op get_reduce_math_op_from_string(std::string op_string);

enum class reduce_op { ROW, COL, SCALAR };
extern std::unordered_map<std::string, reduce_op> REDUCE_OP_ENUM;
reduce_op get_reduce_op_from_string(std::string op_string);

enum class transpose_op { XY, YZ };
extern std::unordered_map<std::string, transpose_op> TRANSPOSE_OP_ENUM;
transpose_op get_transpose_op_from_string(std::string op_string);

//! Broadcast Tensor operation -- dst = broadcast(src)
void broadcast(broadcast_op op_type, llk::Tensor &dst, llk::TensorDims dst_dims, llk::Tensor &src);

//! Unary operation -- dst = unary(src)
void unary(unary_op op_type, llk::Tensor &dst, llk::Tensor &src, float scale_factor = 1.0f);

//! Broadcast Unary operation -- dst = src
void unary_broadcast(
    unary_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src,
    broadcast_op src_broadcast_type,
    float scale_factor = 1.0f);

//! Unary sfpi operation -- dst = unary(src)
void unary_sfpi(unary_sfpi_op op_type, llk::Tensor &dst, llk::Tensor &src, float scale_factor = 1.0f);

//! Broadcast Unary sfpi operation -- dst = src
void unary_sfpi_broadcast(
    unary_sfpi_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src,
    broadcast_op src_broadcast_type,
    float scale_factor = 1.0f);

//! Relu operation -- dst = relu(src)
void relu(relu_op op_type, llk::Tensor &dst, llk::Tensor &src, float relu_threshold = 0.0f);

//! Binary operation -- dst = src1 <op> src2
void binary(binary_op op_type, llk::Tensor &dst, llk::Tensor &src1, llk::Tensor &src2);

//! Broadcast Binary operation -- dst = src1 <op> src2
void binary_broadcast(
    binary_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src1,
    broadcast_op src1_broadcast_type,
    llk::Tensor &src2,
    broadcast_op src2_broadcast_type);

//! Row Pool Operation
void reduce(reduce_op op_type, reduce_math_op math_op_type, llk::Tensor &dst, llk::Tensor &src, float multiplier);

//! Matmul operation -- dst = src1 <op> src2
void matmul(llk::Tensor &dst, llk::Tensor &src1, llk::Tensor &src2, bool transpose_xy_en = false);

//! Pack operation -- assumes src is unpacked and float tiles. dst = pack(src)
void pack(llk::Tensor &dst, llk::Tensor &src, bool skip_tilize = false);

//! Unpack operation -- assumes src is packed tiles in the data_format specified. dst = unpack(src)
void unpack(llk::Tensor &dst, llk::Tensor &src);

namespace tile_ops {
void unary(unary_op op_type, c_tile &output, c_tile &src);
void relu(relu_op op_type, c_tile &output, c_tile &src, float relu_threshold = 0.0f);
void binary(binary_op op_type, c_tile &output, c_tile &src1, c_tile &src2);
void broadcast(broadcast_op op_type, c_tile &output, c_tile &src);
void unary_sfpi(unary_sfpi_op op_type, c_tile &output, c_tile &src);
}  // namespace tile_ops
}  // namespace tensor_ops
}  // namespace llk
