// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/depthwise_stream.h"
#include "hlks/inc/wait_pop_tiles_utils.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_matmul(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
    hlk_mm_tile_init(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args)
{
    const bool relu_en = (args->relu_config>0);
    const bool l1_acc_en = (args->l1_acc_en>0);
    const bool shared_buffer = args->shared_buffer>0;
    const bool spill = (args->block_cnt>1);
    const bool gradient_op = args->gradient_op>0;
    const int inner_r = args->num_tiles_per_m_sub_block; // inner row block size in tiles
    const int inner_c = args->num_tiles_per_n_sub_block; // inner column block size in tiles
    const int inner_d = args->block_tile_dim; // u_kt == 1
    const int outer_r = args->num_m_sub_blocks;          // outer row block size (in inner row blocks)
    const int outer_c = args->num_n_sub_blocks;          // outer column block size (in inner column blocks)
    const int block_cnt = args->block_cnt;               //number of blocks to accumulate - equal to number of compressed input1 matrices
    const bool min_in0_buffer = args->min_input_buffer[0]>0;
    //const bool min_in1_buffer = args->min_input_buffer[1]>0; not supported
    const int in0_tile_cnt = min_in0_buffer ? inner_r*inner_c : inner_r*inner_c*outer_r;
    const int in1_tile_cnt = inner_c * outer_c; // TODO: add block_tile_dim==u_kt
    const int out_block_cnt = outer_r*outer_c; // number of u-blocks in output (mblock_m*nblock_n)
    const int out_block_tile_cnt = inner_r * inner_c; // number of tiles in ublock (ublock_rt*ublock_ct)
    const int out_tile_cnt = out_block_cnt * out_block_tile_cnt; // number of tiles in output (mblock_m*nblock_n*ublock_rt*ublock_ct)
    const bool adv_features_en = args->adv_features_en>0;
    const bool bias = args->bias>0;

    wait_for_tiles_kernel_broadcast<DEPTHWISE_MAX_NUM_INPUTS>(core_ptr);

    for (int batch = 0; batch < args->batch_cnt; batch++) {
        wait_for_tiles_kernel_broadcast_t<DEPTHWISE_MAX_NUM_INPUTS>(core_ptr);
        bool enable_reload = gradient_op;
        bool l1_acc = false;

        if ((spill || bias) && !gradient_op)
        {
            hlk_reconfig_packer_df(core_ptr, HlkOperand::out0, HlkOperand::intermed0);
        }

        if (relu_en)
        {
            hlk_relu_config(core_ptr, 0);
        }

        for(int block_id = 0; block_id < block_cnt; ++block_id)
        {
            // Buffer horizontal strip
            if constexpr (!kernel_broadcast[1]) {
                hlk_wait_tiles(core_ptr, HlkOperand::in1, in1_tile_cnt);
            }

            bool last_out = (block_id == (block_cnt-1));
            if (last_out and !gradient_op and !bias)
            {
                hlk_reconfig_packer_df(core_ptr, HlkOperand::intermed0, HlkOperand::out0);
                if (relu_en)
                {
                    hlk_relu_config(core_ptr, args->relu_config);
                }

                if (l1_acc_en and l1_acc and (!shared_buffer || relu_en))
                {
                    hlk_reconfig_packer_l1_acc(core_ptr, 0);
                    l1_acc = false;
                }
            }

            if (enable_reload) {
                hlk_wait_tiles(core_ptr, HlkOperand::intermed0, out_tile_cnt);
            }

            if (last_out and !gradient_op and !bias)
            {
                // Wait for output buffer to be freed up
                hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, out_tile_cnt);
            }
            else
            {
                // Wait for intermed buffer to be freed up
                if (!l1_acc) {
                    hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, out_tile_cnt);
                }
            }

            for (int out_c = 0; out_c < outer_c; out_c++)
            {
                // Buffer vertical strip if min buffer is not used
                if ((not kernel_broadcast[0]) and (not min_in0_buffer)) {
                    hlk_wait_tiles(core_ptr, HlkOperand::in0, in0_tile_cnt);
                }

                for (int out_r = 0; out_r < outer_r; out_r++)
                {
                    // Buffer ublock if min buffer is used
                    if ((not kernel_broadcast[0]) and min_in0_buffer) {
                        hlk_wait_tiles(core_ptr, HlkOperand::in0, in0_tile_cnt);
                    }

                    hlk_acquire_dst(core_ptr);

                    if (enable_reload)
                    {
                        if (!l1_acc)
                        {
                            hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::intermed0, !adv_features_en, false);
                            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::in1, HlkOperand::intermed0, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register, in1 = srcA
                            int in_index = (out_r * outer_c + out_c) * out_block_tile_cnt;
                            for (int i = 0; i < out_block_tile_cnt; i++)
                            {
                                hlk_copy_tile_to_dst(core_ptr, HlkOperand::intermed0, in_index, i, !adv_features_en);
                                in_index++;
                            }
                            hlk_mm_tile_init_short(core_ptr,
                                                    HlkOperand::in0,
                                                    HlkOperand::in1,
                                                    args->transpose);
                            hlk_reconfig_unpacker_df(core_ptr, HlkOperand::intermed0, HlkOperand::in1, HlkOperand::in0, HlkOperand::in0); //reconfig df for src A register
                        }
                    }

                    // Compute MM Sub Block
                    // TODO: add inner_d loop
                    int dst_index = 0;
                    for (int in_r = 0; in_r < inner_r; in_r++)
                    {
                        for (int in_c = 0; in_c < inner_c; in_c++)
                        {
                            int in0_index = (min_in0_buffer ? 0 : out_r * inner_r * inner_c) + in_r * inner_c + in_c;
                            if constexpr (kernel_broadcast[0]) {
                                in0_index = (block_id*inner_r*inner_c*outer_r + out_r*inner_r*inner_c + in_r*inner_c + in_c) % kernel_broadcast[0];
                            }

                            int in1_index = out_c * inner_c + in_c;  // TODO: assumes in1 ublock_rt == u_kt == 1
                            if constexpr (kernel_broadcast[1]) {
                                in1_index = (block_id*inner_c*inner_d*outer_c + out_c*inner_c + in_c) % kernel_broadcast[1];
                            }

                            hlk_mm_tile<kernel_broadcast[0], kernel_broadcast[1]>(
                                core_ptr,
                                HlkOperand::in0,
                                HlkOperand::in1,
                                in0_index,
                                in1_index,
                                dst_index,
                                args->transpose,
                                inner_d
                            );

                            dst_index++;
                        }
                    }

                    int out_index = (out_r*outer_c + out_c)*out_block_tile_cnt;

                    if (last_out and !gradient_op and !bias) {
                        // Pack final results to output buffer
                        for (int i = 0; i < out_block_tile_cnt; i++) {
                            hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::out0, out_index);
                            out_index++;
                        }
                    }
                    else
                    {
                        // Pack partial result to interm buffer
                        for (int i = 0; i < out_block_tile_cnt; i++) {
                            hlk_pack_tile_to_stream(core_ptr, i, HlkOperand::intermed0, out_index);
                            out_index++;
                        }
                    }

                    hlk_release_dst(core_ptr);

                    if ((not kernel_broadcast[0]) and min_in0_buffer) {
                        hlk_pop_tiles(core_ptr, HlkOperand::in0, in0_tile_cnt);
                    }
                }

                // Pop in0 vertical strip
                if ((not kernel_broadcast[0]) and (not min_in0_buffer)) {
                    hlk_pop_tiles(core_ptr, HlkOperand::in0, in0_tile_cnt);
                }    
            }

            // TODO: Is this right place or should it be after if (spill) block?
            if (enable_reload) {
                // Pop all blocks from interm buffer
                // Use for loop to match number of pops with number of output blocks
                // Required when interm and output buffers are shared
                for (int i = 0; i < out_block_cnt; i++) {
                    hlk_pop_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                }
            }

            if (last_out and !gradient_op and !bias) {
                hlk_push_tiles(core_ptr, HlkOperand::out0, out_block_tile_cnt*out_block_cnt);
            } else {
                // Push all blocks to interm buffer
                // Use for loop to match number of pushes with number of output blocks
                // Required when interm and output buffers are shared
                for (int i = 0; i < out_block_cnt; i++) {
                    hlk_push_tiles(core_ptr, HlkOperand::intermed0, out_block_tile_cnt);
                }
            }

            if (spill) {
                enable_reload = true;
                if (l1_acc_en) {
                    if (block_id == 0) {
                        hlk_reconfig_packer_l1_acc(core_ptr, 1);
                        l1_acc = true;
                    }
                }
            }

            // Pop in1 horizontal strip
            if constexpr (not kernel_broadcast[1]) {
                hlk_pop_tiles(core_ptr, HlkOperand::in1, in1_tile_cnt);
            }    
        }

        if (l1_acc) {
            hlk_reconfig_packer_l1_acc(core_ptr, 0);
            l1_acc = false;
        }

        if (bias) {
            // Reconfigure hw for running add with tile row brodcast
            if (adv_features_en) {
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
                // Wakernel_broadcastit for tiles on the input if streaming or it is first kernel_broadcast
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

                // Pop tiles on the input if streaming or it is last kernel_broadcast
                if (not kernel_broadcast[2]) {
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
            if (adv_features_en) {
                hlk_mm_tile_init_short(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
            } else {
                hlk_mm_tile_init(core_ptr, HlkOperand::in0, HlkOperand::in1, args->transpose);
            }
        }
        pop_tiles_kernel_broadcast_t<DEPTHWISE_MAX_NUM_INPUTS>(core_ptr);
    }

    pop_tiles_kernel_broadcast<DEPTHWISE_MAX_NUM_INPUTS>(core_ptr);

}