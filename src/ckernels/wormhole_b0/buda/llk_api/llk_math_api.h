// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ckernel.h"
#include "ckernel_defs.h"
#include "ckernel_template.h"
#include "cmath_common.h"
#include "ckernel_globals.h"

#include "llk_io.h"
#include "llk_defs.h"
#include "llk_sfpu_types.h"
#include "llk_operands.h"
#include "llk_param_structs.h"
#include "llk_math_eltwise_unary_datacopy.h"
#include "llk_math_eltwise_unary_sfpu.h"
#include "llk_math_eltwise_binary_sfpu.h"
#include "llk_math_eltwise_binary.h"
#include "llk_math_matmul.h"
#include "llk_math_reduce.h"
#include "llk_math_common.h"

/*************************************************************************
* LLK ELTWISE UNARY DATACOPY
*************************************************************************/ 

template <DataCopyType type, BroadcastType src_b_bcast_type = BroadcastType::NONE, DstSync Dst = DstSync::SyncFull, bool is_fp32_dest_acc_en = false, bool unpack_to_dest = false>
inline void llk_math_eltwise_unary_datacopy(uint dst_index, uint operand = 0) {
    TT_LLK_DUMP("llk_math_eltwise_unary_datacopy<{}, {}, {}, {}, {}>({}, {})", type, src_b_bcast_type, Dst, is_fp32_dest_acc_en, unpack_to_dest, dst_index, operand);
    const std::uint32_t operand_id = get_operand_id(operand);
    _llk_math_eltwise_unary_datacopy_<type, src_b_bcast_type, Dst, is_fp32_dest_acc_en, unpack_to_dest>(
        dst_index,
        unpack_src_format[operand_id],
        unpack_dst_format[operand_id]
    );
}

template <DataCopyType type, BroadcastType src_b_bcast_type = BroadcastType::NONE>
// within_face_16x16_transpose is used by unpacker, math does not transpose
inline void llk_math_eltwise_unary_datacopy_init(const std::uint32_t transpose_of_faces=0 /*unused*/, const std::uint32_t within_face_16x16_transpose=0 /* unused */, const std::uint32_t operand = 0) {
    TT_LLK_DUMP("llk_math_eltwise_unary_datacopy_init<{}, {}>({}, {}, {})", type, src_b_bcast_type, transpose_of_faces, within_face_16x16_transpose, operand);
    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);
    _llk_math_eltwise_unary_datacopy_init_<type, src_b_bcast_type>(
        transpose_of_faces,
        within_face_16x16_transpose,
        num_faces
    );
}    

/*************************************************************************
* LLK ELTWISE BINARY
*************************************************************************/ 

// Version with no operand
template <
    EltwiseBinaryType eltwise_binary_type,
    BroadcastType src_b_bcast_type,
    int MATH_FIDELITY_DESC = 0,
    EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE>
inline void llk_math_eltwise_binary_init(const std::uint32_t transpose=0, const std::uint32_t acc_to_dest = 0) {
    TT_LLK_DUMP("llk_math_eltwise_binary_init<{}, {}, {}, {}>({}, {})", eltwise_binary_type, src_b_bcast_type, MATH_FIDELITY_DESC, binary_reuse_dest, transpose, acc_to_dest);

    const std::uint32_t num_faces = 4;

    _llk_math_eltwise_binary_init_<eltwise_binary_type, src_b_bcast_type, MATH_FIDELITY_DESC, binary_reuse_dest>(
        num_faces, 
        transpose, 
        acc_to_dest
    );
}

// Version with operands
template <
    EltwiseBinaryType eltwise_binary_type,
    BroadcastType src_b_bcast_type,
    int MATH_FIDELITY_DESC = 0,
    EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE>
