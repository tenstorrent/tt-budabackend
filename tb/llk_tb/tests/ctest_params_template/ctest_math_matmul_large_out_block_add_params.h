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

const hlk_args_t hlk_args {
    .dst_tile_rows = __dst_tile_rows__,
    .dst_tile_cols = __dst_tile_cols__,
    .block_tile_dim = __block_tile_dim__,
    .block_cnt = __block_cnt__
};

constexpr std::uint32_t unpack_tile_dims[24][2] = {{32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}};
constexpr std::uint32_t unpack_tile_num_faces[24] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t unpack_tile_face_r_dim[24] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool unpack_partial_face[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool unpack_narrow_tile[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const uint arg_loop_count = __arg_loop_count__;
