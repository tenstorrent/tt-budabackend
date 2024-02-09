// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

typedef struct {
    std::int32_t dst_tile_rows;
    std::int32_t dst_tile_cols;
    std::int32_t block_tile_dim;
    std::int32_t block_cnt;
} hlk_args_t;

const hlk_args_t hlk_args{.dst_tile_rows = 2, .dst_tile_cols = 2, .block_tile_dim = 2, .block_cnt = 1};

const llk_math_matmul_params_t llk_args{.unused = 0};

const uint arg_loop_count = 1;
