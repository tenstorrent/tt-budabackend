// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "hlk_api.h"

struct hlk_args_t {
    std::int32_t block_tile_dim; 
    std::int32_t block_cnt;
    std::int32_t batch_cnt;
    std::int32_t num_m_sub_blocks; // output mblock_m
    std::int32_t num_n_sub_blocks; // output mblock_n
    std::int32_t num_tiles_per_m_sub_block; // output ublock_rt
    std::int32_t num_tiles_per_n_sub_block; // output ublock_ct
    std::int32_t in_num_n_sub_blocks; // Input mblock_n
    std::int32_t in_num_tiles_per_n_sub_block; // Input ublock_ct
};