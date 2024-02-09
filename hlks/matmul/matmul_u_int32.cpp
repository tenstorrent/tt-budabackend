// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/matmul_u_int32.h"

#include "hlks/inc/binary_sfpu_inner_loop.h"
#include "hlks/inc/wait_pop_tiles_utils.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_hw_config_matmul(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
    hlk_mm_block_init(
        core_ptr,
        HlkOperand::in0,
        HlkOperand::in1,
        args->transpose,
        args->num_tiles_per_n_sub_block,
        args->num_tiles_per_m_sub_block,
        args->block_tile_dim);

    if (args->sfpu_op != (int)SfpuOp::Invalid) {
        // TODO: Add sfpu op init
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
    const int inner_r = args->num_tiles_per_m_sub_block; // inner row block size in tiles
    const int inner_c = args->num_tiles_per_n_sub_block; // inner column block size in tiles
    const int inner_d = args->block_tile_dim; // inner block size in tiles
    const int outer_r = args->num_m_sub_blocks; // outer row block size (in inner row blocks)
    const int outer_c = args->num_n_sub_blocks; // outer column block size (in inner column blocks)
    const int outer_id = args->block_cnt; // outer inner dim (in inner dim blocks)
    const int in0_block_tile_cnt = min_in0_buffer ? inner_r*inner_d : inner_r*inner_d*outer_r;
    const int in1_block_tile_cnt = inner_c*inner_d*outer_c;
    const int out_block_cnt = outer_r*outer_c;
    const int out_block_tile_cnt = inner_r * inner_c;
    const bool adv_features_en = args->adv_features_en>0;
    const bool requant = args->requant>0;
    const bool dequant = args->dequant>0;
    const HlkOperand rq_operand = bias ? HlkOperand::in3 : HlkOperand::in2;
    const HlkOperand dq_operand = bias ? HlkOperand::in3 : HlkOperand::in2;
    const int rq_index = bias ? 3 : 2;
    const int dq_index = bias ? 3 : 2;

    if (gradient_op) {
        hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0);
    }

    wait_for_tiles_kernel_broadcast<MATMUL_U_INT32_MAX_NUM_INPUTS>(core_ptr);

    for (int batch = 0; batch < args->batch_cnt; batch++) {
        wait_for_tiles_kernel_broadcast_t<MATMUL_U_INT32_MAX_NUM_INPUTS>(core_ptr);

        bool enable_reload = gradient_op;
        bool l1_acc = false;

        if ((spill || requant || dequant) && (!gradient_op))  {
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

            if (last_out and !gradient_op and !requant and !dequant and last_z) {
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

            if constexpr (!kernel_broadcast[1]) {
                // Buffer horizontal strip
                hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
            }

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
                    } else if (bias && (oid==0) && (z==0)) {
                        hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::in2, !adv_features_en, false);
                        hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::in2, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register, in1 = srcA
                        if constexpr (!kernel_broadcast[2]) {
                            hlk_wait_tiles(core_ptr, HlkOperand::in2, out_block_tile_cnt);
                        }
                        for (int i = 0; i < out_block_tile_cnt; i++) {
                            int tile_index = i;
                            if constexpr (kernel_broadcast[2]) {
                                tile_index = (batch*out_block_cnt*out_block_tile_cnt + (out_r*outer_c+out_c)*out_block_tile_cnt + i)%kernel_broadcast[2];
                            }
                            hlk_copy_tile_to_dst(core_ptr, 
                                                    HlkOperand::in2, 
                                                    tile_index,
                                                    i, 
                                                    !adv_features_en);
                        }
                        if constexpr (!kernel_broadcast[2]) {
                            hlk_pop_tiles(core_ptr, HlkOperand::in2, out_block_tile_cnt);
                        }     
                        hlk_mm_block_init_short(core_ptr,
                                                HlkOperand::in0,
                                                HlkOperand::in1,
                                                args->transpose, 
                                                args->num_tiles_per_n_sub_block, 
                                                args->num_tiles_per_m_sub_block, 
                                                args->block_tile_dim);
                        hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in2, HlkOperand::in1, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register
                    }

                    // Compute MM Sub Block
                    for (int in_d = 0; in_d < inner_d; in_d++) {
                        int dst_index = 0;
                        int in0_index = (min_in0_buffer ? 0 : out_r * inner_r * inner_d) + in_d;
                        if constexpr (kernel_broadcast[0]) {
                            in0_index = (oid*inner_r*inner_d*outer_r + out_r*inner_r*inner_d + in_d)%kernel_broadcast[0];
                        }
                        int in1_index = out_c*inner_c*inner_d + in_d*inner_c;

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

                    if (last_out and !gradient_op and !requant and !dequant and last_z) {
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

            if constexpr (!kernel_broadcast[1]) {
                // Pop in1 horizontal strip
                hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_block_tile_cnt);
            }   
            
        }

        }

        if (l1_acc) {
            hlk_reconfig_packer_l1_acc(core_ptr, 0);
            l1_acc = false;
        }

        if (requant) {
            hlk_reconfig_unpacker_df_srca(core_ptr, HlkOperand::intermed0); //reconfig df for src A
            hlk_sfpu_requant_int32_init(core_ptr, args->zero_point);
            hlk_reconfig_packer_df(core_ptr, HlkOperand::out0);
            if (relu_en) {
                hlk_relu_config(core_ptr, args->relu_config);
            }

            constexpr int half_dest_size = 2;
            const int num_iter = out_block_tile_cnt > half_dest_size ? 2 : 1;

            for (int block = 0; block < out_block_cnt; block++) {
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                if (not kernel_broadcast[rq_index]) {
                    hlk_wait_tiles(core_ptr, rq_operand, out_block_tile_cnt);
                }

                binary_sfpu_inner_loop<BinaryOp::Requantization>(
                    core_ptr,
                    HlkOperand::intermed0,
                    rq_operand,
                    HlkOperand::out0,
                    out_block_tile_cnt,
                    block,
                    false,
                    half_dest_size,
                    num_iter,
                    0,
                    kernel_broadcast[rq_index]);

                hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                if (not kernel_broadcast[rq_index]) {
                    hlk_pop_tiles(core_ptr, rq_operand, out_block_tile_cnt);
                }    
            }
            hlk_mm_block_init_short(core_ptr,
                                    HlkOperand::in0,
                                    HlkOperand::in1,
                                    args->transpose, 
                                    args->num_tiles_per_n_sub_block, 
                                    args->num_tiles_per_m_sub_block, 
                                    args->block_tile_dim);
            hlk_reconfig_unpacker_df_srca(core_ptr, HlkOperand::in1); //reconfig df for src A
            hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0);
        } else if (dequant) {
            hlk_reconfig_unpacker_df_srca(core_ptr, HlkOperand::intermed0); //reconfig df for src A
            hlk_sfpu_dequant_int32_init(core_ptr, args->zero_point);
            hlk_reconfig_packer_df(core_ptr, HlkOperand::out0);
            if (relu_en) {
                hlk_relu_config(core_ptr, args->relu_config);
            }

            constexpr int half_dest_size = 2;
            const int num_iter = out_block_tile_cnt > half_dest_size ? 2 : 1;

            for (int block = 0; block < out_block_cnt; block++) {
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                if (not kernel_broadcast[dq_index]) {
                    hlk_wait_tiles(core_ptr, dq_operand, out_block_tile_cnt);
                }

                binary_sfpu_inner_loop<BinaryOp::Dequantization>(
                    core_ptr,
                    HlkOperand::intermed0,
                    dq_operand,
                    HlkOperand::out0,
                    out_block_tile_cnt,
                    block,
                    false,
                    half_dest_size,
                    num_iter,
                    0,
                    kernel_broadcast[dq_index]);

                hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                if (not kernel_broadcast[dq_index]) {
                    hlk_pop_tiles(core_ptr, dq_operand, out_block_tile_cnt);
                }
            }
            hlk_mm_block_init_short(core_ptr,
                                    HlkOperand::in0,
                                    HlkOperand::in1,
                                    args->transpose, 
                                    args->num_tiles_per_n_sub_block, 
                                    args->num_tiles_per_m_sub_block, 
                                    args->block_tile_dim);
            hlk_reconfig_unpacker_df_srca(core_ptr, HlkOperand::in1); //reconfig df for src A
            hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0);
        }

        pop_tiles_kernel_broadcast_t<MATMUL_U_INT32_MAX_NUM_INPUTS>(core_ptr);
    }

    pop_tiles_kernel_broadcast<MATMUL_U_INT32_MAX_NUM_INPUTS>(core_ptr);
}


