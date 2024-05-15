// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_tensor_ops.h"

#include "glog/logging.h"
#include "llk_tensor_dims.h"
using namespace llk::tensor_ops;

std::unordered_map<std::string, broadcast_op> llk::tensor_ops::BROADCAST_OP_ENUM{
    {"NONE", broadcast_op::NONE},
    {"BROADCAST_COL", broadcast_op::COL},
    {"BROADCAST_ROW", broadcast_op::ROW},
    {"BROADCAST_SCALAR", broadcast_op::SCALAR},
};
broadcast_op llk::tensor_ops::get_broadcast_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (BROADCAST_OP_ENUM.find(op_string) == BROADCAST_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return BROADCAST_OP_ENUM[op_string];
}

std::unordered_map<std::string, binary_op> llk::tensor_ops::BINARY_OP_ENUM{
    {"ADD", binary_op::ELWADD},
    {"SUB", binary_op::ELWSUB},
    {"MUL", binary_op::ELWMUL},
    {"DIV", binary_op::ELWDIV},
    {"ACC", binary_op::ELWACC},
};
binary_op llk::tensor_ops::get_binary_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (BINARY_OP_ENUM.find(op_string) == BINARY_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return BINARY_OP_ENUM[op_string];
}

std::unordered_map<std::string, unary_op> llk::tensor_ops::UNARY_OP_ENUM{
    {"DATACOPY", unary_op::DATACOPY},
    {"DROPOUT", unary_op::DATACOPY},
    {"EXPONENTIAL", unary_op::EXPONENTIAL},
    {"GELU", unary_op::GELU},
    {"GELU_DERIVATIVE", unary_op::GELU_DERIVATIVE},
    {"LOG", unary_op::LOG},
    {"RECIPROCAL", unary_op::RECIPROCAL},
    {"SIGMOID", unary_op::SIGMOID},
    {"SQRT", unary_op::SQRT},
    {"TANH", unary_op::TANH},
    {"TRANSPOSE_XY", unary_op::TRANSPOSE_XY},
    {"ZERO_OUTPUT", unary_op::ZERO_OUTPUT},
};
unary_op llk::tensor_ops::get_unary_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (UNARY_OP_ENUM.find(op_string) == UNARY_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return UNARY_OP_ENUM[op_string];
}

std::unordered_map<std::string, unary_sfpi_op> llk::tensor_ops::UNARY_SFPI_OP_ENUM{
    {"TEST1", unary_sfpi_op::TEST1},
    {"TEST2", unary_sfpi_op::TEST2},
    {"TEST3", unary_sfpi_op::TEST3},
    {"TEST4", unary_sfpi_op::TEST4},
    {"TEST5", unary_sfpi_op::TEST5},
    {"TEST6", unary_sfpi_op::TEST6},
    {"TEST7", unary_sfpi_op::TEST7},
    {"TEST8", unary_sfpi_op::TEST8},
    {"TEST9", unary_sfpi_op::TEST9},
    {"TEST10", unary_sfpi_op::TEST10},
    {"TEST11", unary_sfpi_op::TEST11},
    {"TEST12", unary_sfpi_op::TEST12},
    {"TEST13", unary_sfpi_op::TEST13},
    {"TEST14", unary_sfpi_op::TEST14},
};
unary_sfpi_op llk::tensor_ops::get_unary_sfpi_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (UNARY_SFPI_OP_ENUM.find(op_string) == UNARY_SFPI_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return UNARY_SFPI_OP_ENUM[op_string];
}

std::unordered_map<std::string, relu_op> llk::tensor_ops::RELU_OP_ENUM{
    {"NONE", relu_op::NONE},
    {"ZERO", relu_op::ZERO},
    {"MIN_THRESHOLD", relu_op::MIN_THRESHOLD},
    {"MAX_THRESHOLD", relu_op::MAX_THRESHOLD},
};
relu_op llk::tensor_ops::get_relu_op_from_string(std::string relu_op_string) {
    transform(relu_op_string.begin(), relu_op_string.end(), relu_op_string.begin(), ::toupper);
    if (RELU_OP_ENUM.find(relu_op_string) == RELU_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + relu_op_string + " not supported");
    }
    return RELU_OP_ENUM[relu_op_string];
}

std::unordered_map<std::string, reduce_math_op> llk::tensor_ops::REDUCE_MATH_OP_ENUM{
    {"AVG", reduce_math_op::AVG},
    {"MAX", reduce_math_op::MAX},
    {"SUM", reduce_math_op::SUM},
};
reduce_math_op llk::tensor_ops::get_reduce_math_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (REDUCE_MATH_OP_ENUM.find(op_string) == REDUCE_MATH_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return REDUCE_MATH_OP_ENUM[op_string];
}

