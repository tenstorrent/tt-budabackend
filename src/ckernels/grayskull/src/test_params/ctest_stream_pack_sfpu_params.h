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

const hlk_args_t hlk_args = {.dst_tile_rows = 1, .dst_tile_cols = 32, .block_cnt = 1};

const llk_pack_params_t llk_args = {.pack_output = 16, .relu_config = {.f = {.ApplyRelu = 0, .Threshold = 0}}};

constexpr std::int32_t pack_src_format[16] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::int32_t pack_dst_format[16] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const std::int32_t arg_loop_count = 1;
