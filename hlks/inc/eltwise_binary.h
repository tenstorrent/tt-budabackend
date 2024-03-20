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
    std::int32_t is_32bit_dest_en;
};

#include "hlks/inc/wait_pop_tiles_utils.h"

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

    wait_for_tiles_kernel_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr);

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        wait_for_tiles_kernel_broadcast_t<BINARY_MAX_NUM_INPUTS>(core_ptr);

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
            wait_for_tiles_no_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr, args);

            // Eltwise Binary
            for (int t = 0; t < args->block_tile_dim; ++t) {
                hlk_binary_op<binary_op>(
                    core_ptr,
                    HlkOperand::in0,
                    HlkOperand::in1,
                    get_tile_index<0>(args, t, block),
                    get_tile_index<1>(args, t, block),
                    t,
                    args->transpose);
            }
            pop_tiles_no_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr, args);

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

        pop_tiles_kernel_broadcast_t<BINARY_MAX_NUM_INPUTS>(core_ptr);
    }

    pop_tiles_kernel_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr);
}