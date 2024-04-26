// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

typedef struct {
    std::int32_t num_block_rows;
    std::int32_t num_block_columns;
    std::int32_t num_tile_rows_per_block;
    std::int32_t num_tile_columns_per_block;
} hlk_args_t;

const hlk_args_t hlk_args{
    .num_block_rows = 2, .num_block_columns = 1, .num_tile_rows_per_block = 1, .num_tile_columns_per_block = 4};

const llk_math_reduce_params_t llk_args{.unused = 0};

const uint arg_loop_count = 1;
