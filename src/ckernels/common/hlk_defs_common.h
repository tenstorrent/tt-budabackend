// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// ==== NEEDED FOR THESE HLK_APIS ====

// hlk_debug_dump
// math_pack_sync_init for math-pre inserted call

#include "hlks/inc/hlk_api.h"

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_api.h" 
#endif

#if defined(UCK_CHLKC_PACK)
#include "llk_pack_api.h" 
#endif

#if defined(UCK_CHLKC_MATH) || defined(UCK_CHLKC_PACK)
#include "llk_math_api.h" 
#endif

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_common.h"
#endif

#if defined(UCK_CHLKC_MATH)
#include "llk_math_common.h"  // hlk_acquire_dest, hlk_release_dst
#endif

#if defined(UCK_CHLKC_PACK)
#include "llk_pack_common.h"  // hlk_acquire_dest, hlk_release_dst
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

// hlk_ttlog
// This file also contains other common stuff, so might include it anyway

#include "ckernel.h"  // needed for TT_LOG

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS
// not needed for a hlk api, but for a temp workaround regarding pre-inserted calls
// llk_setup_operands
// llk_pack_init
// llk_setup_outputs
// llk_pack_init

#if defined(UCK_CHLKC_UNPACK)
#include "llk_io_unpack.h"  // hlk_wait_tiles, hlk_pop_tiles
#endif

#if defined(UCK_CHLKC_PACK)
#include "llk_io_pack.h"  // hlk_wait_for_free_tiles, hlk_push_tiles
#include "llk_pack.h"  // hlk_hw_config_single_operand, hlk_pack_tile_to_stream, hlk_hw_config_two_operands, hlk_hw_config_reduce, hlk_hw_config_tilize
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

// hlk sfpu inits, hlk sfpu ops

#if defined(UCK_CHLKC_MATH) || defined(UCK_CHLKC_PACK)
#include "llk_math_eltwise_unary_sfpu.h"
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_A.h"  // hlk hw config single operand, hlk_copy_tile_to_dst_init, hlk_copy_tile_to_dst
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

#if defined(UCK_CHLKC_MATH)
#include "llk_math_eltwise_unary_datacopy.h"  // hlk_copy_tile_to_dst_init, hlk_copy_tile_to_dst
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_AB.h"  // hlk_add_tile_init, hlk_add_tile, hlk_hw_config_two_operands
#endif

#if defined(UCK_CHLKC_MATH)
#include "llk_math_eltwise_binary.h"  // hlk_add_tile_init, hlk_add_tile
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK_APIS ====

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_AB_matmul.h"  // hlk_hw_config_matmul
#endif

#if defined(UCK_CHLKC_MATH)
#include "llk_math_matmul.h"  // matmul
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK APIS ====

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_reduce.h"  // hlk_hw_config_reduce, hlk_reduce_tile_init, hlk_reduce_tile
#endif

#if defined(UCK_CHLKC_MATH)
#include "llk_math_reduce.h"  // hlk_reduce_tile_init, hlk_reduce_tile
#endif

// ==== END ====

// ==== NEEDED FOR THESE HLK APIS ====

#if defined(UCK_CHLKC_UNPACK)
#include "llk_unpack_tilize.h"  // hlk_hw_config_tilize, hlk_tilize_init, hlk_tilize
#endif

// ==== END ====

// ==== NEEDED FOR PERF MEASUREMENTS ====

#include "ckernel_perf_api.h"

// ==== END ====

// ==== NEEDED FOR COMMON ENUMS ====

#include "llk_defs.h"
#include "llk_sfpu_types.h"

// ==== END ====

// COMPILE TIME CONSTANTS

