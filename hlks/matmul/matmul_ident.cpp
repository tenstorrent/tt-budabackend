// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/matmul_ident.h"
#include "hlks/inc/hlk_api.h"

constexpr int LOCAL_INDEX_ARRAY_SIZE = 32;

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
   hlk_hw_config_matmul(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
   hlk_mm_tile_init(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    typedef union strip_info_struct
    {
        struct 
        {
          uint16_t enc[2];
          uint16_t nz_ublocks;
          uint16_t index_array[];
        } val;

        struct 
        {
          uint16_t index_l:16;
          uint16_t index_h:14;
          uint16_t last_strip_in_row:1;
          uint16_t last_strip_in_tile:1;
          uint16_t nz_ublocks;
          uint16_t index_array[];
        } f;
   } strip_info_struct;   

   volatile strip_info_struct *strip_info_ptr;
   int nz_strips;
   int strips_info_in_tile;
   const int ublock_tile_index_bits = 16 - args->sparse_tile_ptr_bits;
   const int ublock_index_bits = args->sparse_ublock_idx_bits;
   const int ublock_tile_inner_d_bits = args->block_tile_dim_bits;
   const int num_tiles_in_ublock_bits = 16 - args->sparse_ublock_idx_bits;

   const bool spill = true; //args->block_cnt > 1; //(outer_r>1) or (outer_c>1);
   const bool relu_en = args->relu_config>0;
   const bool l1_acc_en = (args->l1_acc_en>0);
   const bool shared_buffer = args->shared_buffer>0;
   const bool gradient_op = args->gradient_op>0;
   const bool bias = args->bias>0;
   bool just_popped_strip_info_tile = true;
   const int inner_r = args->num_tiles_per_m_sub_block; // inner row block size in tiles
   const int inner_c = args->num_tiles_per_n_sub_block; // inner column block size in tiles
   const int inner_d = args->block_tile_dim; // inner block size in tiles
   const int outer_r = args->num_m_sub_blocks; // outer row block size (in inner row blocks)
   const int outer_c = args->num_n_sub_blocks; // outer column block size (in inner column blocks)
   const int outer_id = args->block_cnt; // outer inner dim (in inner dim blocks)
   const int in0_block_tile_cnt = inner_r*inner_d*outer_r;
   const int in1_block_tile_cnt = inner_c*inner_d*outer_c;
   const int out_block_tile_cnt = inner_r * inner_c;
   const int out_block_cnt = outer_r * outer_c;
   const int out_tile_cnt = out_block_cnt*out_block_tile_cnt; // 
   int index_tile_cntr = 0;
   bool enable_reload = gradient_op;
   bool l1_acc = false;
   constexpr bool in1_kernel_broadcast = kernel_broadcast[1]>0;
   constexpr bool in2_kernel_broadcast = kernel_broadcast[2]>0;
   constexpr bool in3_kernel_broadcast = kernel_broadcast[3]>0;
   const bool copy_to_local_mem = outer_c>=2;
   bool out_format = true; //by defualt format is configured for output
   const bool adv_features_en = args->adv_features_en>0;
   //uint32_t max_non_zero_tiles = 0;
   //uint32_t non_zero_ublocks = 0;

   hlk_wait_tiles(core_ptr, HlkOperand::in0, args->num_sparse_tiles);

   if constexpr (!in1_kernel_broadcast) {
      hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
   } else if constexpr (!kernel_broadcast_per_t[1]) {
      hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
   }

   if constexpr (in2_kernel_broadcast && !kernel_broadcast_per_t[2]) {
      hlk_wait_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
   }

   if constexpr (in3_kernel_broadcast && !kernel_broadcast_per_t[3]) {
      hlk_wait_tiles(core_ptr, HlkOperand::in3, kernel_broadcast[3]);
   }


   int batch = 0;
   int oid = 0;
   bool reload_bcast = true;
   while (batch < args->batch_cnt) {
      if (reload_bcast) {
         if constexpr (in1_kernel_broadcast && kernel_broadcast_per_t[1]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
         }

         if constexpr (in2_kernel_broadcast && kernel_broadcast_per_t[2]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
         }

         if constexpr (in3_kernel_broadcast && kernel_broadcast_per_t[3]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in3, kernel_broadcast[3]);
         }
      }

      // If appropriate wait for new sparsity info tile to be in the
      // receive buffer for Operand 2
      if(just_popped_strip_info_tile)
      {
         just_popped_strip_info_tile = false;
         // Wait for tile with sparsity information
         if constexpr (!in2_kernel_broadcast) {
            hlk_wait_tiles(core_ptr, HlkOperand::in2, 1);
         }
         // Map the struct pointer/interface to beginning of tile
         hlk_get_tile(core_ptr, HlkOperand::in2, 0, (std::uint32_t*) &strip_info_ptr);
      }
      bool last_out = strip_info_ptr->f.last_strip_in_row;
      bool last_strip_in_tile = strip_info_ptr->f.last_strip_in_tile;

      // Disable l1 acc for last output unless we are adding bias or doing gradient accumulation
      if (last_out and !gradient_op and !bias) {
         if (!out_format) {
            hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0, HlkOperand::out0);
            out_format = true;
         }   
         // Skip if l1 acc is already disabled
         if (l1_acc_en and l1_acc and (!shared_buffer or relu_en)) {
             hlk_reconfig_packer_l1_acc(core_ptr, 0);
             l1_acc = false;
         }    
      } else {
         if (out_format) {
            hlk_reconfig_packer_df(core_ptr, HlkOperand::out0, HlkOperand::intermed0);
            out_format = false;
         } 

      }   

      // check whether left input strip is zeros
      uint32_t strip_index = (((uint32_t(strip_info_ptr->f.index_h) << 16u) | uint32_t(strip_info_ptr->f.index_l)));

      for (; oid < (int)strip_index; ++oid) {
         // Wait for right input strip to come in and pop it
         if constexpr (!in1_kernel_broadcast) {
            hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
            hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
         }   
      }

      // below two loops move through u-blocks
      uint32_t nz_ublocks_in_strip = strip_info_ptr->f.nz_ublocks;
      uint32_t current_index = 0;
      int32_t current_ublock_index = 0;
      uint32_t nz_tiles_in_ublock = 0;
      uint32_t ublock_cntr=0;
      uint16_t index_array[LOCAL_INDEX_ARRAY_SIZE];
      volatile uint16_t *p_index_array = nullptr;

      for (int out_r = 0; out_r < outer_r; out_r++) {
         // Increment to next sparse u-block - current index expected
         // to be pointing to the next u-block index byte
         bool left_ublock_zero = true;
         if (nz_ublocks_in_strip>0) {
            uint32_t encoded_index = strip_info_ptr->f.index_array[current_index]; 
            current_ublock_index = encoded_index & ((1<<ublock_index_bits)-1);
            left_ublock_zero = (current_ublock_index != out_r) || (ublock_cntr >= nz_ublocks_in_strip);
            if (!left_ublock_zero) {
               nz_tiles_in_ublock = encoded_index >> ublock_index_bits;
               if (0 == nz_tiles_in_ublock) {
                  nz_tiles_in_ublock = (1<<num_tiles_in_ublock_bits); //special encoding for max tiles in ublock
               }
               ublock_cntr++; 
               current_index++; // move to tile index/ptr
               p_index_array = (volatile uint16_t*) &strip_info_ptr->f.index_array[current_index];
               if (copy_to_local_mem) { // copy to local memory buffer for max reuse
                  if (nz_tiles_in_ublock <= LOCAL_INDEX_ARRAY_SIZE) {
                     for (uint32_t n=0; n<nz_tiles_in_ublock; n++) {
                        index_array[n] = strip_info_ptr->f.index_array[current_index+n];
                     }
                     p_index_array = (volatile uint16_t*) &index_array[0];
                  }   
               }
               current_index+=nz_tiles_in_ublock;
            }    
         }   

         for (int out_c = 0; out_c < outer_c; out_c++) {

            if (!left_ublock_zero or !enable_reload or (last_out and !gradient_op and !bias and !shared_buffer)) {
               hlk_acquire_dst(core_ptr);
            }

            // Reload from l1 in following conditions
            // 1. Enable reload flag is true (set after first iteration)
            // 2. l1 accumulation is not enabled (last iteration with output buffer in bfp format)
            // 3. left ublock is non zero
            // 4. last iteration when buffer sharing disabled (interm and output data formats are different)
            if (enable_reload) {
               hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
               if (!l1_acc) {
                  if(!left_ublock_zero ||
                    (last_out and !gradient_op and !bias and !shared_buffer)) {
                     hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register, in1 = srcA
                     hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::intermed0, !adv_features_en, false);
                     for (int i = 0; i < out_block_tile_cnt; i++) {
                        hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, i, i, !adv_features_en);
                     }
                     hlk_mm_tile_init_short(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
                     hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register
                  }   
               }   
               hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
            }

            if(!left_ublock_zero)
            {
               // Compute MM Sub Block
               int dst_index = 0;
               for (uint32_t tile_idx = 0; tile_idx < nz_tiles_in_ublock; ++tile_idx) {
                  int32_t encoded_index = p_index_array[tile_idx];

                  int in0_index = encoded_index >> ublock_tile_index_bits;
                  int enc_index = (encoded_index & ((1<<ublock_tile_index_bits)-1));
                  int dst_index = (enc_index>>ublock_tile_inner_d_bits)*inner_c;
                  int in1_index = out_c*inner_c*inner_d + (enc_index & ((1<<ublock_tile_inner_d_bits)-1))*inner_c;

                     if constexpr (in1_kernel_broadcast) {
                        in1_index = in1_index  % kernel_broadcast[1];
                     }
                     hlk_mm_tile<kernel_broadcast[0], kernel_broadcast[1]>(
                         core_ptr,
                         HlkOperand::in0,
                         HlkOperand::in1,
                         in0_index,
                         in1_index,
                         dst_index,
                         args->transpose,
                         inner_c);
                     dst_index++;
               }
            }

            //inline Pack out to output or intermediate buffer
            // and pack out to stream happen regardless of left ublock input status
            // (unchanged from dense matmul_u HLK)
            if (last_out and !gradient_op and !bias) {
               // Pack out to output buffer
               if (relu_en) {
                   hlk_relu_config(core_ptr, args->relu_config);
               }
               hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
               if(!left_ublock_zero or !enable_reload ||
                    (last_out and !gradient_op and !bias and !shared_buffer)) {
                  for (int i = 0; i < out_block_tile_cnt; i++) {
                     hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::out0);
                  }
               }   
               hlk_push_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
               if (relu_en) {
                   hlk_relu_config(core_ptr, 0);
               }
            } else {
               // Move partial result to interm buffer unless ublock is zero or it's first strip
               hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
               if(!left_ublock_zero or !enable_reload) {
                  for (int i = 0; i < out_block_tile_cnt; i++) {
                     hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::intermed0);
                  }
               }
               hlk_push_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
            }

            if (!left_ublock_zero or !enable_reload || (last_out and !gradient_op and !bias and !shared_buffer)) {
               hlk_release_dst(core_ptr);
            } 
         }
      }

      if (last_out) {
         // Disable l1 accumulation if it's enabled before loading bias
         if (l1_acc_en and l1_acc) {
             hlk_reconfig_packer_l1_acc(core_ptr, 0);
             l1_acc = false;
         }

         if (bias) {
            // Reconfigure hw for running add with tile row brodcast
            if (adv_features_en) {
               hlk_binary_op_init_short<BinaryOp::Add, Dim::R>(core_ptr, HlkOperand::intermed0, HlkOperand::in3, 0, 0);
            } else {
               hlk_binary_op_init<BinaryOp::Add, Dim::R>(core_ptr, HlkOperand::intermed0, HlkOperand::in3, 0, 0);
            }
            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in3); //reconfig df for src B register
            hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0, HlkOperand::out0);
            out_format = true;

            if (relu_en) {
                hlk_relu_config(core_ptr, args->relu_config);
            }
            for (int block = 0; block < out_block_cnt; block++) {
               hlk_acquire_dst(core_ptr);
               if constexpr (!in3_kernel_broadcast) {
                  hlk_wait_tiles(core_ptr, HlkOperand::in3, out_block_tile_cnt);
               }   
               hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
               for (int t = 0; t < out_block_tile_cnt; ++t) {
                  int rindex = t;
                  if constexpr (in3_kernel_broadcast) {
                     rindex = (batch * out_block_cnt * out_block_tile_cnt + block * out_block_tile_cnt + t) %
                              kernel_broadcast[3];
                  }
                  hlk_binary_op<BinaryOp::Add, Dim::R>(
                      core_ptr, HlkOperand::intermed0, HlkOperand::in3, t, rindex, t, 0);
               }
               if constexpr (!in3_kernel_broadcast) {
                  hlk_pop_tiles(core_ptr, HlkOperand::in3, out_block_tile_cnt);
               }   
               hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
               hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
               for (int i = 0; i < out_block_tile_cnt; i++) {
                  hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::out0);
               }
               hlk_push_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
               hlk_release_dst(core_ptr);
            }  
            if (relu_en) {
                hlk_relu_config(core_ptr, 0);
            }
            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in3, HlkOperand::in0); //reconfig df for src B register
            if (adv_features_en) {
               hlk_mm_tile_init_short(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
            } else {
               hlk_mm_tile_init(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
            }
         }
         hlk_flush_tiles(core_ptr, HlkOperand::intermed0, out_tile_cnt);
         enable_reload = gradient_op;

         if constexpr (in1_kernel_broadcast && kernel_broadcast_per_t[1]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
         }

         if constexpr (in2_kernel_broadcast && kernel_broadcast_per_t[2]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
         }

         if constexpr (in3_kernel_broadcast && kernel_broadcast_per_t[3]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in3, kernel_broadcast[3]);
         }

         reload_bcast = true;
      } else {
         reload_bcast = false;
         if (spill) {
            enable_reload = true;
            if (l1_acc_en and !l1_acc) {
               hlk_reconfig_packer_l1_acc(core_ptr, 1);
               l1_acc = true;
            }
         }
      }
       
      
      if(last_strip_in_tile) {
         hlk_release_tile(core_ptr, HlkOperand::in2);
         if constexpr (!in2_kernel_broadcast) {
            hlk_pop_tiles(core_ptr, HlkOperand::in2, 1);
         }   
         just_popped_strip_info_tile = true;
         index_tile_cntr++;
      }

      // Move info pointer to next strip
      volatile uintptr_t strip_info_byte_ptr =
          (volatile uintptr_t)strip_info_ptr + sizeof(strip_info_struct) + current_index * sizeof(uint16_t);
      strip_info_ptr = (volatile strip_info_struct*)strip_info_byte_ptr;

      batch += int(last_out);
   }


   if constexpr (in1_kernel_broadcast) {
      if constexpr(!kernel_broadcast_per_t[1]) {
         hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
      }
   } else {
      hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
      for (; oid < (args->act_t * args->block_cnt - 1); ++oid) {
         hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
         hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
      }
   }   

   hlk_pop_tiles(core_ptr, HlkOperand::in0, args->num_sparse_tiles);

   if constexpr (in2_kernel_broadcast && !kernel_broadcast_per_t[2]) {
      hlk_pop_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
   }

   if constexpr (in3_kernel_broadcast && !kernel_broadcast_per_t[3]) {
      hlk_pop_tiles(core_ptr, HlkOperand::in3, kernel_broadcast[3]);
   }

   // Pop remaining index tiles
   if (args->num_index_tiles > index_tile_cntr) {
      int remaining_index_tiles = args->num_index_tiles - index_tile_cntr;
      for (int i=0; i<remaining_index_tiles; i++) {
         hlk_wait_tiles(core_ptr, HlkOperand::in2, 1);
         hlk_pop_tiles(core_ptr, HlkOperand::in2, 1);
      }
   }

   //hlk_debug_dump(core_ptr, (uint8_t*)&max_non_zero_tiles, 4);
   //hlk_debug_dump(core_ptr, (uint8_t*)&non_zero_ublocks, 4);
}


