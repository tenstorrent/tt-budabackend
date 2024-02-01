// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include <cstdint>

constexpr int DEPTHWISE_MAX_NUM_INPUTS = 3;

struct hlk_args_t {
    int batch_cnt;    // t dimension
    int block_tile_dim;    
    int num_m_sub_blocks;
    int num_n_sub_blocks;
    int num_tiles_per_m_sub_block;
    int num_tiles_per_n_sub_block;
    int block_cnt;    // number of blocks to accumulate - equal to number of compressed input1 matrices
    int gradient_op;
    int transpose;
    int bias;
    int relu_config;
    int min_input_buffer[2];
    int kernel_broadcast[DEPTHWISE_MAX_NUM_INPUTS];
    int kernel_broadcast_per_t[DEPTHWISE_MAX_NUM_INPUTS];
    int l1_acc_en;
    int shared_buffer; // interm data format != output data format
    int adv_features_en; // arch specific control
};