#if !defined(UCK_CHLKC_MATH) && !defined(UCK_CHLKC_UNPACK) && !defined(UCK_CHLKC_PACK)
constexpr bool IS_FP32_DEST_ACC_EN = false;
constexpr bool IS_PERF_DUMP_EN = false;
constexpr bool IS_UNTILIZE_OUTPUT_EN = false;
constexpr bool IS_PACK_MICROBLOCKS_EN = false;
constexpr bool IS_PACK_L1_ACC_EN = false;
constexpr bool IS_UNPACK_MATH_DECOUPLED_EN = false;
constexpr bool IS_MATH_PACK_DECOUPLED_EN = false;
constexpr ReluType RELU_TYPE = ReluType::NO_RELU;
constexpr uint32_t RELU_THRESHOLD = 0;
constexpr SfpuExecutionThread SFPU_EXECUTION_THREAD = SfpuExecutionThread::Math;
constexpr bool IS_KERNEL_UNPACK_DELAY_EN = false;
constexpr bool IS_KERNEL_MATH_DELAY_EN = false;
constexpr bool IS_KERNEL_PACK_DELAY_EN = false;
constexpr DstTileFaceLayout MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT = DstTileFaceLayout::RowMajor;
constexpr DstSync DST_SYNC = DstSync::SyncHalf;
constexpr bool IS_INT_FPU_EN = false;
constexpr StochRndType STOCH_RND_TYPE = StochRndType::None;
constexpr bool IS_TILE_DIM_UNPACK_RECONFIG_EN = false;
constexpr bool IS_TILE_DIM_PACK_RECONFIG_EN = false;

static_assert(false, "Should use compile time generated constants!");
#endif
// otherwise, those constants are included in a generated hlk_compile_time_constants file

// COMMON DEFS

#if defined(UCK_CHLKC_UNPACK)
#define UNPACK(...) __VA_ARGS__
#define UNPACK_IF_NOT_MATH_DECOUPLED(...)               \
    if constexpr (IS_UNPACK_MATH_DECOUPLED_EN == false) \
        __VA_ARGS__;
#else
#define UNPACK(...)
#define UNPACK_IF_NOT_MATH_DECOUPLED(...)
#endif

#if defined(UCK_CHLKC_MATH)
#define MATH(...) __VA_ARGS__
#else
#define MATH(...)
#endif

#if defined(UCK_CHLKC_PACK)
#define PACK(...) __VA_ARGS__
#define PACK_IF_NOT_MATH_DECOUPLED(...)                 \
    if constexpr (IS_MATH_PACK_DECOUPLED_EN == false) { \
        __VA_ARGS__;                                    \
    }
#else
#define PACK(...)
#define PACK_IF_NOT_MATH_DECOUPLED(...)
#endif

#define SFPU_THREAD(...)                                                \
    if constexpr (SFPU_EXECUTION_THREAD == SfpuExecutionThread::Math) { \
        MATH(__VA_ARGS__);                                              \
    } else {                                                            \
        PACK(__VA_ARGS__);                                              \
    }

#ifndef TT_HLK_ALWAYS_INLINE
#define TT_HLK_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

template <BinaryOp op_type>
constexpr ckernel::EltwiseBinaryType get_eltwise_binary_op_from_op_type() {
    if constexpr (op_type == BinaryOp::Add) {
        return ckernel::EltwiseBinaryType::ELWADD;
    } else if constexpr (op_type == BinaryOp::Subtract) {
        return ckernel::EltwiseBinaryType::ELWSUB;
    } else if constexpr (op_type == BinaryOp::Multiply) {
        return ckernel::EltwiseBinaryType::ELWMUL;
    } else {
        // Todo: assert here somehow
        return ckernel::EltwiseBinaryType::ELWADD; // ?
    }
}

template <BinaryOpDstReuse dest_reuse>
constexpr ckernel::EltwiseBinaryReuseDestType get_eltwise_binary_reuse_dest_type_from_binary_dst_reuse() {
    if constexpr (dest_reuse == BinaryOpDstReuse::DstToSrcA) {
        return ckernel::EltwiseBinaryReuseDestType::DEST_TO_SRCA;
    } else if constexpr (dest_reuse == BinaryOpDstReuse::DstToSrcB) {
        return ckernel::EltwiseBinaryReuseDestType::DEST_TO_SRCB;
    } else {
        // Todo:: assert here somehow
        return ckernel::EltwiseBinaryReuseDestType::NONE;
    }
}

template <Dim dim>
constexpr ckernel::ReduceDim get_reduce_dim_from_dim() {
    if constexpr (dim == Dim::R) {
        return ckernel::ReduceDim::REDUCE_COL; // Notice inverted logic, not a mistake!
    } else if constexpr (dim == Dim::C) {
        return ckernel::ReduceDim::REDUCE_ROW; // Notice inverted logic, not a mistake!
    } else if constexpr (dim == Dim::RC) {
        return ckernel::ReduceDim::REDUCE_SCALAR;
    } else {
        // Todo: assert here somehow
        return ckernel::ReduceDim::REDUCE_COL;
    }
}

