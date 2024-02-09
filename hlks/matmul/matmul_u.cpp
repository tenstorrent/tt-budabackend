// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/matmul_u.h"
#include "hlks/inc/hlk_api.h"
#include "hlks/inc/wait_pop_tiles_utils.h"

#define RUN_SFPU_OP(core_ptr, hlk_operand, args, block_tile_dim) \
    if (args->sfpu_op == (int)SfpuOp::Gelu) { \
        for(int t = 0; t < block_tile_dim; ++t) { \
            hlk_unary_sfpu_op<SfpuOp::Gelu>(core_ptr, hlk_operand, t, args->sfpu_vector_mode); \
        } \
    } \
    
TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_matmul(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
    int inner_c_dim = args->num_tiles_per_n_sub_block;
    int inner_r_dim = args->num_tiles_per_m_sub_block;
    hlk_mm_block_init(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose, inner_c_dim, inner_r_dim, args->block_tile_dim);

    if (args->sfpu_op == (int)SfpuOp::Gelu) {
        // Take HlkOperand::intermed0 as args, as intermed tile dims match output tile dims
        hlk_unary_sfpu_init<SfpuOp::Gelu>(core_ptr, HlkOperand::intermed0);
    }
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    const bool accumulate = (args->accumulate>0);
    const bool relu_en = (args->relu_config>0);
    const bool l1_acc_en = (args->l1_acc_en>0);
    const bool shared_buffer = args->shared_buffer>0;
    const int acc_z = accumulate ? args->z : 1;
    const bool spill = (args->block_cnt>1) || (args->accumulate>0); //(outer_r>1) or (outer_c>1);
    const bool gradient_op = args->gradient_op>0;
    const bool bias = args->bias>0;
    const bool min_in0_buffer = args->min_input_buffer[0]>0;
    const bool min_in1_buffer = args->min_input_buffer[1]>0;
    const int inner_r = args->num_tiles_per_m_sub_block; // inner row block size in tiles
    const int inner_c = args->num_tiles_per_n_sub_block; // inner column block size in tiles
    const int inner_d = args->block_tile_dim; // inner block size in tiles
    const int outer_r = args->num_m_sub_blocks; // outer row block size (in inner row blocks)
    const int outer_c = args->num_n_sub_blocks; // outer column block size (in inner column blocks)
    const int outer_id = args->block_cnt; // outer inner dim (in inner dim blocks)
    const int in0_block_tile_cnt = min_in0_buffer ? inner_r*inner_d : inner_r*inner_d*outer_r;
    const int in1_block_tile_cnt = min_in1_buffer ? inner_c*inner_d : inner_c*inner_d*outer_c;
    const int out_block_cnt = outer_r*outer_c;
    const int out_block_tile_cnt = inner_r * inner_c;
    const bool adv_features_en = args->adv_features_en>0;

    if (gradient_op) {
        hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0);
    }

    if constexpr (kernel_broadcast[0] && !kernel_broadcast_per_t[0]) {
        hlk_wait_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
    }

    if constexpr (kernel_broadcast[1] && !kernel_broadcast_per_t[1]) {
        hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
    }

    if constexpr (kernel_broadcast[2] && !kernel_broadcast_per_t[2]) {
        hlk_wait_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
    }

    for (int batch = 0; batch < args->batch_cnt; batch++) {
        if constexpr (kernel_broadcast[0] && kernel_broadcast_per_t[0]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
        }

        if constexpr (kernel_broadcast[1] && kernel_broadcast_per_t[1]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
        }

        if constexpr (kernel_broadcast[2] && kernel_broadcast_per_t[2]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
        }

        bool enable_reload = gradient_op;
        bool l1_acc = false;

        if ((spill || bias) && (!gradient_op))  {
            hlk_reconfig_packer_df(core_ptr, HlkOperand::out0, HlkOperand::intermed0);
        }

        if (relu_en) {
            hlk_relu_config(core_ptr, 0);
        }

        for(int z = 0; z < acc_z; z++) {

        bool last_z = !accumulate or (z == (acc_z-1));

        for(int oid = 0; oid < outer_id; oid++)
        {
            bool last_out = (oid == (outer_id-1));

            if (last_out and !gradient_op and !bias and last_z) {
                hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0, HlkOperand::out0);
                if (relu_en) {
                     hlk_relu_config(core_ptr, args->relu_config);
                }
                if (l1_acc_en and l1_acc and (!shared_buffer || (args->sfpu_op != (int)SfpuOp::Invalid) || relu_en)) {
                    hlk_reconfig_packer_l1_acc(core_ptr, 0);
                    l1_acc = false;
                }
            }

            if (!min_in0_buffer and (not kernel_broadcast[0])) {
                // Buffer vertical strip
                hlk_wait_tiles(core_ptr, HlkOperand::in0, in0_block_tile_cnt);
            }

            if (!min_in1_buffer and (not kernel_broadcast[1])) {
                // Buffer horizontal strip
                hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
            }

            if (min_in1_buffer) {
                // Buffer vertical strip for in0
                // Buffer input block for in1
                // Blocks will be computed in block column major order in output
                // Wait for entire buffer to be empty, write all blocks to the interm/output 
                // and then push blocks

                if (enable_reload) {
                    hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_cnt*out_block_tile_cnt);
                }

                if (last_out and !gradient_op and !bias and last_z) {
                    // Wait for output buffer to be freed up
                    hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt*out_block_cnt);
                } else {
                    // Wait for interned buffer to be freed up
                    if (!l1_acc) {
                        hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt*out_block_cnt);
                    }
                }

                // Compute blocks in column major order as we hold only single block for in0
                for (int out_c = 0; out_c < outer_c; out_c++) {

                    // Wait for in1 block to be ready
                    if constexpr (!kernel_broadcast[1]) {
                        hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
                    }
                    for (int out_r = 0; out_r < outer_r; out_r++) {

                        // Read each in0 block in vertical strip and run math

                        hlk_acquire_dst(core_ptr);

                        if (enable_reload) {
                            if (!l1_acc) {
                                hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::intermed0, !adv_features_en, false);
                                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register, in1 = srcA
                                int in_index = (out_r*outer_c + out_c)*out_block_tile_cnt;
                                for (int i = 0; i < out_block_tile_cnt; i++) {
                                   hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, in_index , i, !adv_features_en);
                                   in_index++;
                                }
                                int inner_c_dim = args->num_tiles_per_n_sub_block;
                                int inner_r_dim = args->num_tiles_per_m_sub_block;
                                hlk_mm_block_init_short(core_ptr,
                                                        HlkOperand::in0,
                                                        HlkOperand::in1,
                                                        args->transpose,
                                                        inner_c_dim, 
                                                        inner_r_dim, 
                                                        args->block_tile_dim);
                                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register
                            }    
                        }

                        // Compute MM Sub Block
                        for (int in_d = 0; in_d < inner_d; in_d++) {
                            int dst_index = 0;
                            int in0_index = out_r*inner_r*inner_d + in_d;
                            int in1_index = in_d*inner_c;
                            if constexpr (kernel_broadcast[0]) {
                                in0_index = (oid*inner_r*inner_d*outer_r + out_r*inner_r*inner_d + in_d)%kernel_broadcast[0];
                            } 
                            if constexpr (kernel_broadcast[1]) {
                                in1_index = (oid*inner_c*inner_d*outer_c + out_c*inner_c*inner_d + in_d*inner_c)%kernel_broadcast[1];
                            }
                            hlk_mm_block<kernel_broadcast[0], kernel_broadcast[1]>(
                                core_ptr, 
                                HlkOperand::in0, 
                                HlkOperand::in1,
                                in0_index,
                                in1_index,
                                dst_index, 
                                args->transpose,
                                inner_c,
                                inner_r,
                                inner_d);

                            dst_index+=inner_c;    
                        }

                        // Run sfpu op if valid
                        if (last_out and !gradient_op and !bias and last_z) {
                            if (args->sfpu_op != (int)SfpuOp::Invalid) {
                                // Take HlkOperand::intermed0 as args, as intermed tile dims match output tile dims
                                RUN_SFPU_OP(core_ptr, HlkOperand::intermed0, args, out_block_tile_cnt);
                            }    
                        }    

                        int out_index = (out_r*outer_c + out_c)*out_block_tile_cnt;
                        if (last_out and !gradient_op and !bias and last_z) {
                            // Pack final results to output buffer
                            for (int i = 0; i < out_block_tile_cnt; i++) {
                               hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::out0, out_index);
                               out_index++;
                            }
                        } else {
                            // Pack partial result to interm buffer
                            for (int i = 0; i < out_block_tile_cnt; i++) {
                               hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::intermed0, out_index);
                               out_index++;
                            }
                        }

                        hlk_release_dst(core_ptr);
                    }

                    // Pop in1 block
                    if constexpr (!kernel_broadcast[1]) {
                        hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
                    }
                }

                if (enable_reload) {
                    // Pop all blocks from interm buffer
                    // Use for loop to match number of pops with number of output blocks 
                    // Required when interm and output buffers are shared
                    for (int i = 0; i < out_block_cnt; i++) {
                        hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                    }
                }    

                if (last_out and !gradient_op and !bias and last_z) {
                    hlk_push_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt*out_block_cnt);
                } else {
                    // Push all blocks to interm buffer
                    // Use for loop to match number of pushes with number of output blocks 
                    // Required when interm and output buffers are shared
                    for (int i = 0; i < out_block_cnt; i++) {
                        hlk_push_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                    }    
                }   

            } else {
                // Buffer vertical strip or input block for in0
                // Buffer input strip for in1
                for (int out_r = 0; out_r < outer_r; out_r++) {

                    if (min_in0_buffer and (not kernel_broadcast[0])) {
                         hlk_wait_tiles(core_ptr, HlkOperand::in0, in0_block_tile_cnt);
                    }

                    for (int out_c = 0; out_c < outer_c; out_c++) {

                        hlk_acquire_dst(core_ptr);

                        if (enable_reload) {
                            hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                            if (!l1_acc) {
                                hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::intermed0, !adv_features_en, false);
                                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register, in1 = srcA
                                for (int i = 0; i < out_block_tile_cnt; i++) {
                                   hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, i, i, !adv_features_en);
                                }
                                hlk_mm_block_init_short(core_ptr,
                                                        HlkOperand::in0,
                                                        HlkOperand::in1,
                                                        args->transpose, 
                                                        args->num_tiles_per_n_sub_block, 
                                                        args->num_tiles_per_m_sub_block, 
                                                        args->block_tile_dim);
                                hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register
                            }    
                            hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                        }

                        // Compute MM Sub Block
                        for (int in_d = 0; in_d < inner_d; in_d++) {
                            int dst_index = 0;
                            int in0_index = (min_in0_buffer ? 0 : out_r*inner_r*inner_d) + in_d;
                            int in1_index = out_c*inner_c*inner_d + in_d*inner_c;

                            if constexpr (kernel_broadcast[0]) {
                                in0_index = (oid*inner_r*inner_d*outer_r + out_r*inner_r*inner_d + in_d)%kernel_broadcast[0];
                            } 
                            if constexpr (kernel_broadcast[1]) {
                                in1_index = (oid*inner_c*inner_d*outer_c + out_c*inner_c*inner_d + in_d*inner_c)%kernel_broadcast[1];
                            }
                            hlk_mm_block<kernel_broadcast[0], kernel_broadcast[1]>(
                                core_ptr, 
                                HlkOperand::in0, 
                                HlkOperand::in1,
                                in0_index,
                                in1_index,
                                dst_index, 
                                args->transpose,
                                inner_c,
                                inner_r,
                                inner_d);

                            dst_index+=inner_c;    
                        }

                        // Run sfpu op if valid
                        if (last_out and !gradient_op and !bias and last_z) {
                            if (args->sfpu_op != (int)SfpuOp::Invalid) {
                                // Take HlkOperand::intermed0 as args, as intermed tile dims match output tile dims
                                RUN_SFPU_OP(core_ptr, HlkOperand::intermed0, args, out_block_tile_cnt);
                            }    
                        }    

                        if (last_out and !gradient_op and !bias and last_z) {
                            // Pack final results to output buffer
                            hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
                            for (int i = 0; i < out_block_tile_cnt; i++) {
                               hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::out0);
                            }
                            hlk_push_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt);
                        } else {
                            // Move partial result to interm buffer
                            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                            for (int i = 0; i < out_block_tile_cnt; i++) {
                               hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::intermed0);
                            }
                            hlk_push_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                        }

                        hlk_release_dst(core_ptr);
                    }

                    if (min_in0_buffer and (not kernel_broadcast[0])) {
                        // Pop in0 block
                        hlk_pop_tiles(core_ptr, HlkOperand::in0, in0_block_tile_cnt);
                    }     
                }
            } 

            if (spill) {
                enable_reload = true;
                if (l1_acc_en) {
                    if (oid == 0) {
                        hlk_reconfig_packer_l1_acc(core_ptr, 1);
                        l1_acc = true;
                    }
                }    
            }    

            if (!min_in0_buffer and (not kernel_broadcast[0])) {
                // Pop in0 vertical strip
                hlk_pop_tiles(core_ptr, HlkOperand::in0, in0_block_tile_cnt);
            }

            if (!min_in1_buffer and (not kernel_broadcast[1])) {
                // Pop in1 horizontal strip
                hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
            }   
            
        }

        }

        if (l1_acc) {
            hlk_reconfig_packer_l1_acc(core_ptr, 0);
            l1_acc = false;
        }

        if (bias) {
            // Reconfigure hw for running add with tile row brodcast
            if (args->adv_features_en != 0) {
                hlk_binary_op_init_short<BinaryOp::Add, Dim::R>(core_ptr, HlkOperand::intermed0, HlkOperand::in2, 0, 0);
            } else {
                hlk_binary_op_init<BinaryOp::Add, Dim::R>(core_ptr, HlkOperand::intermed0, HlkOperand::in2, 0, 0);
            }

            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in2); //reconfig df for src B register
            hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0, HlkOperand::out0);
            if (relu_en) {
                hlk_relu_config(core_ptr, args->relu_config);
            }     
            for (int block = 0; block < out_block_cnt; block++) {
                hlk_acquire_dst(core_ptr);
                // Wait for tiles on the input if streaming or it is first kernel_broadcast
                if constexpr (!kernel_broadcast[2]) {
                    hlk_wait_tiles(core_ptr, HlkOperand::in2, out_block_tile_cnt);
                } 
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                for (int t = 0; t < out_block_tile_cnt; ++t) {
                    int rindex = t;
                    if constexpr (kernel_broadcast[2]) {
                        rindex = (batch*out_block_cnt*out_block_tile_cnt + block*out_block_tile_cnt + t)%kernel_broadcast[2];
                    }
                    hlk_binary_op<BinaryOp::Add, Dim::R>(
                     core_ptr, 
                     HlkOperand::intermed0, 
                     HlkOperand::in2, 
                     t, 
                     rindex, 
                     t,
                     0);
                }

                // Run sfpu op if valid
                if (args->sfpu_op != (int)SfpuOp::Invalid) {
                    // Take HlkOperand::intermed0 as args, as intermed tile dims match output tile dims
                    RUN_SFPU_OP(core_ptr, HlkOperand::intermed0, args, out_block_tile_cnt);
                }

                // Pop tiles on the input if streaming or it is last kernel_broadcast
                if constexpr (!kernel_broadcast[2]) {
                    hlk_pop_tiles(core_ptr, HlkOperand::in2, out_block_tile_cnt);
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

            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in2, HlkOperand::in0); //reconfig df for src B register

            if (args->adv_features_en != 0) {
                hlk_mm_block_init_short(core_ptr,
                        HlkOperand::in0,
                        HlkOperand::in1,
                        args->transpose,
                        args->num_tiles_per_n_sub_block,
                        args->num_tiles_per_m_sub_block, 
                        args->block_tile_dim);
            } else {
                hlk_mm_block_init(core_ptr,
                        HlkOperand::in0,
                        HlkOperand::in1,
                        args->transpose, 
                        args->num_tiles_per_n_sub_block, 
                        args->num_tiles_per_m_sub_block, 
                        args->block_tile_dim);
            }
        }

        if constexpr (kernel_broadcast[0] && kernel_broadcast_per_t[0]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
        }

        if constexpr (kernel_broadcast[1] && kernel_broadcast_per_t[1]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
        }

        if constexpr (kernel_broadcast[2] && kernel_broadcast_per_t[2]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
        }
    }

    if constexpr (kernel_broadcast[0] && !kernel_broadcast_per_t[0]) {
        hlk_pop_tiles(core_ptr, HlkOperand::in0, kernel_broadcast[0]);
    }

    if constexpr (kernel_broadcast[1] && !kernel_broadcast_per_t[1]) {
        hlk_pop_tiles(core_ptr, HlkOperand::in1, kernel_broadcast[1]);
    }

    if constexpr (kernel_broadcast[2] && !kernel_broadcast_per_t[2]) {
        hlk_pop_tiles(core_ptr, HlkOperand::in2, kernel_broadcast[2]);
    }
}


