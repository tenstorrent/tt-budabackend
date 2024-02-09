// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "src/ckernels/common/hlk_defs_common.h"

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_transpose_yz.h"
#endif

TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile_init_short(tt_core* core_ptr) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_transpose_yz_init());
    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE, false, IS_FP32_DEST_ACC_EN, false>());
}

TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile_init(tt_core* core_ptr) {
    hlk_transpose_yz_tile_init_short(core_ptr);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_transpose_yz_tile(
    tt_core* core_ptr, int stream, int index, int dst_index, int dim, int phase) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_transpose_yz(stream, index, phase));
    MATH(llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, DST_SYNC, IS_FP32_DEST_ACC_EN>(dst_index));
}

TT_HLK_ALWAYS_INLINE void hlk_copy_tile_to_dst_init_short(
    tt_core* core_ptr, int stream, int transpose_of_faces, int within_face_16x16_transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_A_init<BroadcastType::NONE, false>(transpose_of_faces, within_face_16x16_transpose, stream));

    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE, false, IS_FP32_DEST_ACC_EN, false>(
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
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_A(stream, index, transpose_of_faces));
    MATH(llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, DST_SYNC, IS_FP32_DEST_ACC_EN>(dst_index));
}