template <Dim dim>
constexpr ckernel::BroadcastType get_broadcast_type_from_dim() {
    if constexpr (dim == Dim::R) {
        return ckernel::BroadcastType::ROW;
    } else if constexpr (dim == Dim::C) {
        return ckernel::BroadcastType::COL;
    } else if constexpr (dim == Dim::RC) {
        return ckernel::BroadcastType::SCALAR;
    } else if constexpr (dim == Dim::None) {
        return ckernel::BroadcastType::NONE;
    } else {
        // Todo: assert here somehow
        return ckernel::BroadcastType::NONE;
    }
}

template <SfpuOp sfpu_op>
constexpr SfpuType get_sfpu_type_from_sfpu_op() {
    if constexpr(sfpu_op == SfpuOp::Exp) {
        return SfpuType::exponential;
    } else if constexpr (sfpu_op == SfpuOp::Log) {
        return SfpuType::log;
    } else if constexpr (sfpu_op == SfpuOp::Sqrt) {
        return SfpuType::sqrt;
    } else if constexpr (sfpu_op == SfpuOp::Gelu) {
        return SfpuType::gelu;
    } else if constexpr (sfpu_op == SfpuOp::GeluDerivative) {
        return SfpuType::gelu_derivative;
    } else if constexpr (sfpu_op == SfpuOp::Reciprocal) {
        return SfpuType::reciprocal;
    } else if constexpr (sfpu_op == SfpuOp::Sigmoid) {
        return SfpuType::sigmoid;
    } else if constexpr (sfpu_op == SfpuOp::Dropout) {
        return SfpuType::dropout;
    } else if constexpr (sfpu_op == SfpuOp::Tanh) {
        return SfpuType::tanh;
    } else if constexpr (sfpu_op == SfpuOp::Square) {
        return SfpuType::square;
    } else if constexpr (sfpu_op == SfpuOp::Power) {
        return SfpuType::power;
    } else if constexpr (sfpu_op == SfpuOp::Sine) {
        return SfpuType::sine;
    } else if constexpr (sfpu_op == SfpuOp::Cosine) {
        return SfpuType::cosine;
    } else if constexpr (sfpu_op == SfpuOp::ReluMax) {
        return SfpuType::relu_max;
    } else if constexpr (sfpu_op == SfpuOp::ReluMin) {
        return SfpuType::relu_min;
    } else if constexpr (sfpu_op == SfpuOp::LRelu) {
        return SfpuType::lrelu;
    } else if constexpr (sfpu_op == SfpuOp::Abs) {
        return SfpuType::abs;
    } else if constexpr (sfpu_op == SfpuOp::Max) {
        return SfpuType::max;
    } else if constexpr (sfpu_op == SfpuOp::CastFp32ToFp16a) {
        return SfpuType::cast_fp32_to_fp16a;
    } else {
        // Todo: assert here somehow?
        return SfpuType::unused;
    }
}

// Dummy definition of tt_core, as we have no usage for it in low level kernels
struct tt_core {};

#pragma region Tile Management

TT_HLK_ALWAYS_INLINE void hlk_wait_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    UNPACK(llk_wait_tiles(stream, num_tiles));
}

template <int pop_blocks = 0>
TT_HLK_ALWAYS_INLINE void hlk_pop_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    UNPACK(llk_pop_tiles<pop_blocks>(stream, num_tiles));
}

TT_HLK_ALWAYS_INLINE void hlk_get_tile(tt_core* core_ptr, int stream, int index, volatile void* p_tile) {
    UNPACK(llk_unpack_get_tile(stream, index, (uint32_t*)p_tile));

    MATH(llk_math_get_tile(stream, index, (uint32_t*)p_tile));

    PACK(llk_pack_get_tile(stream, index, (uint32_t*)p_tile));
}

TT_HLK_ALWAYS_INLINE void hlk_release_tile(tt_core* core_ptr, int stream) {
    UNPACK(llk_unpack_release_tile(stream));

    MATH(llk_math_release_tile(stream));

    PACK(llk_pack_release_tile(stream));
}

TT_HLK_ALWAYS_INLINE void hlk_flush_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    UNPACK(llk_clear_tiles(stream, num_tiles));

    PACK(llk_free_tiles(stream, num_tiles));
}

TT_HLK_ALWAYS_INLINE void hlk_wait_for_free_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    PACK(llk_wait_for_free_tiles<false, IS_UNTILIZE_OUTPUT_EN, IS_PACK_MICROBLOCKS_EN>(stream, num_tiles));
}

