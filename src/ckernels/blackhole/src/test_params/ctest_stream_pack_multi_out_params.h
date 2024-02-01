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

constexpr hlk_args_t hlk_args = {.dst_tile_rows = 8, .dst_tile_cols = 1, .block_cnt = 1};

struct hlk_tile_info_t {
    std::uint8_t tile_index_within_out_buf;
    std::uint8_t buffer_index;
    std::uint8_t input_tile_index;
    std::uint8_t reserved;
};

constexpr hlk_tile_info_t tile_info[128] = {
    {.tile_index_within_out_buf = 0, .buffer_index = 16},
    {.tile_index_within_out_buf = 0, .buffer_index = 17},
    {.tile_index_within_out_buf = 1, .buffer_index = 16},
    {.tile_index_within_out_buf = 1, .buffer_index = 17},
    {.tile_index_within_out_buf = 2, .buffer_index = 16},
    {.tile_index_within_out_buf = 2, .buffer_index = 17},
    {.tile_index_within_out_buf = 3, .buffer_index = 16},
    {.tile_index_within_out_buf = 3, .buffer_index = 17}};

constexpr std::uint32_t hlk_num_input_tiles = 8;

constexpr std::uint32_t hlk_num_outputs = 2;
constexpr std::uint32_t hlk_num_output_tiles[8] = {4, 4, 0, 0, 0, 0, 0, 0};

constexpr llk_pack_params_t llk_args = {.pack_output = 0, .relu_config = {.f = {.ApplyRelu = 0, .Threshold = 0}}};

constexpr std::int32_t pack_src_format[16] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::int32_t pack_dst_format[16] = {
    (std::int32_t)DataFormat::Bfp8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

constexpr std::int32_t arg_loop_count = 1;
