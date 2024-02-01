// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <tensix_types.h>

typedef struct {
    std::int32_t dst_tile_rows;
    std::int32_t dst_tile_cols;
    std::int32_t block_tile_dim;
    std::int32_t block_cnt;
} hlk_args_t;

const hlk_args_t hlk_args = {.dst_tile_rows = 2, .dst_tile_cols = 2, .block_tile_dim = 2, .block_cnt = 1};

typedef struct {
    const llk_unpack_AB_matmul_params_t llk_unpack_AB_matmul_args;
    const llk_unpack_AB_params_t llk_unpack_AB_args;
} llk_args_t;

const llk_args_t llk_args = {
    .llk_unpack_AB_matmul_args = {.unpA_operand = 0, .unpB_operand = 1, .transpose_xy_srca = 0},
    .llk_unpack_AB_args = {.unpA_operand = 0, .unpB_operand = 1},
};

constexpr std::int32_t unpack_src_format[24] = {
    (std::int32_t)DataFormat::Bfp8,
    (std::int32_t)DataFormat::Bfp8,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0};
constexpr std::int32_t unpack_dst_format[24] = {
    (std::int32_t)DataFormat::Bfp8,
    (std::int32_t)DataFormat::Bfp8,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0};

const std::int32_t arg_loop_count = 1;