TT_HLK_ALWAYS_INLINE void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream) {
    PACK(
        if constexpr (IS_MATH_PACK_DECOUPLED_EN == false) {
            llk_pack<false, DST_SYNC, IS_UNTILIZE_OUTPUT_EN, IS_FP32_DEST_ACC_EN>(dst_index, stream);
        } else { llk_pack_decouple<false, DST_SYNC, IS_UNTILIZE_OUTPUT_EN, IS_FP32_DEST_ACC_EN>(dst_index, stream); });
}

// 4 arguments, has true as first template arg
TT_HLK_ALWAYS_INLINE void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream, int tile_index) {
    // We are hardcoding false here (as the third template argument) instead of using IS_UNTILIZE_OUTPUT_EN because that
    // is an invalid combination and it will cause a static assert hit during compile time.
    PACK(
        if constexpr (IS_MATH_PACK_DECOUPLED_EN == false) {
            llk_pack<true, DST_SYNC, false, IS_FP32_DEST_ACC_EN>(dst_index, stream, tile_index);
        } else { llk_pack_decouple<true, DST_SYNC, false, IS_FP32_DEST_ACC_EN>(dst_index, stream, tile_index); });
}

TT_HLK_ALWAYS_INLINE void hlk_push_tiles(tt_core* core_ptr, int stream, int num_tiles) {
    PACK(llk_push_tiles<IS_UNTILIZE_OUTPUT_EN, IS_PACK_MICROBLOCKS_EN>(stream, num_tiles));
}

#pragma endregion

#pragma region Sfpu

#pragma region Sfpu Inits

template<SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_unary_sfpu_init(tt_core* ptr, int stream, unsigned int sfpu_specific_parameter) {
    SFPU_THREAD(llk_math_eltwise_unary_sfpu_init<get_sfpu_type_from_sfpu_op<sfpu_op>(), APPROX>(stream, 0, 0, sfpu_specific_parameter));
}

#pragma endregion

#pragma region Sfpu Ops

template<SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_unary_sfpu_op(tt_core* core_ptr, int stream, int dst_index, int dim, unsigned int sfpu_specific_param_0, unsigned int sfpu_specific_param_1, unsigned int param_2, unsigned int param_3, unsigned int param_4, unsigned int param_5) {
    SFPU_THREAD(llk_math_eltwise_unary_sfpu<get_sfpu_type_from_sfpu_op<sfpu_op>(), APPROX, DST_SYNC, IS_INT_FPU_EN>(stream, dst_index, dim, sfpu_specific_param_0, sfpu_specific_param_1, param_2, param_3, param_4, param_5));
}

#pragma endregion

#pragma endregion

#pragma region Hw Configs

