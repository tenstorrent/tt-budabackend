// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include <cstdint>

constexpr int MATMUL_IDENT_MAX_NUM_INPUTS = 4;

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
    int num_index_tiles;
    int num_sparse_tiles;
    int num_columns;
    int sparse_tile_ptr_bits;
    int sparse_ublock_idx_bits;
    int block_tile_dim_bits;
    int indices_len;
    int starting_row;
    int bias;
    int relu_config;
    int kernel_broadcast[MATMUL_IDENT_MAX_NUM_INPUTS];
    int kernel_broadcast_per_t[MATMUL_IDENT_MAX_NUM_INPUTS];
    int l1_acc_en;
    int shared_buffer; //interm data format != output data format
    int act_t; //post tm activation t
    int adv_features_en; // arch specific control
};