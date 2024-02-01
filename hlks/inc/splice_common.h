// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include <cstdint>

const int SPLICE_MAX_NUM_INPUTS = 8;
const int SPLICE_INPUT_IDX_COLUMNS = 3;
struct hlk_args_t {
    std::int32_t block_tile_dim;
    std::int32_t block_cnt;
    std::int32_t batch_cnt;
    std::int32_t num_m_sub_blocks;
    std::int32_t num_n_sub_blocks;
    std::int32_t num_tiles_per_m_sub_block;
    std::int32_t num_tiles_per_n_sub_block;
    std::int32_t gradient_op;
    std::int32_t transpose;
    std::int32_t num_inputs;
    std::int32_t input_idx[SPLICE_MAX_NUM_INPUTS][SPLICE_INPUT_IDX_COLUMNS];  // Max 8 inputs
};