TT_HLK_ALWAYS_INLINE void hlk_hw_config_single_operand(tt_core* core_ptr, int stream, int transpose) {
    UNPACK(llk_unpack_A_hw_configure_disaggregated<IS_FP32_DEST_ACC_EN, STOCH_RND_TYPE>(stream, transpose));

    PACK(
        llk_pack_hw_configure_disaggregated<IS_UNTILIZE_OUTPUT_EN, IS_FP32_DEST_ACC_EN, RELU_TYPE, RELU_THRESHOLD>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_hw_config_two_operands(tt_core* core_ptr, int stream_a, int stream_b, int transpose) {
    UNPACK(
        llk_unpack_AB_hw_configure_disaggregated<IS_FP32_DEST_ACC_EN, STOCH_RND_TYPE>(stream_a, stream_b, transpose));

    PACK(
        llk_pack_hw_configure_disaggregated<IS_UNTILIZE_OUTPUT_EN, IS_FP32_DEST_ACC_EN, RELU_TYPE, RELU_THRESHOLD>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_hw_config_matmul(tt_core* core_ptr, int stream_a, int stream_b, int transpose) {
    UNPACK(llk_unpack_AB_matmul_hw_configure_disaggregated<IS_FP32_DEST_ACC_EN, STOCH_RND_TYPE>(
        stream_a, stream_b, transpose));

    PACK(
        llk_pack_hw_configure_disaggregated<IS_UNTILIZE_OUTPUT_EN, IS_FP32_DEST_ACC_EN, RELU_TYPE, RELU_THRESHOLD>(16));
}

template<ReduceFunc pool_type, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_hw_config_reduce(tt_core* core_ptr, int stream, float coefficient) {
    UNPACK(llk_unpack_reduce_hw_configure_disaggregated<
           (ckernel::PoolType)pool_type,
           get_reduce_dim_from_dim<dim>(),
           IS_FP32_DEST_ACC_EN,
           STOCH_RND_TYPE>(stream, coefficient));

    // check relu
    PACK(llk_pack_reduce_hw_configure_disaggregated<
         IS_UNTILIZE_OUTPUT_EN,
         (ckernel::PoolType)pool_type,
         get_reduce_dim_from_dim<dim>(),
         IS_FP32_DEST_ACC_EN,
         RELU_TYPE,
         RELU_THRESHOLD>(16));
}

#pragma endregion

#pragma region Fpu

#pragma region Fpu Inits

TT_HLK_ALWAYS_INLINE void hlk_mm_block_init_short(
    tt_core* core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_AB_matmul_init(lstream, rstream, transpose, inner_c_dim, inner_r_dim, inner_d_dim));
    MATH(llk_math_matmul_init<MATH_FIDELITY_DESC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(
        lstream, rstream, transpose, inner_c_dim, inner_r_dim, inner_d_dim));
}

TT_HLK_ALWAYS_INLINE void hlk_mm_block_init(
    tt_core* core_ptr, int lstream, int rstream, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim) {
    hlk_mm_block_init_short(core_ptr, lstream, rstream, transpose, inner_c_dim, inner_r_dim, inner_d_dim);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT, IS_UNTILIZE_OUTPUT_EN>(
            16));
}

TT_HLK_ALWAYS_INLINE void hlk_mm_tile_init_short(tt_core* core_ptr, int lstream, int rstream, int transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_AB_matmul_init(lstream, rstream, transpose));
    MATH(llk_math_matmul_init<MATH_FIDELITY_DESC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(lstream, rstream, transpose));
}

TT_HLK_ALWAYS_INLINE void hlk_mm_tile_init(tt_core* core_ptr, int lstream, int rstream, int transpose) {
    hlk_mm_tile_init_short(core_ptr, lstream, rstream, transpose);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT, IS_UNTILIZE_OUTPUT_EN>(
            16));
}

template <BinaryOp op_type, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_init_short(
    tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_AB_init<get_broadcast_type_from_dim<broadcast_dim>()>(lstream, rstream, transpose, acc_to_dest));
    MATH(
        llk_math_eltwise_binary_init_with_operands<get_eltwise_binary_op_from_op_type<op_type>(), get_broadcast_type_from_dim<broadcast_dim>(), MATH_FIDELITY_DESC>(
            lstream, rstream, transpose, acc_to_dest));
}

template <BinaryOp op_type, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_init(
    tt_core* core_ptr, int lstream, int rstream, int transpose, int acc_to_dest) {
    hlk_binary_op_init_short<op_type, broadcast_dim>(core_ptr, lstream, rstream, transpose, acc_to_dest);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

template <BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest_init_short(tt_core* core_ptr) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_A_init<
                                 get_broadcast_type_from_dim<broadcast_dim>(),
                                 true,
                                 get_eltwise_binary_reuse_dest_type_from_binary_dst_reuse<dst_reuse>()>());
    MATH(llk_math_eltwise_binary_init<
         get_eltwise_binary_op_from_op_type<op_type>(),
         get_broadcast_type_from_dim<broadcast_dim>(),
         MATH_FIDELITY_DESC,
         get_eltwise_binary_reuse_dest_type_from_binary_dst_reuse<dst_reuse>()>());
}

template <BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest_init(tt_core* core_ptr) {
    hlk_binary_op_reuse_dest_init_short<op_type, dst_reuse, broadcast_dim>(core_ptr);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

template<ReduceFunc pool_type, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile_init_short(tt_core* core_ptr, int within_face_16x16_transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reduce_init<(ckernel::PoolType)pool_type, get_reduce_dim_from_dim<dim>()>(
        within_face_16x16_transpose));
    MATH(llk_math_reduce_init<(ckernel::PoolType)pool_type, get_reduce_dim_from_dim<dim>(), MATH_FIDELITY_DESC>(
        within_face_16x16_transpose));
}

template<ReduceFunc pool_type, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile_init(tt_core* core_ptr, int within_face_16x16_transpose) {
    hlk_reduce_tile_init_short<pool_type, dim>(core_ptr, within_face_16x16_transpose);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_tilize_init_short(tt_core* core_ptr, int stream, int ct_dim) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_tilize_init(stream, ct_dim));
    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE>(0, 0, stream));
}

