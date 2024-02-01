// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "hlk_api.h"

constexpr int BINARY_MAX_NUM_INPUTS = 2;
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
    std::int32_t zero_point; 
};

// BINARY_KERNEL_TYPE will be defined when the appropriate binary kernel (add, subtract, mul) is ran.
// It's definition will be in tt_build/test_name/graph_name/op_index/hlk_compile_time_constants.h
#if !defined(BINARY_KERNEL_TYPE_DEFINED)
constexpr BinaryOp BINARY_KERNEL_TYPE = BinaryOp::Add;
#endif

template<BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void hlk_binary_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {

    hlk_hw_config_two_operands(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);

    // No effect on mul
    const int acc_to_dest = args->gradient_op;
    hlk_binary_op_init<binary_op>(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose, acc_to_dest);
}

template<BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void hlk_binary_main(tt_core *core_ptr, const hlk_args_t *args) {

    // No effect on mul 
    const int acc_to_dest = args->gradient_op;
    const bool gradient_op = args->gradient_op>0;

    if (gradient_op) {
        hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0); //reconfig df for packer
    }

    if constexpr (kernel_broadcast[0] && !kernel_broadcast_per_t[0]) {
        hlk_wait_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
    }
    if constexpr (kernel_broadcast[1] && !kernel_broadcast_per_t[1]) {
        hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
    }

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        if constexpr (kernel_broadcast[0] && kernel_broadcast_per_t[0]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
        }
        if constexpr (kernel_broadcast[1] && kernel_broadcast_per_t[1]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
        }

        for (int block = 0; block < args->block_cnt; ++block) {
            hlk_acquire_dst(core_ptr);

            if (gradient_op) {
                hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::intermed0, false, false);
                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in0, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in1); //reconfig df for src A register
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
                for (int i = 0; i < args->block_tile_dim; i++) {
                    hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, i, i, false);
                }
                hlk_pop_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
                hlk_binary_op_init_short<binary_op>(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose, acc_to_dest);
                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in1, HlkOperand::in1); //reconfig df for src A register
            }

            // Wait for tiles on the input if streaming or it is first kernel_broadcast
            if constexpr (!kernel_broadcast[0]) {
                hlk_wait_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);
            }

            if constexpr (!kernel_broadcast[1]) {
                hlk_wait_tiles(core_ptr, HlkOperand::in1, args->block_tile_dim);
            }

            // Eltwise Binary
            for (int t = 0; t < args->block_tile_dim; ++t) {
                int lindex = t;
                int rindex = t;
                if constexpr (kernel_broadcast[0]) {
                    lindex = (block * args->block_tile_dim + t) % kernel_broadcast[0];
                }
                if constexpr (kernel_broadcast[1]) {
                    rindex = (block * args->block_tile_dim + t) % kernel_broadcast[1];
                }
                hlk_binary_op<binary_op>(
                    core_ptr,
                    HlkOperand::in0,
                    HlkOperand::in1,
                    lindex,
                    rindex, 
                    t,
                    args->transpose);
            }
            if constexpr (!kernel_broadcast[0]) {
                hlk_pop_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);
            }

            if constexpr (!kernel_broadcast[1]) {
                hlk_pop_tiles(core_ptr, HlkOperand::in1, args->block_tile_dim);
            }

            // Wait for space in output
            if (gradient_op) {
                // Wait for space in output
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);

                for(int t = 0; t < args->block_tile_dim; ++t) {
                    hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::intermed0);
                }

                hlk_push_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
            } else {
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);

                for (int t = 0; t < args->block_tile_dim; ++t) {
                    hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::out0);
                }

                hlk_push_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);
            }
            hlk_release_dst(core_ptr);
        }

        if constexpr (kernel_broadcast[0] && kernel_broadcast_per_t[0]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
        }
        if constexpr (kernel_broadcast[1] && kernel_broadcast_per_t[1]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
        }
    }

    if constexpr (kernel_broadcast[0] && !kernel_broadcast_per_t[0]) {
        hlk_pop_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
    }
    if constexpr (kernel_broadcast[1] && !kernel_broadcast_per_t[1]) {
        hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
    }
}