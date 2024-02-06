// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <tensix_types.h>

typedef struct {
    std::int32_t dst_tile_rows;
    std::int32_t dst_tile_cols;
    std::int32_t block_tile_dim;
    std::int32_t block_cnt;
} hlk_args_t;

const hlk_args_t hlk_args = {
    .dst_tile_rows = __dst_tile_rows__,
    .dst_tile_cols = __dst_tile_cols__,
    .block_cnt = __block_cnt__
};

const llk_unpack_AB_params_t llk_args = {
    .unpA_operand = (std::int32_t) 0,
    .unpB_operand = (std::int32_t) 1
};

constexpr std::int32_t unpack_src_format[24] = {(std::int32_t) DataFormat::__src0_format__, (std::int32_t) DataFormat::__src1_format__ ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
constexpr std::int32_t unpack_dst_format[24] = {(std::int32_t) DataFormat::__dst0_format__, (std::int32_t) DataFormat::__dst1_format__ ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

constexpr std::uint32_t unpack_tile_dims[24][2] = {{32, 32}, {32, 32}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
constexpr std::uint32_t unpack_tile_num_faces[24] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t unpack_tile_face_r_dim[24] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool unpack_partial_face[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool unpack_narrow_tile[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const std::int32_t arg_loop_count = __arg_loop_count__;