TT_HLK_ALWAYS_INLINE void hlk_tilize_init(tt_core* core_ptr, int stream, int ct_dim) {
    hlk_tilize_init_short(core_ptr, stream, ct_dim);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_transpose_xy_tile_init_short(
    tt_core* core_ptr, int transpose_of_faces, int within_face_16x16_transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_A_init<BroadcastType::NONE, false>(transpose_of_faces, within_face_16x16_transpose, 0));
    MATH(llk_math_eltwise_unary_datacopy_init<A2D, BroadcastType::NONE>(
        transpose_of_faces, within_face_16x16_transpose, 0));
}

TT_HLK_ALWAYS_INLINE void hlk_transpose_xy_tile_init(
    tt_core* core_ptr, int transpose_of_faces, int within_face_16x16_transpose) {
    hlk_transpose_xy_tile_init_short(core_ptr, transpose_of_faces, within_face_16x16_transpose);
    PACK_IF_NOT_MATH_DECOUPLED(
        llk_init_packer_dest_offset_registers<DST_SYNC, DstTileFaceLayout::RowMajor, IS_UNTILIZE_OUTPUT_EN>(16));
}

#pragma endregion

#pragma region Fpu ops

template<BinaryOp op_type, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op(
    tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dst_index, int transpose) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_AB<get_broadcast_type_from_dim<broadcast_dim>()>(lstream, rstream, lindex, rindex, transpose));
    MATH(llk_math_eltwise_binary<
        get_eltwise_binary_op_from_op_type<op_type>(),
        get_broadcast_type_from_dim<broadcast_dim>(),
        DST_SYNC,
        MATH_FIDELITY_DESC,
        EltwiseBinaryReuseDestType::NONE,
        IS_FP32_DEST_ACC_EN>(lstream, rstream, dst_index));
}

template<BinaryOp op_type, BinaryOpDstReuse dst_reuse, Dim broadcast_dim>
TT_HLK_ALWAYS_INLINE void hlk_binary_op_reuse_dest(tt_core* core, int rstream, int rindex, int dst_index, int clear_fp_32_dst_acc) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_A<get_broadcast_type_from_dim<broadcast_dim>(), true, get_eltwise_binary_reuse_dest_type_from_binary_dst_reuse<dst_reuse>()>(rstream, rindex));
    MATH(llk_math_eltwise_binary<
         get_eltwise_binary_op_from_op_type<op_type>(),
         get_broadcast_type_from_dim<broadcast_dim>(),
         DST_SYNC,
         MATH_FIDELITY_DESC,
         get_eltwise_binary_reuse_dest_type_from_binary_dst_reuse<dst_reuse>(),
         IS_FP32_DEST_ACC_EN>(dst_index, clear_fp_32_dst_acc));
}

TT_HLK_ALWAYS_INLINE void hlk_mm_block(
    tt_core* core_ptr,
    int lstream,
    int rstream,
    int lindex,
    int rindex,
    int dst_index,
    int transpose,
    int inner_c_dim,
    int inner_r_dim,
    int inner_d_dim) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_AB_matmul(lstream, rstream, lindex, rindex, inner_c_dim, inner_r_dim, inner_d_dim));
    MATH(llk_math_matmul<MATH_FIDELITY_DESC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(
        dst_index, transpose, inner_c_dim, inner_r_dim, inner_d_dim));
}

TT_HLK_ALWAYS_INLINE void hlk_mm_tile(
    tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_AB_matmul(lstream, rstream, lindex, rindex, inner_c_dim));
    MATH(llk_math_matmul<MATH_FIDELITY_DESC, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(dstindex, transpose, inner_c_dim));
}

template<ReduceFunc pool_type, Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_tile(
    tt_core* core_ptr, int lstream, int lindex, int dst_index, float coefficient) {
    UNPACK_IF_NOT_MATH_DECOUPLED(
        llk_unpack_reduce<(ckernel::PoolType)pool_type, get_reduce_dim_from_dim<dim>()>(lstream, lindex));
    MATH(llk_math_reduce<
             (ckernel::PoolType)pool_type,
             get_reduce_dim_from_dim<dim>(),
             MATH_FIDELITY_DESC,
             IS_FP32_DEST_ACC_EN,
             IS_INT_FPU_EN>(dst_index););
}

