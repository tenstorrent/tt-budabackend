// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/reduce_common.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
   // Take HlkOperand::intermed0 as args, as intermed tile dims match output tile dims
    hlk_unary_sfpu_init<SfpuOp::Max>(core_ptr, HlkOperand::intermed0);
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, false);
    hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::in0, false, false);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    const int32_t HALF_DEST_SIZE = args->is_32bit_dest_en ? 2 : 4;
    const int num_iter = args->block_tile_dim > HALF_DEST_SIZE ? 2 : 1; // Number of iterations through dest per block

    for (int b = 0; b < args->batch_cnt; b++) {

       for (int z = 0; z < args->z_dim; z++) {

          bool first_in = z == 0;  
          bool last_out = z == (args->z_dim-1);

          for(int block = 0; block < args->block_cnt; ++block) {

             // Wait for space in output
             if (last_out) {
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);
             } else {
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
             }   

             // Wait for tiles on the input
             hlk_wait_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);
             if (!first_in) {
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
             }

             int half_block_tile_dim = args->block_tile_dim > HALF_DEST_SIZE ? HALF_DEST_SIZE : args->block_tile_dim;
             for(int i = 0; i < num_iter; ++i) {
                 hlk_acquire_dst(core_ptr);
                 for(int t = 0; t < half_block_tile_dim; ++t) {
                     hlk_copy_tile_to_dst(core_ptr, HlkOperand::in0, i*HALF_DEST_SIZE+t, 2*t, false);
                     if (!first_in) {
                        hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, i*HALF_DEST_SIZE+t, 2*t+1, false);
                        hlk_unary_sfpu_op<SfpuOp::Max>(core_ptr, HlkOperand::intermed0, 2*t, (int)Dim::RC);
                     }

                     if (last_out) {
                        hlk_pack_tile_to_stream(core_ptr, 2*t, HlkOperand::out0);
                     } else {
                        hlk_pack_tile_to_stream(core_ptr, 2*t, HlkOperand::intermed0);
                     }
                 }
                 half_block_tile_dim = args->block_tile_dim - half_block_tile_dim;
                 hlk_release_dst(core_ptr);
             }     

             // Pop input and push to output 
             hlk_pop_tiles(core_ptr, HlkOperand::in0, args->block_tile_dim);

             if (!first_in) {
                hlk_pop_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);
             }   

             if (last_out) {
                hlk_push_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);
             } else {
                hlk_push_tiles(core_ptr, HlkOperand::intermed0, args->block_tile_dim);

             }


          }

       }   
    }
}
