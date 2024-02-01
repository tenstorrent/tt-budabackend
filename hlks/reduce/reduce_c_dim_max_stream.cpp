// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/reduce_common.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_reduce<ReduceFunc::Max, Dim::C>(core_ptr, HlkOperand::in0, 1.0f);
    hlk_reduce_tile_init_short<ReduceFunc::Max, Dim::C>(core_ptr, 0);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    int in_block_tile_cnt = args->num_tiles_per_m_sub_block*args->block_tile_dim; // block_tile_dim == ublock_kt == in_ublock_ct

    for (int b = 0; b < args->batch_cnt; b++) {

        for(int out_r=0;out_r<args->num_m_sub_blocks;out_r++) {

            hlk_acquire_dst(core_ptr);

            // reduce across blocks in the col dim (block_cnt == mblock_k == in_mblock_n)
            for(int block=0; block < args->block_cnt; ++block)
            {
                hlk_wait_tiles(core_ptr, HlkOperand::in0, in_block_tile_cnt);
                
                int in_index = 0;
                int dst_index = 0;
                for(int in_r=0;in_r<args->num_tiles_per_m_sub_block;in_r++) {
                    dst_index = in_r;			
                    // reduce a row within a block
                    for(int in_c=0;in_c<args->block_tile_dim;in_c++) {
                        hlk_reduce_tile<ReduceFunc::Max, Dim::C>(core_ptr, HlkOperand::in0, in_index, dst_index, 1.0f);
                        in_index++;
                    }
                }
                hlk_pop_tiles(core_ptr, HlkOperand::in0, in_block_tile_cnt);
            }

            // Pack out
            hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->num_tiles_per_m_sub_block); 
            for(int in_r=0 ; in_r<args->num_tiles_per_m_sub_block;in_r++)
            {
                hlk_pack_tile_to_stream(core_ptr, in_r, HlkOperand::out0);
            }
            hlk_push_tiles(core_ptr, HlkOperand::out0, args->num_tiles_per_m_sub_block); 

            hlk_release_dst(core_ptr);
        }	    
    }
}
