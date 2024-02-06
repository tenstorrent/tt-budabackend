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
    .dst_tile_rows = __dst_tile_rows__,
    .dst_tile_cols = __dst_tile_cols__,
    .block_cnt = __block_cnt__
};

const llk_pack_params_t llk_args = {
    .pack_output = 16,
    .relu_config = {.f = {.ApplyRelu=0, .Threshold=0}}
};

constexpr std::int32_t pack_src_format[16]   = {(std::int32_t) DataFormat::__src_format__ ,0,0,0,0,0,0,0,(std::int32_t) DataFormat::__intermediate0_src_format__,0,0,0,0,0,0,0};
constexpr std::int32_t pack_dst_format[16]   = {(std::int32_t) DataFormat::__dst_format__ ,0,0,0,0,0,0,0,(std::int32_t) DataFormat::__intermediate0_dst_format__,0,0,0,0,0,0,0};
constexpr std::uint32_t pack_tile_dims[16][2] = {{32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}};
constexpr std::uint32_t pack_tile_num_faces[16] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t pack_tile_face_r_dim[16] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool pack_partial_face[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool pack_narrow_tile[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const std::int32_t arg_loop_count = __arg_loop_count__;
