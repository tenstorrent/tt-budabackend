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

struct hlk_tile_info_t {
    std::uint8_t tile_index_within_in_buf;
    std::uint8_t buffer_index;
    std::uint8_t output_tile_index;
    std::uint8_t reserved;
};

constexpr hlk_tile_info_t tile_info[200] = {
    {.tile_index_within_in_buf = 0, .buffer_index = 0},
    {.tile_index_within_in_buf = 0, .buffer_index = 1},
    {.tile_index_within_in_buf = 0, .buffer_index = 2},
    {.tile_index_within_in_buf = 0, .buffer_index = 3},
    {.tile_index_within_in_buf = 1, .buffer_index = 0},
    {.tile_index_within_in_buf = 1, .buffer_index = 1},
    {.tile_index_within_in_buf = 1, .buffer_index = 2},
    {.tile_index_within_in_buf = 1, .buffer_index = 3}};

constexpr std::uint32_t hlk_num_output_tiles = 8;

constexpr std::uint32_t hlk_num_inputs = 4;
constexpr std::uint32_t hlk_num_input_tiles[8] = {2, 2, 0, 0, 0, 0, 0, 0};

const llk_unpack_A_params_t llk_args = {.unpA_operand = 0};

constexpr std::int32_t unpack_src_format[24] = {
    (std::int32_t)DataFormat::Bfp8,
    (std::int32_t)DataFormat::Bfp8,
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
    0};
constexpr std::int32_t unpack_dst_format[24] = {
    (std::int32_t)DataFormat::Bfp8,
    (std::int32_t)DataFormat::Bfp8,
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
    0};

const std::int32_t arg_loop_count = 1;