TT_HLK_ALWAYS_INLINE void hlk_tilize(tt_core* core_ptr, int lstream, int lindex, int dst_index, int ct_dim) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_tilize(lstream, lindex, ct_dim));
    MATH(llk_math_eltwise_unary_datacopy<A2D, BroadcastType::NONE, DST_SYNC, IS_FP32_DEST_ACC_EN>(dst_index, lstream));
}

#pragma endregion

#pragma endregion

#pragma region Other HLKs

TT_HLK_ALWAYS_INLINE void hlk_pre_input_processing(tt_core* core_ptr, const int input_index) {
    // PERF_DUMP_EN
    {
        // This LLK executes on all 3 threads if PERF_DUMP is enabled
        if constexpr (IS_PERF_DUMP_EN == true) {
            set_perf_dump_flag_for_input(input_index);
        }
        MATH(if constexpr (IS_PERF_DUMP_EN == true) { perf_math_counter_start(); });

        PACK(if constexpr (IS_PERF_DUMP_EN == true) { serialize_input_loop_start<2>(); });
        PACK(if constexpr (IS_PERF_DUMP_EN == true) { record_pack_input_init_timestamp(); });
    }

    // KERNEL_DELAY
    {
        UNPACK(if constexpr (IS_KERNEL_UNPACK_DELAY_EN == true) { serialize_input_loop_start<0>(); });
        UNPACK(if constexpr (IS_KERNEL_UNPACK_DELAY_EN == true) { stall_kernel(INSERT_UNPACK_DELAY); });

        MATH(if constexpr (IS_KERNEL_MATH_DELAY_EN == true) { serialize_input_loop_start<1>(); });
        MATH(if constexpr (IS_KERNEL_MATH_DELAY_EN == true) { stall_kernel(INSERT_MATH_DELAY); });

        // If IS_PERF_DUMP_EN is true, then serialize_input_loop_start<2>() will have already been called, so no need to
        // call it
        PACK(if constexpr ((IS_KERNEL_PACK_DELAY_EN == true) && (IS_PERF_DUMP_EN == false)) {
            serialize_input_loop_start<2>();
        });
        PACK(if constexpr (IS_KERNEL_PACK_DELAY_EN == true) { stall_kernel(INSERT_PACK_DELAY); });
    }
}

TT_HLK_ALWAYS_INLINE void hlk_post_input_processing(tt_core* core_ptr) {
    // PERF_DUMP_EN
    {
        UNPACK(if constexpr (IS_PERF_DUMP_EN == true) { record_unpack_first_instruction_timestamp(); });

        MATH(if constexpr (IS_PERF_DUMP_EN == true) { record_perf_math_counter(); });

        PACK(if constexpr (IS_PERF_DUMP_EN == true) { serialize_input_loop_end<2>(); });
        PACK(if constexpr (IS_PERF_DUMP_EN == true) { record_pack_input_end_timestamp(); });
    }

    // KERNEL_DELAY
    {
        UNPACK(if constexpr (IS_KERNEL_UNPACK_DELAY_EN == true) { serialize_input_loop_end<0>(); });

        MATH(if constexpr (IS_KERNEL_MATH_DELAY_EN == true) { serialize_input_loop_end<1>(); });

        // If IS_PERF_DUMP_EN is true, then serialize_input_loop_end<2> will have already been called, so no need to
        // call it.
        PACK(if constexpr (IS_KERNEL_PACK_DELAY_EN == true && IS_PERF_DUMP_EN == false) {
            serialize_input_loop_end<2>();
        });
    }
}

// This is not a hlk, but it is a nice place to define this function.
void setup_kernel() {
    UNPACK(llk_setup_operands());

    MATH(llk_math_pack_sync_init<DST_SYNC, IS_FP32_DEST_ACC_EN>());

    PACK(llk_pack_init<IS_UNTILIZE_OUTPUT_EN, false, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(16));
    PACK(llk_setup_outputs());
    PACK(llk_pack_dest_init<
         DST_SYNC,
         MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT,
         IS_UNTILIZE_OUTPUT_EN,
         IS_FP32_DEST_ACC_EN>(16));
}

TT_HLK_ALWAYS_INLINE void hlk_debug_dump(tt_core* core_ptr, unsigned char* data, int size) {
    UNPACK(llk_unpack_debug_dump(data, size));

    MATH(llk_math_debug_dump(data, size));

    PACK(llk_pack_debug_dump(data, size));
}