inline void llk_math_eltwise_binary_init_with_operands(const std::uint32_t operand_A, const std::uint32_t operand_B, const std::uint32_t transpose=0, const std::uint32_t acc_to_dest = 0) {
    TT_LLK_DUMP("llk_math_eltwise_binary_init_with_operands<{}, {}, {}, {}>({}, {}, {}, {})", eltwise_binary_type, src_b_bcast_type, MATH_FIDELITY_DESC, binary_reuse_dest, operand_A, operand_B, transpose, acc_to_dest);

    const std::uint32_t operand_id = get_operand_id(operand_A); // operand_id is used to extract tile dim data which is the same for both operands
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);

    _llk_math_eltwise_binary_init_<eltwise_binary_type, src_b_bcast_type, MATH_FIDELITY_DESC, binary_reuse_dest>(
        num_faces, 
        transpose, 
        acc_to_dest
    );
}

template <
    EltwiseBinaryType eltwise_binary_type,
    BroadcastType src_b_bcast_type,
    DstSync Dst = DstSync::SyncFull,
    int MATH_FIDELITY_DESC = 0,
    EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE,
    bool is_fp32_dest_acc_en = false>
inline void llk_math_eltwise_binary(uint dst_index, const bool clear_fp32_dst_acc = true) {

    TT_LLK_DUMP("llk_math_eltwise_binary<{}, {}, {}, {}, {}, {}>({}, {})", eltwise_binary_type, src_b_bcast_type, Dst, MATH_FIDELITY_DESC, binary_reuse_dest, is_fp32_dest_acc_en, dst_index, clear_fp32_dst_acc);

    const std::uint32_t num_faces = 4;

    _llk_math_eltwise_binary_<eltwise_binary_type, src_b_bcast_type, Dst, MATH_FIDELITY_DESC, binary_reuse_dest, is_fp32_dest_acc_en>(
        num_faces, 
        dst_index, 
        clear_fp32_dst_acc
    );
}

template <
    EltwiseBinaryType eltwise_binary_type,
    BroadcastType src_b_bcast_type,
    DstSync Dst = DstSync::SyncFull,
    int MATH_FIDELITY_DESC = 0,
    EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE,
    bool is_fp32_dest_acc_en = false>
inline void llk_math_eltwise_binary(const std::uint32_t operand_A, const std::uint32_t operand_B, uint dst_index, const bool clear_fp32_dst_acc = true) {
    TT_LLK_DUMP("llk_math_eltwise_binary<{}, {}, {}, {}, {}, {}>({}, {}, {}, {})", eltwise_binary_type, src_b_bcast_type, Dst, MATH_FIDELITY_DESC, binary_reuse_dest, is_fp32_dest_acc_en, operand_A, operand_B, dst_index, clear_fp32_dst_acc);

    const std::uint32_t operand_id = get_operand_id(operand_A); // both operands must have same number of faces
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);

    _llk_math_eltwise_binary_<eltwise_binary_type, src_b_bcast_type, Dst, MATH_FIDELITY_DESC, binary_reuse_dest, is_fp32_dest_acc_en>(
        num_faces, 
        dst_index, 
        clear_fp32_dst_acc
    );
}

/*************************************************************************
* LLK ELTWISE UNARY&BINARY SFPU
*************************************************************************/ 