std::unordered_map<std::string, reduce_op> llk::tensor_ops::REDUCE_OP_ENUM{
    {"ROW", reduce_op::ROW},
    {"COL", reduce_op::COL},
    {"SCALAR", reduce_op::SCALAR},
};
reduce_op llk::tensor_ops::get_reduce_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (REDUCE_OP_ENUM.find(op_string) == REDUCE_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return REDUCE_OP_ENUM[op_string];
}

std::unordered_map<std::string, transpose_op> llk::tensor_ops::TRANSPOSE_OP_ENUM{
    {"XY", transpose_op::XY},
    {"YZ", transpose_op::YZ},
};
transpose_op llk::tensor_ops::get_transpose_op_from_string(std::string op_string) {
    transform(op_string.begin(), op_string.end(), op_string.begin(), ::toupper);
    if (TRANSPOSE_OP_ENUM.find(op_string) == TRANSPOSE_OP_ENUM.end()) {
        throw std::runtime_error("tensor_op " + op_string + " not supported");
    }
    return TRANSPOSE_OP_ENUM[op_string];
}

void llk::tensor_ops::tile_ops::broadcast(broadcast_op op_type, c_tile &output, c_tile &src) {
    // Tile level operation
    if (op_type == broadcast_op::COL) {
        output.copy_from_tile(src);
        output.iterate_xyzw(
            [](c_tile *tile, int x, int y, int z, int w) { tile->value(x, y, z, w).f = tile->value(0, y, z, w).f; });
    } else if (op_type == broadcast_op::ROW) {
        output.copy_from_tile(src);
        output.iterate_xyzw(
            [](c_tile *tile, int x, int y, int z, int w) { tile->value(x, y, z, w).f = tile->value(x, 0, z, w).f; });
    } else if (op_type == broadcast_op::SCALAR) {
        output.copy_from_tile(src);
        output.iterate_xyzw(
            [](c_tile *tile, int x, int y, int z, int w) { tile->value(x, y, z, w).f = tile->value(0, 0, z, w).f; });
    } else if (op_type == broadcast_op::NONE) {
        output.copy_from_tile(src);
    } else {
        throw std::runtime_error("Broadcast op not supported");
    }
}

void llk::tensor_ops::broadcast(broadcast_op op_type, llk::Tensor &dst, llk::TensorDims dst_dims, llk::Tensor &src) {
    dst.set_dims(dst_dims);
    for (auto w = 0; w < dst.tile_tensor.size(); w++) {
        for (auto z = 0; z < dst.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < dst.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < dst.tile_tensor[w][z][ct].size(); rt++) {
                    auto &src_tile = src.tile_tensor[w][z][ct][rt];
                    auto &dst_tile = dst.tile_tensor[w][z][ct][rt];
                    tile_ops::broadcast(op_type, dst_tile, src_tile);
                }
            }
        }
    }
}

void llk::tensor_ops::tile_ops::unary(unary_op op_type, c_tile &output, c_tile &src) {
    // Tile level operation
    if (op_type == unary_op::DATACOPY) {
        output.copy_from_tile(src);
    } else if (op_type == unary_op::DROPOUT) {
        output.copy_from_tile(src);
    } else if (op_type == unary_op::EXPONENTIAL) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = std::exp(tile->value(x, y, z, w).f);
        });
    } else if (op_type == unary_op::GELU) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            float &value = tile->value(x, y, z, w).f;
            value = 0.5 * value * (1 + std::tanh((M_2_SQRTPI / M_SQRT2) * (value + 0.044715 * std::pow(value, 3))));
        });
    } else if (op_type == unary_op::GELU_DERIVATIVE) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            float &value = tile->value(x, y, z, w).f;
            value = (0.5 * (1 + std::erf(value * M_SQRT1_2))) +
                    value * ((M_2_SQRTPI * M_SQRT1_2 * 0.5) * std::exp(std::pow(value, 2) * (-0.5)));
        });
    } else if (op_type == unary_op::LOG) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = std::log(tile->value(x, y, z, w).f);
        });
    } else if (op_type == unary_op::RECIPROCAL) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            float &value = tile->value(x, y, z, w).f;
            // For FP16. Previously std::numeric_limits<float>::epsilon();
            bool isvalzero = std::abs(value - 0.0f) < 9.77E-04F;
            // Attempt to better match versim which returns signed large number when div zero.
            // Change to be >= instead of > to support value = 0.0f picking pos INFINITY
            value = isvalzero ? (value >= 0.0f ? INFINITY : -INFINITY) : 1.0f / value;
        });
    } else if (op_type == unary_op::SIGMOID) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            float &value = tile->value(x, y, z, w).f;
            value = 1.0f / (1.0f + std::exp(-value));
        });
    } else if (op_type == unary_op::SQRT) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = std::sqrt(tile->value(x, y, z, w).f);
        });
    } else if (op_type == unary_op::TANH) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = std::tanh(tile->value(x, y, z, w).f);
        });
    } else if (op_type == unary_op::TRANSPOSE_XY) {
        output.copy_from_tile(src);
        output.iterate_xyzw(
            [src](c_tile *tile, int x, int y, int z, int w) { tile->value(x, y, z, w).f = src.value(y, x, z, w).f; });
    } else if (op_type == unary_op::ZERO_OUTPUT) {
        output.tile_reset();
    } else {
        throw std::runtime_error("Unary op not supported");
    }
}

