// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"

namespace wait_pop_tiles_internal_helpers {

// Functionaly in this namespaces provides a way to iterate over all
// input streams while using iterator as a constexpr value and enable
// using "if constexpr" construct to check if kernel_broadcast is enabled
// or not. This is achieved using template meta-programming pattern.
// Regular index in the for loop can't be made constexpr.

// wait/pop tiles kernel broadcast
template <int stream, typename Function>
static TT_HLK_ALWAYS_INLINE void wait_pop_tile_kernel_broadcast(Function f, tt_core* core_ptr) {
    if constexpr (kernel_broadcast[stream] && !kernel_broadcast_per_t[stream]) {
        f(core_ptr, (HlkOperand)stream, kernel_broadcast[stream]);
    }
}

template <int i, typename Function>
struct wait_pop_tile_kernel_bcast_template_loop {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr) {
        wait_pop_tile_kernel_broadcast<i>(f, core_ptr);
        wait_pop_tile_kernel_bcast_template_loop<i - 1, Function>::call(f, core_ptr);
    }
};

template <typename Function>
struct wait_pop_tile_kernel_bcast_template_loop<0, Function> {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr) {
        wait_pop_tile_kernel_broadcast<0>(f, core_ptr);
    }
};

// wait/pop tiles kernel broadcast per t
template <int stream, typename Function>
static TT_HLK_ALWAYS_INLINE void wait_pop_tile_kernel_broadcast_per_t(Function f, tt_core* core_ptr) {
    if constexpr (kernel_broadcast[stream] && kernel_broadcast_per_t[stream]) {
        f(core_ptr, (HlkOperand)stream, kernel_broadcast[stream]);
    }
}

template <int i, typename Function>
struct wait_pop_tile_kernel_bcast_per_t_template_loop {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr) {
        wait_pop_tile_kernel_broadcast_per_t<i>(f, core_ptr);
        wait_pop_tile_kernel_bcast_per_t_template_loop<i - 1, Function>::call(f, core_ptr);
    }
};

template <typename Function>
struct wait_pop_tile_kernel_bcast_per_t_template_loop<0, Function> {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr) {
        wait_pop_tile_kernel_broadcast_per_t<0>(f, core_ptr);
    }
};

// Wait/pop tiles no broadcast.
template <int stream, typename Function>
static TT_HLK_ALWAYS_INLINE void wait_pop_tile_no_broadcast(Function f, tt_core* core_ptr, int block_tile_dim) {
    if constexpr (!kernel_broadcast[stream]) {
        f(core_ptr, (HlkOperand)stream, block_tile_dim);
    }
}

template <int i, typename Function>
struct wait_pop_tile_template_loop {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr, int block_tile_dim) {
        wait_pop_tile_no_broadcast<i>(f, core_ptr, block_tile_dim);
        wait_pop_tile_template_loop<i - 1, Function>::call(f, core_ptr, block_tile_dim);
    }
};

template <typename Function>
struct wait_pop_tile_template_loop<0, Function> {
    static TT_HLK_ALWAYS_INLINE void call(Function f, tt_core* core_ptr, int block_tile_dim) {
        wait_pop_tile_no_broadcast<0>(f, core_ptr, block_tile_dim);
    }
};
}  // namespace wait_pop_tiles_internal_helpers

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_kernel_broadcast(tt_core* core_ptr) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_kernel_bcast_template_loop<num_inputs - 1, decltype(hlk_wait_tiles)*>::call(hlk_wait_tiles, core_ptr);
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_kernel_broadcast(tt_core* core_ptr) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_kernel_bcast_template_loop<num_inputs - 1, decltype(hlk_pop_tiles<0>)*>::call(hlk_pop_tiles<0>, core_ptr);
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_kernel_broadcast_t(tt_core* core_ptr) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_kernel_bcast_per_t_template_loop<num_inputs - 1, decltype(hlk_wait_tiles)*>::call(hlk_wait_tiles, core_ptr);
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_kernel_broadcast_t(tt_core* core_ptr) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_kernel_bcast_per_t_template_loop<num_inputs - 1, decltype(hlk_pop_tiles<0>)*>::call(hlk_pop_tiles<0>, core_ptr);
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void wait_for_tiles_no_broadcast(tt_core* core_ptr, const hlk_args_t* args) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_template_loop<num_inputs - 1, decltype(hlk_wait_tiles)*>::call(
        hlk_wait_tiles, core_ptr, args->block_tile_dim);
}

template <int num_inputs>
static TT_HLK_ALWAYS_INLINE void pop_tiles_no_broadcast(tt_core* core_ptr, const hlk_args_t* args) {
    using namespace wait_pop_tiles_internal_helpers;
    wait_pop_tile_template_loop<num_inputs - 1, decltype(hlk_pop_tiles<0>)*>::call(
        hlk_pop_tiles<0>, core_ptr, args->block_tile_dim);
}

template <int input_index>
static TT_HLK_ALWAYS_INLINE int get_tile_index(const hlk_args_t *args, int tile_index, int block_index) {
    int result = tile_index;
    if constexpr (kernel_broadcast[input_index]) {
        result = (block_index * args->block_tile_dim + tile_index) % kernel_broadcast[input_index];
    }
    return result;
}