template <SfpuType operation, bool APPROXIMATION_MODE, int SfpuType_PARAM=0, int ITERATIONS=8, bool IS_INT_SFPU_EN = false>
inline void llk_math_calculate_sfpu(const int iterations = ITERATIONS, uint param0 = 0, uint param1 = 0, uint param2 = 0, uint param3 = 0, uint param4 = 0, uint param5 = 0)
{
    if constexpr (operation == SfpuType::exponential) {
        sfpu::_calculate_exponential_<APPROXIMATION_MODE, false, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::exp_with_base) {
        sfpu::_calculate_exponential_<APPROXIMATION_MODE, true, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::tanh) {
        sfpu::_calculate_tanh_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::hardtanh) {
        sfpu::_calculate_hardtanh_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0, param1, param2);
    }
    else if constexpr (operation == SfpuType::gelu) {
        sfpu::_calculate_gelu_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::reciprocal) {
        sfpu::_calculate_reciprocal_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::sigmoid) {
        sfpu::_calculate_sigmoid_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::sqrt) {
        sfpu::_calculate_sqrt_<APPROXIMATION_MODE, ITERATIONS, 2>(iterations);
    }
    else if constexpr (operation == SfpuType::tanh_derivative) {
        sfpu::_calculate_tanh_derivative_<APPROXIMATION_MODE, SfpuType_PARAM, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::lrelu) {
        sfpu::_calculate_lrelu_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::dropout) {
        sfpu::_calculate_dropout_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0, param1);
    }
    else if constexpr (operation == SfpuType::power) {
        sfpu::_calculate_power_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::square) {
        sfpu::_calculate_square_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::log) {
        sfpu::_calculate_log_<APPROXIMATION_MODE, false, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::log_with_base) {
        sfpu::_calculate_log_<APPROXIMATION_MODE, true, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::gelu_derivative) {
        sfpu::_calculate_gelu_derivative_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr ((operation == SfpuType::equal_zero) || 
                       (operation == SfpuType::not_equal_zero) ||
                       (operation == SfpuType::less_than_zero) || 
                       (operation == SfpuType::greater_than_equal_zero) ||
                       (operation == SfpuType::less_than_equal_zero) ||
                       (operation == SfpuType::greater_than_zero)) {
        //invert output and use same comparison check
        constexpr bool invert_output = ((operation == SfpuType::greater_than_equal_zero) ||
                                        (operation == SfpuType::not_equal_zero) ||
                                        (operation == SfpuType::greater_than_zero));
        constexpr bool check_zero = (operation == SfpuType::equal_zero) || (operation == SfpuType::not_equal_zero);
        constexpr bool second_check = (operation == SfpuType::less_than_equal_zero) || (operation == SfpuType::greater_than_zero);
        constexpr bool is_less_than_equal_zero = (operation == SfpuType::less_than_equal_zero);
        sfpu::_calculate_comp_<APPROXIMATION_MODE, invert_output, check_zero, second_check, is_less_than_equal_zero, ITERATIONS>(iterations, param5);
    }
    else if constexpr (operation == SfpuType::clamp) {
        sfpu::_calculate_clamp_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0, param1, param2);
    }
    else if constexpr (operation == SfpuType::abs) {
        sfpu::_calculate_abs_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::sign) {
        sfpu::_calculate_sign_<APPROXIMATION_MODE, ITERATIONS>(iterations, param5);
    }
    else if constexpr (operation == SfpuType::max) {
        if constexpr (IS_INT_SFPU_EN)
            sfpu::_calculate_max_int32_<APPROXIMATION_MODE, ITERATIONS>(iterations);
        else
            sfpu::_calculate_max_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::sine) {
        sfpu::_calculate_sine_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::cosine) {
        sfpu::_calculate_cosine_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::relu_min) {
        sfpu::_relu_min_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::relu_max) {
        sfpu::_relu_max_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::cast_fp32_to_fp16a) {
        sfpu::_cast_fp32_to_fp16a_<APPROXIMATION_MODE, ITERATIONS>(iterations);
    }
    else if constexpr (operation == SfpuType::quant_int32) {
        sfpu::_quant_int32_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::requant_int32) {
        sfpu::_requant_int32_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::dequant_int32) {
        sfpu::_dequant_int32_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
    else if constexpr (operation == SfpuType::add_int32) {
        sfpu::_add_int32_<APPROXIMATION_MODE, ITERATIONS>(iterations, param0);
    }
}

/*************************************************************************
* LLK ELTWISE UNARY SFPU
*************************************************************************/ 

