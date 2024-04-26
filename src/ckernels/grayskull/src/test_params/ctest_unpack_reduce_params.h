// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <tensix_types.h>

typedef struct {
    std::int32_t num_block_rows;
    std::int32_t num_block_columns;
    std::int32_t num_tile_rows_per_block;
    std::int32_t num_tile_columns_per_block;
} hlk_args_t;

const hlk_args_t hlk_args = {
    .num_block_rows = 2, .num_block_columns = 1, .num_tile_rows_per_block = 1, .num_tile_columns_per_block = 4};
const float const_mult = 1.0;
const llk_unpack_reduce_params_t llk_args = {.unpA_operand = 0};

constexpr std::int32_t unpack_src_format[24] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::int32_t unpack_dst_format[24] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const std::int32_t arg_loop_count = 1;
