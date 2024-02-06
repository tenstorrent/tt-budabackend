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
    std::int32_t num_m_sub_blocks;
    std::int32_t num_n_sub_blocks;
    std::int32_t num_tiles_per_m_sub_block;
    std::int32_t num_tiles_per_n_sub_block;
    std::int32_t gradient_op;
    std::int32_t transpose;

    // Reduce Z dim specific
    std::int32_t z_dim;
    bool is_32bit_dest_en;
};