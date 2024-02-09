// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include <cstdint>

constexpr int MATMUL_U_MAX_NUM_INPUTS = 3;

struct hlk_args_t {
    int block_tile_dim;
    int dst_tile_rows;
    int dst_tile_cols;
    int block_cnt;
    int batch_cnt;
    int in0_block_tile_cnt;
    int in1_block_tile_cnt;
    int out_block_tile_cnt;
    int num_m_sub_blocks;
    int num_n_sub_blocks;
    int num_tiles_per_m_sub_block;
    int num_tiles_per_n_sub_block;
    int num_tiles_per_sub_block;
    int gradient_op;
    int transpose;
    int bias;
    int accumulate;
    int z;
    int relu_config;
    int min_input_buffer[2];
    int sfpu_op;
    int sfpu_vector_mode;
    int l1_acc_en;
    int shared_buffer; // interm data format != output data format
    int adv_features_en; // arch specific control
};