template <SfpuType sfpu_op, bool APPROXIMATE, DstSync Dst = DstSync::SyncFull, bool IS_INT_SFPU_EN=false>
inline void llk_math_eltwise_unary_sfpu(
    const uint operand,
    uint dst_index, 
    int vector_mode = (int)VectorMode::RC,
    uint param0 = 0,
    uint param1 = 0,
    uint param2 = 0,
    uint param3 = 0,
    uint param4 = 0,
    uint param5 = 0) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu<{}, {}, {}, {}>({}, {}, {}, {}, {}, {}, {}, {}, {})", (int)sfpu_op, APPROXIMATE, Dst, IS_INT_SFPU_EN, operand, dst_index, vector_mode, param0, param1, param2, param3, param4, param5);

    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operand_id);

    constexpr int ITERATIONS = 8;

    _llk_math_eltwise_unary_sfpu_start_<Dst>(dst_index);

    if (vector_mode == (int)VectorMode::R) {
        // Do a row vector, Face0 + Face1 -- first iteration (first row)
        const int iterations = (num_faces < 4) ? 
                                    ((face_r_dim <= 2) ? 2 : face_r_dim/2) : 2; // At least 2 iterations for odd and even columns
#pragma GCC unroll 0
        for (int face = 0; face < 2; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS, IS_INT_SFPU_EN>(iterations, param0, param1, param2, param3, param4, param5);
            // Move to the next face
            _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
        }
        // Skip next two faces
        _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
        _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
    } else if (vector_mode == (int)VectorMode::C) {
        // Do a column vector, Face0 + Face2 if tile is 32x32 or Face0+Face1 if tiles is 32x16 -- All iterations for full face
#pragma GCC unroll 0
        for (int face = 0; face < 2; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS, IS_INT_SFPU_EN>(ITERATIONS, param0, param1, param2, param3, param4, param5);
            _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
            if (num_faces>2) { // Skip next face if tile is 32x32
                _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
            }
        }
        if (num_faces<=2) { 
            // Skip next two faces
            _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
            _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
        }    
    } else {
        // Do all four faces, and iterate through all 4 blocks of 4 rows each
#pragma GCC unroll 0
        for (int face = 0; face < 4; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS, IS_INT_SFPU_EN>(ITERATIONS, param0, param1, param2, param3, param4, param5);
            // Move to the next face
            _llk_math_eltwise_unary_sfpu_inc_dst_face_addr_();
        }
    }
    _llk_math_eltwise_unary_sfpu_done_();
}

