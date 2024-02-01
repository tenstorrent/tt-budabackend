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

constexpr hlk_args_t hlk_args = {
    .dst_tile_rows = __dst_tile_rows__,
    .dst_tile_cols = __dst_tile_cols__,
    .block_cnt = __block_cnt__
};

struct hlk_tile_info_t {
    std::uint8_t tile_index_within_out_buf;
    std::uint8_t buffer_index;
    std::uint8_t input_tile_index;
    std::uint8_t reserved;
};

constexpr hlk_tile_info_t tile_info[128] = {{.tile_index_within_out_buf = __tile0_index_within_out_buf__,  .buffer_index = 16 + __tile0_out_buf__} ,     //0
                                            {.tile_index_within_out_buf = __tile1_index_within_out_buf__,  .buffer_index = 16 + __tile1_out_buf__} ,     //1
                                            {.tile_index_within_out_buf = __tile2_index_within_out_buf__,  .buffer_index = 16 + __tile2_out_buf__} ,     //2
                                            {.tile_index_within_out_buf = __tile3_index_within_out_buf__,  .buffer_index = 16 + __tile3_out_buf__} ,     //3
                                            {.tile_index_within_out_buf = __tile4_index_within_out_buf__,  .buffer_index = 16 + __tile4_out_buf__} ,     //4
                                            {.tile_index_within_out_buf = __tile5_index_within_out_buf__,  .buffer_index = 16 + __tile5_out_buf__} ,     //5
                                            {.tile_index_within_out_buf = __tile6_index_within_out_buf__,  .buffer_index = 16 + __tile6_out_buf__} ,     //6
                                            {.tile_index_within_out_buf = __tile7_index_within_out_buf__,  .buffer_index = 16 + __tile7_out_buf__} ,     //7
                                            {.tile_index_within_out_buf = __tile8_index_within_out_buf__,  .buffer_index = 16 + __tile8_out_buf__} ,     //8
                                            {.tile_index_within_out_buf = __tile9_index_within_out_buf__,  .buffer_index = 16 + __tile9_out_buf__} ,     //9
                                            {.tile_index_within_out_buf = __tile10_index_within_out_buf__, .buffer_index = 16 + __tile10_out_buf__},     //10
                                            {.tile_index_within_out_buf = __tile11_index_within_out_buf__, .buffer_index = 16 + __tile11_out_buf__},     //11
                                            {.tile_index_within_out_buf = __tile12_index_within_out_buf__, .buffer_index = 16 + __tile12_out_buf__},     //12
                                            {.tile_index_within_out_buf = __tile13_index_within_out_buf__, .buffer_index = 16 + __tile13_out_buf__},     //13
                                            {.tile_index_within_out_buf = __tile14_index_within_out_buf__, .buffer_index = 16 + __tile14_out_buf__},     //14
                                            {.tile_index_within_out_buf = __tile15_index_within_out_buf__, .buffer_index = 16 + __tile15_out_buf__}      //15
                                        };

constexpr std::uint32_t hlk_num_input_tiles = __num_input_tiles__;

constexpr std::uint32_t hlk_num_outputs = __num_outputs__;
constexpr std::uint32_t hlk_num_output_tiles[8] = {__num_output0_tiles__,__num_output1_tiles__,__num_output2_tiles__,__num_output3_tiles__,0,0,0,0};



const llk_pack_params_t llk_args = {
    .pack_output = 16,
    .relu_config = {.f = {.ApplyRelu=__relu_mode__ , .Threshold=__relu_threshold__}}
};

constexpr std::int32_t pack_src_format[16]   = {(std::int32_t) DataFormat::__src0_format__ ,(std::int32_t) DataFormat::__src0_format__ ,(std::int32_t) DataFormat::__src0_format__ ,(std::int32_t) DataFormat::__src0_format__ ,0,0,0,0,0,0,0,0,0,0,0,0};
constexpr std::int32_t pack_dst_format[16]   = {(std::int32_t) DataFormat::__dst0_format__ ,(std::int32_t) DataFormat::__dst0_format__ ,(std::int32_t) DataFormat::__dst0_format__ ,(std::int32_t) DataFormat::__dst0_format__ ,0,0,0,0,0,0,0,0,0,0,0,0};

constexpr std::uint32_t pack_tile_dims[16][2] = {{32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}, {32, 32}};
constexpr std::uint32_t pack_tile_num_faces[16] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, };
constexpr std::uint32_t pack_tile_face_r_dim[16] = { 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, };
constexpr bool pack_partial_face[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
constexpr bool pack_narrow_tile[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

const std::int32_t arg_loop_count = __arg_loop_count__;
