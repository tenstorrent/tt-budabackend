// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/tilizer_stream.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, 0);
    hlk_tilize_init_short(core_ptr, HlkOperand::in0, args->in_num_tiles_per_n_sub_block);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    for(int batch = 0; batch < args->batch_cnt; ++batch) {

        for(int m = 0; m < args->num_m_sub_blocks; ++m) {

            for(int rt = 0;  rt < args->num_tiles_per_m_sub_block; ++rt) {

                for(int in_n = 0;  in_n < args->in_num_n_sub_blocks; ++in_n) {

                    hlk_wait_tiles(core_ptr, HlkOperand::in0, args->in_num_tiles_per_n_sub_block);

                    int in0_offset = 0;

                    for(int n = 0;  n < args->in_num_tiles_per_n_sub_block/args->num_tiles_per_n_sub_block; ++n) {

                        hlk_acquire_dst(core_ptr);

                        for(int t = 0; t < args->num_tiles_per_n_sub_block; ++t) {
                            hlk_tilize(
                              core_ptr, 
                              HlkOperand::in0, 
                              in0_offset+t,
                              t,
                              args->in_num_tiles_per_n_sub_block);
                        }

                        in0_offset+=args->num_tiles_per_n_sub_block;

                        // Wait for space in output
                        hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->num_tiles_per_n_sub_block);

                        for(int t = 0; t < args->num_tiles_per_n_sub_block; ++t) {
                            hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::out0);
                        }

                        hlk_push_tiles(core_ptr, HlkOperand::out0, args->num_tiles_per_n_sub_block);
                          
                        hlk_release_dst(core_ptr);
                    }    

                    // Pop tiles on the input if streaming or it is last kernel_broadcast
                    hlk_pop_tiles<(int)true>(core_ptr, HlkOperand::in0, args->in_num_tiles_per_n_sub_block);
                }    

            }    
        }
    }
}