template <SfpuType sfpu_op, bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_init(
    const uint operand, const uint param0 = 0, const uint param1 = 0, const uint param2 = 0, const uint param3 = 0, const uint param4 = 0, const uint param5 = 0) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_init<{}, {}>({}, {}, {}, {}, {}, {})", sfpu_op, APPROXIMATE, param0, param1, param2, param3, param4, param5);

    _llk_math_eltwise_unary_sfpu_init_();

    switch (sfpu_op) {
        case SfpuType::reciprocal:
            sfpu::_init_reciprocal_<APPROXIMATE>();
            break;
        case SfpuType::exponential:
            sfpu::_init_exponential_<APPROXIMATE>();
            break;
        case SfpuType::log:
            sfpu::_init_log_<APPROXIMATE>();
            break;
        case SfpuType::sqrt:
            sfpu::_init_sqrt_<APPROXIMATE>();
            break;
        case SfpuType::tanh:
        case SfpuType::tanh_derivative:
            sfpu::_init_tanh_<APPROXIMATE>();
            break;
        case SfpuType::sigmoid:
            sfpu::_init_sigmoid_<APPROXIMATE>();
            break;
        case SfpuType::gelu_derivative:
            sfpu::_init_gelu_derivative_<APPROXIMATE>();
            break;
        case SfpuType::gelu:
            sfpu::_init_gelu_<APPROXIMATE>();
            break;
        case SfpuType::dropout:
            sfpu::_init_dropout_(param2);
            break;
        default:
            break;
    }
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_exponential(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_exponential<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::exponential, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_exponential_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::exponential, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_sqrt(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_sqrt<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::sqrt, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_sqrt_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::sqrt, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_gelu(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_gelu<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::gelu, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_gelu_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::gelu, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_gelu_derivative(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_gelu_derivative<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::gelu_derivative, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_gelu_derivative_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::gelu_derivative, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_reciprocal(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_reciprocal<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::reciprocal, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_reciprocal_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::reciprocal, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_log(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_log<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::log, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_log_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::log, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_tanh(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_tanh<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::tanh, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_tanh_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::tanh, APPROXIMATE>(operand);
}

template <DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_dropout(const uint operand, uint dst_index, int vector_mode, int integer_dropout, int scale_factor) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_dropout<{}>({}, {}, {}, {})", dst_sync, dst_index, vector_mode, integer_dropout, scale_factor);
    constexpr bool dont_care = false;
    llk_math_eltwise_unary_sfpu<SfpuType::dropout, dont_care, dst_sync>(operand, dst_index, vector_mode, integer_dropout, scale_factor);
}

inline void llk_math_eltwise_unary_sfpu_dropout_init(const uint operand, uint seed = 0) {
    constexpr bool dont_care = false;
    constexpr uint dont_care_param = 0;

    llk_math_eltwise_unary_sfpu_init<SfpuType::dropout, dont_care>(operand, dont_care_param, dont_care_param, seed);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_sigmoid(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_sigmoid<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::sigmoid, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_sigmoid_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::sigmoid, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_max(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_max<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::max, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_max_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::max, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_square(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_square<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::square, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_square_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::square, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_power(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC, int pow = 0) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_power<{}, {}>({}, {}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode, pow);
    llk_math_eltwise_unary_sfpu<SfpuType::power, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode, pow);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_power_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::power, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_sine(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_sine<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::sine, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_sine_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::sine, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_cosine(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_cosine<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::cosine, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_cosine_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::cosine, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_lrelu(const uint operand, uint dst_index, int vector_mode, uint uint_slope) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_lrelu<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::lrelu, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode, uint_slope);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_lrelu_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::lrelu, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_relu_max(const uint operand, uint dst_index, int vector_mode, uint uint_threshold) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_relu_max<{}, {}>({}, {}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode, uint_threshold);
    llk_math_eltwise_unary_sfpu<SfpuType::relu_max, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode, uint_threshold);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_relu_max_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::relu_max, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_relu_min(const uint operand, uint dst_index, int vector_mode, uint uint_threshold) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_relu_min<{}, {}>({}, {}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode, uint_threshold);
    llk_math_eltwise_unary_sfpu<SfpuType::relu_min, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode, uint_threshold);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_relu_min_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::relu_min, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_abs(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    TT_LLK_DUMP("llk_math_eltwise_unary_sfpu_abs<{}, {}>({}, {})", APPROXIMATE, dst_sync, dst_index, vector_mode);
    llk_math_eltwise_unary_sfpu<SfpuType::abs, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_abs_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::abs, APPROXIMATE>(operand);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_cast_fp32_to_fp16a(const uint operand, uint dst_index, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_unary_sfpu<SfpuType::cast_fp32_to_fp16a, APPROXIMATE, dst_sync>(operand, dst_index, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_cast_fp32_to_fp16a_init(const uint operand) {
    llk_math_eltwise_unary_sfpu_init<SfpuType::cast_fp32_to_fp16a, APPROXIMATE>(operand);
}

/*************************************************************************
* LLK ELTWISE BINARY SFPU
*************************************************************************/ 

template <SfpuType sfpu_op, bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_binary_sfpu(
    const uint operand,
    uint dst_index_a,
    uint dst_index_b,
    int vector_mode = (int)VectorMode::RC,
    uint param0 = 0,
    uint param1 = 0,
    uint param2 = 0,
    uint param3 = 0,
    uint param4 = 0,
    uint param5 = 0) {
    TT_LLK_DUMP("llk_math_eltwise_binary_sfpu<{}, {}, {}>({}, {}, {}, {}, {}, {}, {}, {}, {}, {})", sfpu_op, APPROXIMATE, Dst, operand, dst_index_a, dst_index_b, vector_mode, param0, param1, param2, param3, param4, param5);

    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operand_id);

    constexpr int ITERATIONS = 8;
    uint dst_index = (dst_index_a <= dst_index_b) ? dst_index_a : dst_index_b;
    param0 = (dst_index_a > dst_index_b) ? dst_index_a-dst_index_b : dst_index_b-dst_index_a;

    _llk_math_eltwise_binary_sfpu_start_(dst_index);

    if (vector_mode == (int)VectorMode::R) {
        // Do a row vector, Face0 + Face1 -- first iteration (first row)
        const int iterations = (num_faces < 4) ? 
                                    ((face_r_dim <= 2) ? 2 : face_r_dim/2) : 2; // At least 2 iterations for odd and even columns
#pragma GCC unroll 0
        for (int face = 0; face < 2; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS>(iterations, param0, param1, param2, param3, param4, param5);
            _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
        }
        _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
        _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
    } else if (vector_mode == (int)VectorMode::C) {
        // Do a column vector, Face0 + Face2 if tile is 32x32 or Face0+Face1 if tiles is 32x16 -- All iterations for full face
#pragma GCC unroll 0
        for (int face = 0; face < 2; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS>(ITERATIONS, param0, param1, param2, param3, param4, param5);
            _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
            if (num_faces>2) { // Skip next 2 faces if tile is 32x32
                _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
            }
        }
        if (num_faces<=2) { 
            _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
            _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
        }    
    } else {
        // Do all four faces, and iterate through all 4 blocks of 4 rows each
#pragma GCC unroll 0
        for (int face = 0; face < 4; face++) {
            llk_math_calculate_sfpu<sfpu_op, APPROXIMATE, 0, ITERATIONS>(ITERATIONS, param0, param1, param2, param3, param4, param5);
            _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_();
        }
    }

    _llk_math_eltwise_binary_sfpu_done_();
}

template <SfpuType sfpu_op, bool APPROXIMATE>
inline void llk_math_eltwise_binary_sfpu_init(
    const uint param0 = 0, const uint param1 = 0, const uint param2 = 0, const uint param3 = 0, const uint param4 = 0, const uint param5 = 0) {
    TT_LLK_DUMP("llk_math_eltwise_binary_sfpu_init<{}, {}>({}, {}, {}, {}, {}, {})", sfpu_op, APPROXIMATE, param0, param1, param2, param3, param4, param5);

    _llk_math_eltwise_binary_sfpu_init_();

    if constexpr (sfpu_op == SfpuType::quant_int32) {
        sfpu::init_quant_zero_point(param0);
    } else if constexpr (sfpu_op == SfpuType::requant_int32) {
        sfpu::init_quant_zero_point(param0);
    } else if constexpr (sfpu_op == SfpuType::dequant_int32) {
        sfpu::init_quant_zero_point(param0);
    } else {
        
    }
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_binary_sfpu_quant_int32(const uint operand, uint dst_index_a, uint dst_index_b, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_binary_sfpu<SfpuType::quant_int32, APPROXIMATE, dst_sync>(operand, dst_index_a, dst_index_b, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_binary_sfpu_quant_int32_init(const uint zero_point) {
    llk_math_eltwise_binary_sfpu_init<SfpuType::quant_int32, APPROXIMATE>(zero_point);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_binary_sfpu_requant_int32(const uint operand, uint dst_index_a, uint dst_index_b, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_binary_sfpu<SfpuType::requant_int32, APPROXIMATE, dst_sync>(operand, dst_index_a, dst_index_b, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_binary_sfpu_requant_int32_init(const uint zero_point) {
    llk_math_eltwise_binary_sfpu_init<SfpuType::requant_int32, APPROXIMATE>(zero_point);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_binary_sfpu_dequant_int32(const uint operand, uint dst_index_a, uint dst_index_b, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_binary_sfpu<SfpuType::dequant_int32, APPROXIMATE, dst_sync>(operand, dst_index_a, dst_index_b, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_binary_sfpu_dequant_int32_init(const uint zero_point) {
    llk_math_eltwise_binary_sfpu_init<SfpuType::dequant_int32, APPROXIMATE>(zero_point);
}

template <bool APPROXIMATE, DstSync dst_sync = DstSync::SyncFull>
inline void llk_math_eltwise_binary_sfpu_add_int32(const uint operand, uint dst_index_a, uint dst_index_b, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_binary_sfpu<SfpuType::add_int32, APPROXIMATE, dst_sync>(operand, dst_index_a, dst_index_b, vector_mode);
}

template <bool APPROXIMATE>
inline void llk_math_eltwise_binary_sfpu_add_int32_init() {
    llk_math_eltwise_binary_sfpu_init<SfpuType::add_int32, APPROXIMATE>();
}

/*************************************************************************
* LLK MATMUL
*************************************************************************/ 

template <int MATH_FIDELITY_DESC, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void llk_math_matmul_init(const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t transpose=0, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {

    TT_LLK_DUMP("llk_math_matmul_init<{}, {}>({}, {}, {}, {}, {}, {})", MATH_FIDELITY_DESC, FaceLayout, operandA, operandB, transpose, ct_dim, rt_dim, kt_dim);

    const std::uint32_t in0_id = get_operand_id(operandA);
    const std::uint32_t in1_id = get_operand_id(operandB);

    const bool partial_face = get_operand_partial_face(in0_id);
    
    const std::uint32_t in0_tile_r_dim = unpack_tile_dims[in0_id][ckernel::TileDim::R_IDX];
    const std::uint32_t in0_tile_c_dim = unpack_tile_dims[in0_id][ckernel::TileDim::C_IDX];
    const std::uint32_t in1_tile_r_dim = unpack_tile_dims[in1_id][ckernel::TileDim::R_IDX];
    const std::uint32_t in1_tile_c_dim = unpack_tile_dims[in1_id][ckernel::TileDim::C_IDX];

    _llk_math_matmul_init_<MATH_FIDELITY_DESC, FaceLayout>(
        in0_tile_r_dim,
        in0_tile_c_dim,
        in1_tile_r_dim,
        in1_tile_c_dim,
        partial_face,
        transpose,
        ct_dim,
        rt_dim,
        kt_dim
    );
}

template <int MATH_FIDELITY_DESC, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void llk_math_matmul(uint dst_index, const bool transpose=false, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {
    TT_LLK_DUMP("llk_math_matmul<{}, {}>({}, {}, {}, {}, {})", MATH_FIDELITY_DESC, FaceLayout, dst_index, transpose, ct_dim, rt_dim, kt_dim);

    _llk_math_matmul_<MATH_FIDELITY_DESC, FaceLayout>(
        dst_index,
        transpose,
        ct_dim,
        rt_dim,
        kt_dim
    );
}

/*************************************************************************
* LLK REDUCE
*************************************************************************/ 

template <PoolType type, ReduceDim dim, int MATH_FIDELITY_DESC = 0, bool is_fp32_dest_acc_en = false, bool is_int_fpu_en = false>
inline void llk_math_reduce(const uint dst_index) {
    TT_LLK_DUMP("llk_math_reduce<{}, {}, {}, {}, {}>({})", type, dim, MATH_FIDELITY_DESC, is_fp32_dest_acc_en, is_int_fpu_en, dst_index);

    _llk_math_reduce_<type, dim, MATH_FIDELITY_DESC, is_fp32_dest_acc_en, is_int_fpu_en>(
        dst_index
    );
}


template <PoolType type, ReduceDim dim, int MATH_FIDELITY_DESC = 0>
inline void llk_math_reduce_init(const std::uint32_t within_face_16x16_transpose=0) { //within_face_16x16_transpose used for unpack, ignored by math
    TT_LLK_DUMP("llk_math_reduce_init<{}, {}, {}>({})", type, dim, MATH_FIDELITY_DESC, within_face_16x16_transpose);

    _llk_math_reduce_init_<type, dim, MATH_FIDELITY_DESC>(
        within_face_16x16_transpose
    );
}

/*************************************************************************
* LLK MATH COMMON
*************************************************************************/ 

template <DstSync Dst>
inline void llk_math_wait_for_dest_available() {
    TT_LLK_DUMP("llk_math_wait_for_dest_available<{}>()", Dst);

    _llk_math_wait_for_dest_available_<Dst>();
   
}

template <DstSync Dst = SyncFull, bool is_fp32_dest_acc_en = false>
inline void llk_math_dest_section_done() {
    TT_LLK_DUMP("llk_math_dest_section_done<{}, {}>()", Dst, is_fp32_dest_acc_en);

    _llk_math_dest_section_done_<Dst, is_fp32_dest_acc_en>();
}

template <DstSync Dst, bool is_fp32_dest_acc_en = false>
inline void llk_math_pack_sync_init() {
    TT_LLK_DUMP("llk_math_pack_sync_init<{}, {}>()", Dst, is_fp32_dest_acc_en);

    _llk_math_pack_sync_init_<Dst, is_fp32_dest_acc_en>();
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_math_get_tile(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t* p_tile) {
    TT_LLK_DUMP("llk_math_get_tile<{}, {}>({}, {}, tile_pointer)", mail2math, mail2pack, operand, 
    tile_index);

    _llk_math_get_tile_<mail2math, mail2pack>(
        tile_index, 
        p_tile
    );
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_math_release_tile(std::uint32_t operand) {
    TT_LLK_DUMP("llk_math_release_tile<{}, {}>({})", mail2math, mail2pack, operand);

    _llk_math_release_tile_<mail2math, mail2pack>();
}

inline void llk_math_debug_dump(std::uint8_t *data, std::uint32_t byte_size) {
    TT_LLK_DUMP("llk_math_debug_dump(ptr, {})", byte_size);
    _llk_math_debug_dump_(data, byte_size);
}

inline void llk_math_debug_dump_seek(std::uint8_t offset) {
    _llk_math_debug_dump_seek_(offset);
}

inline void llk_math_reconfig_data_format_srca(const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srca({})", srca_new_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    _llk_math_reconfig_data_format_srca_(
        unpack_dst_format[new_srca_operand_id]
    );

}

inline void llk_math_reconfig_data_format_srcb(const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srcb({})", srcb_new_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);
    _llk_math_reconfig_data_format_srcb_(
        unpack_dst_format[new_srcb_operand_id]
    );
}

inline void llk_math_reconfig_data_format(const std::uint32_t srca_new_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format({}, {})", srca_new_operand, srcb_new_operand);

    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    _llk_math_reconfig_data_format_( 
        unpack_dst_format[new_srca_operand_id],
        unpack_dst_format[new_srcb_operand_id]
    );
}

inline void llk_math_reconfig_data_format(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand, const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format({}, {}, {}, {})", srca_old_operand, srca_new_operand, srcb_old_operand, srcb_new_operand);
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id]) && (unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])) {
        llk_math_reconfig_data_format(srca_new_operand, srcb_new_operand);
    } else if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id])){
        llk_math_reconfig_data_format_srca(srca_new_operand);
    } else if((unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])){
        llk_math_reconfig_data_format_srcb(srcb_new_operand);
    }
}

inline void llk_math_reconfig_data_format_srca(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srca({}, {})", srca_old_operand, srca_new_operand);
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);

    if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id])){
        llk_math_reconfig_data_format_srca(srca_new_operand);
    }
}

inline void llk_math_reconfig_data_format_srcb(const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srcb({}, {})", srcb_old_operand, srcb_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if((unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])){
        llk_math_reconfig_data_format_srcb(srcb_new_operand);
    }
}
