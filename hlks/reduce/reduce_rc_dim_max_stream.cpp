// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/reduce_common.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_reduce<ReduceFunc::Max, Dim::RC>(core_ptr, HlkOperand::in0, 1.0f);
    hlk_reduce_tile_init_short<ReduceFunc::Max, Dim::RC>(core_ptr, 0);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    for (int b = 0; b < args->batch_cnt; b++) {

        hlk_acquire_dst(core_ptr);

        // reduce across blocks in the col dim (they all reduce onto num_tiles_per_m_sub_block)
        for(int in_block_idx=0; in_block_idx < args->block_cnt; ++in_block_idx)
        {
            hlk_wait_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);

            int dst_tile_index = 0;
            int input_tile_index = 0;
            for(int r=0;r<args->num_tiles_per_m_sub_block;++r)
            {
                // reduce a row within a block
                for(int c=0;c<args->num_tiles_per_n_sub_block;++c)
                {
                    hlk_reduce_tile<ReduceFunc::Max, Dim::RC>(core_ptr, HlkOperand::in0, input_tile_index, dst_tile_index, 1.0f);
                    input_tile_index++;
                }
            }
            hlk_pop_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);
        }

        // Pack out
        hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, 1);
        hlk_pack_tile_to_stream(core_ptr, 0, HlkOperand::out0);
        hlk_push_tiles(core_ptr, HlkOperand::out0, 1);

        hlk_release_dst(core_ptr);
    }
}
