// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <tensix_types.h>

typedef struct {
    std::int32_t dst_tile_rows;
    std::int32_t dst_tile_cols;
    std::int32_t block_cnt;
} hlk_args_t;

const hlk_args_t hlk_args = {.dst_tile_rows = 1, .dst_tile_cols = 4, .block_cnt = 2};

const llk_unpack_untilize_params_t llk_args = {
    .unpA_operand = 0,
};

constexpr std::int32_t unpack_src_format[24] = {
    (std::int32_t)DataFormat::Float16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::int32_t unpack_dst_format[24] = {
    (std::int32_t)DataFormat::Float16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const std::int32_t arg_loop_count = 1;
