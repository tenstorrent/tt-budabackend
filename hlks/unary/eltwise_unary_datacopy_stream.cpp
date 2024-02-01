// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/eltwise_unary_common.h"
#include "hlks/inc/wait_pop_tiles_utils.h"


TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, args->transpose);
    hlk_copy_tile_to_dst_init(core_ptr, HlkOperand::in0, args->transpose, args->transpose);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    bool gradient_op = args->gradient_op>0;

    if (gradient_op) {
        hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in0, HlkOperand::in0, HlkOperand::in0, HlkOperand::intermed0); //reconfig df for src B register
        hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0); //reconfig df for packer
    }

    wait_for_tiles_kernel_broadcast<UNARY_MAX_NUM_INPUTS>(core_ptr);

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        wait_for_tiles_kernel_broadcast_t<UNARY_MAX_NUM_INPUTS>(core_ptr);

        for(int block = 0; block < args->block_cnt; ++block) {
            hlk_acquire_dst(core_ptr);
            bool first_tile = (block == 0);
            bool last_tile = (block == (args->block_cnt-1));

            // Wait for tiles on the input if streaming or it is first kernel_broadcast
            wait_for_tiles_no_broadcast<UNARY_MAX_NUM_INPUTS>(core_ptr, args);

            if (gradient_op) {
                hlk_binary_op_init_short<BinaryOp::Add>(core_ptr, HlkOperand::in0, HlkOperand::intermed0, 0, 0);
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
                for (int i = 0; i < args->block_tile_dim; i++) {
                    hlk_binary_op<BinaryOp::Add>(
                        core_ptr,
                        HlkOperand::in0,
                        HlkOperand::intermed0,
                        get_tile_index<0>(args, i, block),
                        i,
                        i,
                        args->transpose);
                }
                hlk_pop_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
            } else {
                for(int t = 0; t < args->block_tile_dim; ++t) {
                    hlk_copy_tile_to_dst(
                        core_ptr, 
                        HlkOperand::in0, 
                        get_tile_index<0>(args, t, block), 
                        t,
                        args->transpose);
                }
            }

            pop_tiles_no_broadcast<UNARY_MAX_NUM_INPUTS>(core_ptr, args);

            if (gradient_op) {
                // Wait for space in output
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);

                for(int t = 0; t < args->block_tile_dim; ++t) {
                    hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::intermed0);
                }

                hlk_push_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
            } else {	       
                // Wait for space in output
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);

                for(int t = 0; t < args->block_tile_dim; ++t) {
                    hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::out0);
                }

                hlk_push_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);
            }	   
            hlk_release_dst(core_ptr);
        }
        pop_tiles_kernel_broadcast_t<UNARY_MAX_NUM_INPUTS>(core_ptr);
    }
    pop_tiles_kernel_broadcast<UNARY_MAX_NUM_INPUTS>(core_ptr);
}
