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

const hlk_args_t hlk_args = {
    .dst_tile_rows = 1,
    .dst_tile_cols = 4,
    .block_cnt = 2
};

struct hlk_tile_info_t {
    std::uint8_t tile_index_within_in_buf;
    std::uint8_t buffer_index;
    std::uint8_t output_tile_index;
    std::uint8_t reserved;
};

constexpr hlk_tile_info_t tile_info[128] = {{.tile_index_within_in_buf = 0, .buffer_index = 0},
                                            {.tile_index_within_in_buf = 0, .buffer_index = 1},
                                            {.tile_index_within_in_buf = 0, .buffer_index = 2},
                                            {.tile_index_within_in_buf = 0, .buffer_index = 3},
                                            {.tile_index_within_in_buf = 1, .buffer_index = 0},
                                            {.tile_index_within_in_buf = 1, .buffer_index = 1},
                                            {.tile_index_within_in_buf = 1, .buffer_index = 2},
                                            {.tile_index_within_in_buf = 1, .buffer_index = 3}};

constexpr std::uint32_t hlk_num_output_tiles = __num_output_tiles__;

constexpr std::uint32_t hlk_num_inputs = __num_inputs__;
constexpr std::uint32_t hlk_num_input_tiles[8] = {__num_input0_tiles__,__num_input1_tiles__,__num_input2_tiles__,__num_input3_tiles__,0,0,0,0};

const llk_unpack_A_params_t llk_args = {
    .unpA_operand = 0
};

constexpr std::int32_t unpack_src_format[24] = {(std::int32_t) DataFormat::__src0_format__,(std::int32_t) DataFormat::__src1_format__,(std::int32_t) DataFormat::__src2_format__,(std::int32_t) DataFormat::__src3_format__,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
constexpr std::int32_t unpack_dst_format[24] = {(std::int32_t) DataFormat::__dst0_format__,(std::int32_t) DataFormat::__dst1_format__,(std::int32_t) DataFormat::__dst2_format__,(std::int32_t) DataFormat::__dst3_format__,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

constexpr std::uint32_t unpack_tile_dims[24][2] = {{32, 32}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
constexpr std::uint32_t unpack_tile_num_faces[24] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t unpack_tile_face_r_dim[24] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool unpack_partial_face[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool unpack_narrow_tile[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const std::int32_t arg_loop_count = 1;
