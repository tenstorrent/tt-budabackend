// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "src/ckernels/common/hlk_defs_common.h"

#if defined(UCK_CHLKC_MATH) || defined(UCK_CHLKC_PACK)
#include "llk_math_eltwise_binary_sfpu.h"
#endif

// Configures FPU to do int8 math
TT_HLK_ALWAYS_INLINE void hlk_hw_config_int8_fpu_math(tt_core* core_ptr) {
     UNPACK(llk_enable_int8_fpu_math());
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_init_short(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_A_init<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, IS_INT_FPU_EN>(transpose_of_faces, within_face_16x16_transpose, stream));

    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE, IS_TILIZE_INPUT_EN, IS_FP32_DEST_ACC_EN, IS_INT_FPU_EN>(
        transpose_of_faces, within_face_16x16_transpose, stream));
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_init(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    hlk_copy_tile_to_dst_init_short(core_ptr, stream, transpose_of_faces, within_face_16x16_transpose);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst(
    tt_core* core_ptr, int stream, int index, int dst_index, int transpose_of_faces) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_A<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, IS_INT_FPU_EN>(stream, index, transpose_of_faces));
    MATH(llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, DST_SYNC, IS_FP32_DEST_ACC_EN, IS_INT_FPU_EN>(dst_index, stream));
}

// Copies tile directy to destination accumulator bypassing the math unit
TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly_init_short(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_A_init<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, /*unpack_to_dest*/true>(transpose_of_faces, within_face_16x16_transpose, stream));

    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE, IS_TILIZE_INPUT_EN, IS_FP32_DEST_ACC_EN, IS_INT_FPU_EN>(
        transpose_of_faces, within_face_16x16_transpose, stream));
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly_init(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    hlk_copy_tile_to_dst_init_short(core_ptr, stream, transpose_of_faces, within_face_16x16_transpose);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_directly(
    tt_core* core_ptr, int stream, int index, int dst_index, int transpose_of_faces) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_A<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, /*unpack_to_dest*/true>(stream, index, transpose_of_faces));
    MATH(llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, DST_SYNC, IS_FP32_DEST_ACC_EN, /*unpack_to_dest*/true>(dst_index, stream));
}

#pragma region Sfpu

#pragma region Sfpu Inits

TT_HLK_ALWAYS_INLINE void hlk_sfpu_quant_int32_init(tt_core* core_ptr, int zero_point) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_quant_int32_init<APPROX>(zero_point));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_requant_int32_init(tt_core* core_ptr, int zero_point) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_requant_int32_init<APPROX>(zero_point));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_dequant_int32_init(tt_core* core_ptr, int zero_point) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_dequant_int32_init<APPROX>(zero_point));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_add_int32_init(tt_core* core_ptr) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_add_int32_init<APPROX>());
}

TT_HLK_ALWAYS_INLINE void hlk_topk_init(tt_core* core_ptr, int stream) {
    SFPU_THREAD(llk_math_eltwise_unary_sfpu_topk_init<APPROX>(stream));
}

#pragma endregion

#pragma region Sfpu Ops

TT_HLK_ALWAYS_INLINE void hlk_sfpu_quant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_quant_int32<APPROX, DST_SYNC>(stream, dst_index_a, dst_index_b, dim));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_requant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_requant_int32<APPROX, DST_SYNC>(stream, dst_index_a, dst_index_b, dim));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_dequant_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_dequant_int32<APPROX, DST_SYNC>(stream, dst_index_a, dst_index_b, dim));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_add_int32(tt_core* core_ptr, int stream, int dst_index_a, int dst_index_b, int dim) {
    SFPU_THREAD(llk_math_eltwise_binary_sfpu_add_int32<APPROX, DST_SYNC>(stream, dst_index_a, dst_index_b, dim));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_local_sort(tt_core* core_ptr, int stream, int dst_index, int dir, int end_phase, int start_phase, int end_step, int start_step) {
    SFPU_THREAD(llk_math_sfpu_topk_local_sort<APPROX, DST_SYNC>(stream, dst_index, dir, end_phase, start_phase, end_step, start_step));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_merge(tt_core* core_ptr, int stream, int dst_index, int m, int k) {
    SFPU_THREAD(llk_math_sfpu_topk_merge<APPROX, DST_SYNC>(stream, dst_index, m, k));
}

TT_HLK_ALWAYS_INLINE void hlk_sfpu_topk_rebuild(tt_core* core_ptr, int stream, int dst_index, int dir, int m, int k, int logk, bool skip_second_tile) {
    SFPU_THREAD(llk_math_sfpu_topk_rebuild<APPROX, DST_SYNC>(stream, dst_index, dir, m, k, logk, skip_second_tile));
}


#pragma endregion

#pragma endregion
