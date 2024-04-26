// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlk_api.h"

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void binary_sfpu_inner_loop(
    tt_core* core_ptr,
    HlkOperand in0,
    HlkOperand in1,
    HlkOperand out,
    int block_tile_dim,
    int block_index,
    bool transpose,
    int half_dest_size,
    int num_iterations,
    int kernel_broadcast_in0,
    int kernel_broadcast_in1) {
    hlk_wait_for_free_tiles(core_ptr, out, block_tile_dim);

    int half_block_tile_dim = block_tile_dim > half_dest_size ? half_dest_size : block_tile_dim;

    for (int i = 0; i < num_iterations; ++i) {
        hlk_acquire_dst(core_ptr);

        // In loop 1 just copy input0 to dest
        hlk_reconfig_unpacker_df_srca(core_ptr, in1, in0);
        hlk_copy_tile_to_dst_init_short(core_ptr, in0, transpose, transpose);

        const int input_base_index = i * half_dest_size;
        for (int t = 0; t < half_block_tile_dim; ++t) {
            const int input_tile_index = input_base_index + t;
            hlk_copy_tile_to_dst(
                core_ptr,
                in0,
                !kernel_broadcast_in0 ? input_tile_index : (block_index * block_tile_dim + input_tile_index) % kernel_broadcast_in0,
                2 * t,
                transpose);
        }

        // In loop 2 copy input1 to dest and run binary sfpu op.
        hlk_reconfig_unpacker_df_srca(core_ptr, in0, in1);
        hlk_copy_tile_to_dst_init_short(core_ptr, in1, false, false);
        for (int t = 0; t < half_block_tile_dim; ++t) {
            const int input_tile_index = input_base_index + t;
            const int tile_index0 = 2 * t;
            const int tile_index1 = tile_index0 + 1;

            hlk_copy_tile_to_dst(
                core_ptr,
                in1,
                !kernel_broadcast_in1 ? input_tile_index : (block_index * block_tile_dim + input_tile_index) % kernel_broadcast_in1,
                tile_index1,
                false);
            
            if constexpr (binary_op == BinaryOp::Quantization) {
                hlk_sfpu_quant_int32(core_ptr, in0, tile_index0, tile_index1, (int)Dim::RC);
            } else if constexpr (binary_op == BinaryOp::Dequantization) {
                hlk_sfpu_dequant_int32(core_ptr, in0, tile_index0, tile_index1, (int)Dim::RC);
            } else if constexpr (binary_op == BinaryOp::Requantization) {
                hlk_sfpu_requant_int32(core_ptr, in0, tile_index0, tile_index1, (int)Dim::RC);
            } else if constexpr (binary_op == BinaryOp::Add) {
                hlk_sfpu_add_int32(core_ptr, in0, tile_index0, tile_index1, (int)Dim::RC);
            } else if constexpr (binary_op == BinaryOp::Maximum) {
                // SfpuOp::Max assumes that in1 is at tile_index0 + 1
                hlk_unary_sfpu_op<SfpuOp::Max>(core_ptr, in0, tile_index0, (int)Dim::RC);
            }

            hlk_pack_tile_to_stream(core_ptr, tile_index0, out);
        }

        half_block_tile_dim = block_tile_dim - half_block_tile_dim;
        hlk_release_dst(core_ptr);
    }

    hlk_push_tiles(core_ptr, out, block_tile_dim);
}

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void binary_sfpu_inner_loop_from_dest(
    tt_core* core_ptr,
    HlkOperand scalars,
    HlkOperand out,
    int block_tile_dim,
    int block_index,
    int kernel_broadcast_in) {
    hlk_wait_for_free_tiles(core_ptr, out, block_tile_dim);

    for (int t = 0; t < block_tile_dim; ++t) {
        // Inputs are already loaded in tiles 0 and 1 as a results of preceding op
        // Here we only load scalars into tiles 2 and 3
        // For this reason micro block size has to be  <= 2
        hlk_copy_tile_to_dst(
            core_ptr,
            scalars,
            t,
            2 + t,
            false);

        if constexpr (binary_op == BinaryOp::Quantization) {
            hlk_sfpu_quant_int32(core_ptr, scalars, t, 2 + t, (int)Dim::RC);
        } else if constexpr (binary_op == BinaryOp::Dequantization) {
            hlk_sfpu_dequant_int32(core_ptr, scalars, t, 2 + t, (int)Dim::RC);
        } else if constexpr (binary_op == BinaryOp::Requantization) {
            hlk_sfpu_requant_int32(core_ptr, scalars, t, 2 + t, (int)Dim::RC);
        }
        hlk_pack_tile_to_stream(core_ptr, t, out);
    }

    // Here we only release dest as preceding op wihch ouputs do dest has aquired dest.
    hlk_release_dst(core_ptr);

    hlk_push_tiles(core_ptr, out, block_tile_dim);
}