TT_HLK_ALWAYS_INLINE void hlk_debug_dump_seek(tt_core* core_ptr, int offset) {
    UNPACK(llk_unpack_debug_dump_seek((uint8_t)offset));

    MATH(llk_math_debug_dump_seek((uint8_t)offset));

    PACK(llk_pack_debug_dump_seek((uint8_t)offset));
}

TT_HLK_ALWAYS_INLINE void hlk_acquire_dst(tt_core* core_ptr) {
    MATH(llk_math_wait_for_dest_available<DST_SYNC>());

    PACK(llk_packer_wait_for_math_done());
}

TT_HLK_ALWAYS_INLINE void hlk_release_dst(tt_core* core_ptr) {
    MATH(llk_math_dest_section_done<DST_SYNC, IS_FP32_DEST_ACC_EN>());

    PACK(llk_pack_dest_section_done<DST_SYNC, IS_FP32_DEST_ACC_EN>());
}

TT_HLK_ALWAYS_INLINE void hlk_relu_config(tt_core* core_ptr, int config) {
    PACK_IF_NOT_MATH_DECOUPLED(llk_pack_relu_config(config));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_l1_acc(tt_core* core_ptr, int config) {
    PACK_IF_NOT_MATH_DECOUPLED(llk_pack_reconfig_l1_acc(config));
}

TT_HLK_ALWAYS_INLINE void hlk_dbg_feature_disable(tt_core*) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_dbg_feature_disable());
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_df(tt_core* core_ptr, int old_stream, int new_stream) {
    PACK(llk_pack_reconfig_data_format<IS_FP32_DEST_ACC_EN, IS_TILE_DIM_PACK_RECONFIG_EN, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(old_stream, new_stream));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_packer_df(tt_core* core_ptr, int new_stream) {
    PACK(llk_pack_reconfig_data_format<IS_FP32_DEST_ACC_EN, IS_TILE_DIM_PACK_RECONFIG_EN, MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT>(new_stream));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df(
    tt_core* core_ptr, int old_lstream, int new_lstream, int old_rstream, int new_rstream) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format<IS_TILE_DIM_UNPACK_RECONFIG_EN>(old_lstream, new_lstream, old_rstream, new_rstream));
    MATH(llk_math_reconfig_data_format(old_lstream, new_lstream, old_rstream, new_rstream));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srca(tt_core* core_ptr, int old_srca, int new_srca) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format_srca<IS_TILE_DIM_UNPACK_RECONFIG_EN>(old_srca, new_srca));
    MATH(llk_math_reconfig_data_format_srca(old_srca, new_srca));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srcb(tt_core* core_ptr, int old_srcb, int new_srcb) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format_srcb<IS_TILE_DIM_UNPACK_RECONFIG_EN>(old_srcb, new_srcb));
    MATH(llk_math_reconfig_data_format_srcb(old_srcb, new_srcb));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df(tt_core* core_ptr, int new_lstream, int new_rstream) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format<IS_TILE_DIM_UNPACK_RECONFIG_EN>(new_lstream, new_rstream));
    MATH(llk_math_reconfig_data_format(new_lstream, new_rstream));
}
TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srca(tt_core* core_ptr, int new_srca) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format_srca<IS_TILE_DIM_UNPACK_RECONFIG_EN>(new_srca));
    MATH(llk_math_reconfig_data_format_srca(new_srca));
}

TT_HLK_ALWAYS_INLINE void hlk_reconfig_unpacker_df_srcb(tt_core* core_ptr, int new_srcb) {
    UNPACK_IF_NOT_MATH_DECOUPLED(llk_unpack_reconfig_data_format_srcb<IS_TILE_DIM_UNPACK_RECONFIG_EN>(new_srcb));
    MATH(llk_math_reconfig_data_format_srcb(new_srcb));
}

TT_HLK_ALWAYS_INLINE void hlk_reduce_mask_clear(tt_core* core_ptr) {
    PACK_IF_NOT_MATH_DECOUPLED(llk_pack_reduce_mask_clear());
}

template<Dim dim>
TT_HLK_ALWAYS_INLINE void hlk_reduce_mask_config(tt_core* core_ptr) {
    PACK(llk_pack_reduce_mask_config<IS_UNTILIZE_OUTPUT_EN, get_reduce_dim_from_dim<dim>()>());
}

#pragma endregion