void llk::tensor_ops::unary(unary_op op_type, llk::Tensor &dst, llk::Tensor &src, float scale_factor) {
    dst.set_dims(src.dims);
    c_tile scale_tile(llk::TensorDims::TILE_ROW_DIM, llk::TensorDims::TILE_COL_DIM, 1, 1);
    scale_tile.init_tile(scale_factor);
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    auto &src_tile = src.tile_tensor[w][z][ct][rt];
                    auto &dst_tile = dst.tile_tensor[w][z][ct][rt];
                    tile_ops::unary(op_type, dst_tile, src_tile);
                    dst_tile = dst_tile.elementwise_mul(scale_tile);
                }
            }
        }
    }
}

void llk::tensor_ops::unary_broadcast(
    unary_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src,
    broadcast_op src_broadcast_type,
    float scale_factor) {
    llk::Tensor src_broadcasted;
    broadcast(src_broadcast_type, src_broadcasted, dst_dims, src);
    unary(op_type, dst, src_broadcasted, scale_factor);
}

void llk::tensor_ops::tile_ops::unary_sfpi(unary_sfpi_op op_type, c_tile &output, c_tile &src) {
    // Tile level operation
    if (op_type >= unary_sfpi_op::TEST1 && op_type <= unary_sfpi_op::TEST14) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = std::sqrt(tile->value(x, y, z, w).f);
        });
    } else {
        throw std::runtime_error("Unary sfpi op not supported");
    }
}

void llk::tensor_ops::unary_sfpi(unary_sfpi_op op_type, llk::Tensor &dst, llk::Tensor &src, float scale_factor) {
    dst.set_dims(src.dims);
    c_tile scale_tile(llk::TensorDims::TILE_ROW_DIM, llk::TensorDims::TILE_COL_DIM, 1, 1);
    scale_tile.init_tile(scale_factor);
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    auto &src_tile = src.tile_tensor[w][z][ct][rt];
                    auto &dst_tile = dst.tile_tensor[w][z][ct][rt];
                    tile_ops::unary_sfpi(op_type, dst_tile, src_tile);
                    dst_tile = dst_tile.elementwise_mul(scale_tile);
                }
            }
        }
    }
}

void llk::tensor_ops::unary_sfpi_broadcast(
    unary_sfpi_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src,
    broadcast_op src_broadcast_type,
    float scale_factor) {
    llk::Tensor src_broadcasted;
    broadcast(src_broadcast_type, src_broadcasted, dst_dims, src);
    unary_sfpi(op_type, dst, src_broadcasted, scale_factor);
}

void llk::tensor_ops::tile_ops::relu(relu_op op_type, c_tile &output, c_tile &src, float relu_threshold) {
    // Tile level operation
    if (op_type == relu_op::NONE) {
        output.copy_from_tile(src);
    } else if (op_type == relu_op::ZERO) {
        output.copy_from_tile(src);
        output.iterate_xyzw([](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f = ((tile->value(x, y, z, w).f) < 0.0) ? 0.0 : (tile->value(x, y, z, w).f);
        });
    } else if (op_type == relu_op::MIN_THRESHOLD) {
        output.iterate_xyzw([src, relu_threshold](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f =
                ((tile->value(x, y, z, w).f) < relu_threshold) ? 0.0 : (tile->value(x, y, z, w).f);
        });
    } else if (op_type == relu_op::MAX_THRESHOLD) {
        output.iterate_xyzw([src, relu_threshold](c_tile *tile, int x, int y, int z, int w) {
            tile->value(x, y, z, w).f =
                ((tile->value(x, y, z, w).f) > relu_threshold) ? relu_threshold : (tile->value(x, y, z, w).f);
        });
    } else {
        throw std::runtime_error("relu mode not supported");
    }
}

