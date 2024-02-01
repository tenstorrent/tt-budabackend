// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_kernel_broadcast(tt_core *core_ptr) {
    for (int i = 0; i < num_inputs; i++) {
        if (kernel_broadcast[i] && !kernel_broadcast_per_t[i]) {
            hlk_wait_tiles(core_ptr, (HlkOperand)i, kernel_broadcast[i]);
        }
    }
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_kernel_broadcast_t(tt_core *core_ptr) {
    for (int i = 0; i < num_inputs; i++) {
        if (kernel_broadcast[i] && kernel_broadcast_per_t[i]) {
            hlk_wait_tiles(core_ptr, (HlkOperand)i, kernel_broadcast[i]);
        }
    }
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_kernel_broadcast(tt_core *core_ptr) {
    for (int i = 0; i < num_inputs; i++) {
        if (kernel_broadcast[i] && !kernel_broadcast_per_t[i]) {
            hlk_pop_tiles(core_ptr, (HlkOperand)i, kernel_broadcast[i]);
        }
    }
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_kernel_broadcast_t(tt_core *core_ptr) {
    for (int i = 0; i < num_inputs; i++) {
        if (kernel_broadcast[i] && kernel_broadcast_per_t[i]) {
            hlk_pop_tiles(core_ptr, (HlkOperand)i, kernel_broadcast[i]);
        }
    }
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_no_broadcast(tt_core *core_ptr, const hlk_args_t *args) {
    for (int i = 0; i < num_inputs; i++) {
        if (!kernel_broadcast[i]) {
            hlk_wait_tiles(core_ptr, (HlkOperand)i, args->block_tile_dim);
        }
    }
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_no_broadcast(tt_core *core_ptr, const hlk_args_t *args) {
    for (int i = 0; i < num_inputs; i++) {
        if (!kernel_broadcast[i]) {
            hlk_pop_tiles(core_ptr, i, args->block_tile_dim);
        }
    }
}

template <int input_index>
static TT_HLK_ALWAYS_INLINE int get_tile_index(const hlk_args_t *args, int tile_index, int block_index) {
    int result = tile_index;
    if constexpr (kernel_broadcast[input_index]) {
        result = (block_index * args->block_tile_dim + tile_index) % kernel_broadcast[input_index];
    }
    return result;
}