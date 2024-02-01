// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlk_api.h"

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void quantization_ops_inner_loop(
    tt_core* core_ptr,
    HlkOperand inputs,
    HlkOperand scalars,
    HlkOperand out,
    int block_tile_dim,
    int block_index,
    int kernel_broadcast_in0,
    int kernel_broadcast_in1) {
    hlk_wait_for_free_tiles(core_ptr, out, block_tile_dim);

    for (int t = 0; t < block_tile_dim; ++t) {
        hlk_acquire_dst(core_ptr);
        hlk_reconfig_unpacker_df_srca(core_ptr, inputs);
        hlk_copy_tile_to_dst_init_short(core_ptr, inputs, false, false);
        hlk_copy_tile_to_dst(
            core_ptr,
            inputs,
            !kernel_broadcast_in0 ? t : (block_index * block_tile_dim + t) % kernel_broadcast_in0,
            0,
            false);

        hlk_reconfig_unpacker_df_srca(core_ptr, inputs, scalars);
        hlk_copy_tile_to_dst_init_short(core_ptr, scalars, false, false);
        hlk_copy_tile_to_dst(
            core_ptr,
            scalars,
            !kernel_broadcast_in1 ? t : (block_index * block_tile_dim + t) % kernel_broadcast_in1,
            2,
            false);

        if constexpr (binary_op == BinaryOp::Quantization) {
            hlk_sfpu_quant_int32(core_ptr, inputs, 0, 2, (int)Dim::RC);
        } else if constexpr (binary_op == BinaryOp::Dequantization) {
            hlk_sfpu_dequant_int32(core_ptr, inputs, 0, 2, (int)Dim::RC);
        } else if constexpr (binary_op == BinaryOp::Requantization) {
            hlk_sfpu_requant_int32(core_ptr, inputs, 0, 2, (int)Dim::RC);
        } else if constexpr (binary_op == BinaryOp::AddInt32) {
            hlk_sfpu_add_int32(core_ptr, inputs, 0, 2, (int)Dim::RC);
        }
        hlk_pack_tile_to_stream(core_ptr, 0, out);
        hlk_release_dst(core_ptr);
    }

    hlk_push_tiles(core_ptr, out, block_tile_dim);
}

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void quantization_ops_inner_loop_from_dest(
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