void llk::tensor_ops::relu(relu_op op_type, llk::Tensor &dst, llk::Tensor &src, float relu_threshold) {
    dst.set_dims(src.dims);
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    auto &src_tile = src.tile_tensor[w][z][ct][rt];
                    auto &dst_tile = dst.tile_tensor[w][z][ct][rt];
                    tile_ops::relu(op_type, dst_tile, src_tile, relu_threshold);
                }
            }
        }
    }
}

void llk::tensor_ops::tile_ops::binary(binary_op op_type, c_tile &output, c_tile &src1, c_tile &src2) {
    // Tile level operation
    if (op_type == binary_op::ELWADD) {  // src1 + src2
        output.copy_from_tile(src1);
        output = output.elementwise_add(src2);
    } else if (op_type == binary_op::ELWSUB) {  // src1 - src2
        output.copy_from_tile(src1);
        output = output.elementwise_sub(src2);
    } else if (op_type == binary_op::ELWMUL) {  // src1 * src2
        output.copy_from_tile(src1);
        output = output.elementwise_mul(src2);
    } else if (op_type == binary_op::ELWDIV) {  // src1 / src2
        output.copy_from_tile(src1);
        output = output.elementwise_div(src2);
    } else {
        throw std::runtime_error("Binary op not supported");
    }
}

void llk::tensor_ops::binary(binary_op op_type, llk::Tensor &dst, llk::Tensor &src1, llk::Tensor &src2) {
    dst.set_dims(src1.dims);
    if (src1.dims != src2.dims) {
        LOG(ERROR) << __FUNCTION__ << "::src1 dims:" << src1.dims.str()
                   << " does not match src2 dims:" << src2.dims.str();
    } else {
        for (auto w = 0; w < src1.tile_tensor.size(); w++) {
            for (auto z = 0; z < src1.tile_tensor[w].size(); z++) {
                for (auto ct = 0; ct < src1.tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < src1.tile_tensor[w][z][ct].size(); rt++) {
                        // Tile level operation
                        auto &src1_tile = src1.tile_tensor[w][z][ct][rt];
                        auto &src2_tile = src2.tile_tensor[w][z][ct][rt];
                        auto &dst_tile = dst.tile_tensor[w][z][ct][rt];
                        tile_ops::binary(op_type, dst_tile, src1_tile, src2_tile);
                    }
                }
            }
        }
    }
}

void llk::tensor_ops::binary_broadcast(
    binary_op op_type,
    llk::Tensor &dst,
    llk::TensorDims dst_dims,
    llk::Tensor &src1,
    broadcast_op src1_broadcast_type,
    llk::Tensor &src2,
    broadcast_op src2_broadcast_type) {
    llk::Tensor src1_broadcasted;
    llk::Tensor src2_broadcasted;
    broadcast(src1_broadcast_type, src1_broadcasted, dst_dims, src1);
    broadcast(src2_broadcast_type, src2_broadcasted, dst_dims, src2);
    binary(op_type, dst, src1_broadcasted, src2_broadcasted);
}

void llk::tensor_ops::matmul(llk::Tensor &dst, llk::Tensor &src1, llk::Tensor &src2, bool transpose_xy_en) {
    bool low_fidelity = true;
    dst.set_dims(llk::TensorDims(src2.dims.w, src2.dims.z, src1.dims.y, src2.dims.x));
    if (src1.dims.x != src2.dims.y) {
        LOG(ERROR) << __FUNCTION__ << "::src1 dims x=" << src1.dims.x << " does not match src2 dims y" << src2.dims.y;
    } else if ((src1.dims.z != src2.dims.z) or (src1.dims.w != src2.dims.w)) {
        LOG(ERROR) << __FUNCTION__ << "::src1 dims:" << src1.dims.str()
                   << " does not match in (z, w) dimentions with src2 dims:" << src2.dims.str();
    } else {
        for (auto w = 0; w < dst.tile_tensor.size(); w++) {
            for (auto z = 0; z < dst.tile_tensor[w].size(); z++) {
                // dst dims must be equal to the com
                for (auto ct = 0; ct < dst.tile_tensor[w][z].size(); ct++) {
                    for (auto rt = 0; rt < dst.tile_tensor[w][z][ct].size(); rt++) {
                        auto &tile = dst.tile_tensor[w][z][ct][rt];
                        tile.tile_reset();
                        for (auto inner_dim_index = 0; inner_dim_index < src2.tile_tensor[w][z].size();
                             inner_dim_index++) {
                            auto &src1_tile = src1.tile_tensor[w][z][ct][inner_dim_index];
                            auto &src2_tile = src2.tile_tensor[w][z][inner_dim_index][rt];

                            c_tile src2_tile_final;

                            if (transpose_xy_en) {
                                src2_tile_final.copy_from_tile(src2_tile);
                                src2_tile_final.iterate_xyzw([&src2_tile](c_tile *tile, int x, int y, int z, int w) {
                                    tile->value(x, y, z, w).f = src2_tile.value(y, x, z, w).f;
                                });
                            } else {
                                src2_tile_final = src2_tile;
                            }

                            auto multiply_result = src1_tile.multiply(src2_tile_final, low_fidelity);
                            tile = tile.elementwise_add(multiply_result);
                        }
                    }
                }
            }
        }
    }
}

