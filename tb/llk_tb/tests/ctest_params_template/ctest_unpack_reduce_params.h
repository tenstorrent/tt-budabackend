// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <tensix_types.h>

typedef struct {
    std::int32_t  num_block_rows;
    std::int32_t  num_block_columns;
    std::int32_t  num_tile_rows_per_block;
    std::int32_t  num_tile_columns_per_block;
} hlk_args_t;

const hlk_args_t hlk_args = {
    .num_block_rows             = __num_block_rows__,           
    .num_block_columns          = __num_block_columns__,         
    .num_tile_rows_per_block    = __num_tile_rows_per_block__,   
    .num_tile_columns_per_block = __num_tile_columns_per_block__
};

const float tensor_dim_size = __tensor_dim_size__;
const float const_mult = 1/tensor_dim_size;

const llk_unpack_reduce_params_t llk_args = {
    .unpA_operand = 0,
};

constexpr std::int32_t unpack_src_format[24] = {(std::int32_t) DataFormat::__src_format__, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
constexpr std::int32_t unpack_dst_format[24] = {(std::int32_t) DataFormat::__dst_format__, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

constexpr std::uint32_t unpack_tile_dims[24][2] = {{32, 32}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
constexpr std::uint32_t unpack_tile_num_faces[24] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t unpack_tile_face_r_dim[24] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool unpack_partial_face[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool unpack_narrow_tile[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const std::int32_t arg_loop_count = __arg_loop_count__;