void llk::tensor_ops::reduce(
    reduce_op op_type, reduce_math_op math_op_type, llk::Tensor &dst, llk::Tensor &src, float multiplier) {
    VLOG(3) << "Original Size: " << src.dims.str();
    VLOG(3) << "multiplier: " << multiplier;
    VLOG(3) << "Pooled Size: " << dst.dims.str();
    if ((math_op_type == reduce_math_op::AVG) or (math_op_type == reduce_math_op::SUM)) {
    } else {
        throw std::runtime_error("Not Supported reduce_math_op");
    }
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    // Tile level operation
                    if (op_type == reduce_op::ROW) {
                        auto &dst_tile = dst.tile_tensor[w][z][ct][0];
                        if (rt == 0) {
                            dst_tile.init_tile(0);
                        }
                        auto &src_tile = src.tile_tensor[w][z][ct][rt];
                        dst_tile.iterate_xyzw([src_tile, multiplier](c_tile *tile, int x, int y, int z, int w) {
                            tile->value(0, y, z, w).f += src_tile.value(x, y, z, w).f * multiplier;
                        });
                    } else if (op_type == reduce_op::COL) {
                        auto &dst_tile = dst.tile_tensor[w][z][0][rt];
                        if (ct == 0) {
                            dst_tile.init_tile(0);
                        }
                        auto &src_tile = src.tile_tensor[w][z][ct][rt];
                        dst_tile.iterate_xyzw([src_tile, multiplier](c_tile *tile, int x, int y, int z, int w) {
                            tile->value(x, 0, z, w).f += src_tile.value(x, y, z, w).f * multiplier;
                        });
                    } else if (op_type == reduce_op::SCALAR) {
                        auto &dst_tile = dst.tile_tensor[w][z][0][0];
                        if ((ct == 0) and (rt == 0)) {
                            dst_tile.init_tile(0);
                        }
                        auto &src_tile = src.tile_tensor[w][z][ct][rt];
                        dst_tile.iterate_xyzw([src_tile, multiplier](c_tile *tile, int x, int y, int z, int w) {
                            tile->value(0, 0, z, w).f += src_tile.value(x, y, z, w).f * multiplier;
                        });
                    } else {
                        throw std::runtime_error("Unsupported reduce operation");
                    }
                }
            }
        }
    }
}

void llk::tensor_ops::pack(llk::Tensor &dst, llk::Tensor &src, bool skip_tilize) {
    uint data_format = static_cast<int>(src.data_format);
    bool quick_mode = true;  // Should always be quick ;)
    bool compressed = false;
    auto dis_zerocomp = not compressed;
    dst.set_dims(src.dims);
    if ((not src.tilized) and (not skip_tilize)) {
        src.tilize();
        dst.tilized = true;
    }
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    dst.tile_tensor[w][z][ct][rt].copy_from_tile(src.tile_tensor[w][z][ct][rt]);
                    dst.tile_tensor[w][z][ct][rt].pack(data_format, dis_zerocomp, quick_mode);
                }
            }
        }
    }
}

void llk::tensor_ops::unpack(llk::Tensor &dst, llk::Tensor &src) {
    // TODO: Need to pass this in as some unpacking spec?
    uint data_format = static_cast<int>(src.data_format);
    bool compressed = false;
    auto dis_zerocomp = not compressed;
    // TODO: end
    dst.set_dims(src.dims);
    for (auto w = 0; w < src.tile_tensor.size(); w++) {
        for (auto z = 0; z < src.tile_tensor[w].size(); z++) {
            for (auto ct = 0; ct < src.tile_tensor[w][z].size(); ct++) {
                for (auto rt = 0; rt < src.tile_tensor[w][z][ct].size(); rt++) {
                    dst.tile_tensor[w][z][ct][rt] = src.tile_tensor[w][z][ct][rt].unpack(data_format, dis_zerocomp);
                }
            }
        }
    }
}
