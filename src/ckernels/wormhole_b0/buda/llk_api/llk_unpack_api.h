// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ckernel.h"
#include "ckernel_defs.h"
#include "ckernel_template.h"
#include "cunpack_common.h"
#include "ckernel_globals.h"

#include "llk_defs.h"
#include "llk_operands.h"
#include "llk_io.h"
#include "llk_param_structs.h"
#include "llk_unpack_A.h"
#include "llk_unpack_AB.h"
#include "llk_unpack_AB_matmul.h"
#include "llk_unpack_reduce.h"
#include "llk_unpack_tilize.h"
#include "llk_unpack_untilize.h"
#include "llk_unpack_common.h"

/*************************************************************************
* LLK UNPACK A  
*************************************************************************/ 

template <bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_A_hw_configure(const llk_unpack_A_params_t *unpack_A_params, const int within_face_16x16_transpose = 0) {
    const uint32_t unpA_operand_id = get_operand_id(unpack_A_params->unpA_operand);
    const uint32_t unpA_num_faces = get_operand_num_faces(unpA_operand_id);
    const uint32_t unpA_face_r_dim = get_operand_face_r_dim(unpA_operand_id);

    _llk_unpack_A_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpack_dst_format[unpA_operand_id], 
        unpA_face_r_dim, 
        within_face_16x16_transpose, 
        unpA_num_faces);
}

template <bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_A_hw_configure_disaggregated(const std::uint32_t unpA_operand, const int within_face_16x16_transpose = 0) {
    TT_LLK_DUMP("llk_unpack_A_hw_configure_disaggregated<{}, {}>({}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, within_face_16x16_transpose);
    const llk_unpack_A_params_t unpack_A_params = {
        .unpA_operand = unpA_operand
    };
    llk_unpack_A_hw_configure<is_fp32_dest_acc_en, stoch_rnd_mode>(&unpack_A_params, within_face_16x16_transpose);
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false, EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE, bool unpack_to_dest = false>
inline void llk_unpack_A_mop_config(const bool transpose_of_faces, const std::uint32_t operand_id, const std::uint32_t unpack_src_format = 0, std::uint32_t unpack_dst_format = 0) {
    const std::uint32_t num_faces = get_operand_num_faces(operand_id); 

    _llk_unpack_A_mop_config_<BType, acc_to_dest, binary_reuse_dest, unpack_to_dest>(
        transpose_of_faces>0, 
        num_faces, 
        unpack_src_format, 
        unpack_dst_format
    );
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false, EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE, bool unpack_to_dest = false>
inline void llk_unpack_A_init(const std::uint32_t transpose_of_faces=0, const std::uint32_t within_face_16x16_transpose=0, const std::uint32_t operand = 0) {
    TT_LLK_DUMP("llk_unpack_A_init<{}, {}, {}>({}, {}, {})", BType, acc_to_dest, binary_reuse_dest, transpose_of_faces, within_face_16x16_transpose, operand);

    cfg_reg_rmw_tensix<THCON_SEC0_REG2_Haloize_mode_RMW>(within_face_16x16_transpose);

    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operand_id);
    const std::uint32_t num_faces  = get_operand_num_faces(operand_id);

    _llk_unpack_A_init_<BType, acc_to_dest, binary_reuse_dest, unpack_to_dest>(
        transpose_of_faces, 
        within_face_16x16_transpose, 
        face_r_dim, num_faces, 
        unpack_src_format[operand_id], 
        unpack_dst_format[operand_id]
    );
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false, EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE, bool unpack_to_dest = false>
inline void llk_unpack_A(const std::uint32_t operand, const std::uint32_t tile_index, const bool transpose_of_faces = 0) {
    TT_LLK_DUMP("llk_unpack_A<{}, {}, {}>({},{},{})", BType, acc_to_dest, binary_reuse_dest, operand, tile_index,  transpose_of_faces);

    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;
    std::uint32_t offset_address = operands[operand_id].f.tile_size_words * tile_index;
    std::uint32_t address = base_address + offset_address;

    _llk_unpack_A_<BType, acc_to_dest, binary_reuse_dest, unpack_to_dest>(
        address, 
        transpose_of_faces>0, 
        unpack_src_format[operand_id], 
        unpack_dst_format[operand_id]
    );
}

/*************************************************************************
* LLK UNPACK AB  
*************************************************************************/ 

template <bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_AB_hw_configure(const llk_unpack_AB_params_t *unpack_AB_params, const int within_face_16x16_transpose = 0) {
    // In0 -> unpA 
    // In1 -> unpB 
    const uint32_t unpA_operand_id = get_operand_id(unpack_AB_params->unpA_operand);
    const uint32_t unpB_operand_id = get_operand_id(unpack_AB_params->unpB_operand);

    // unpA -> srcA
    // unpB -> srcB
    const uint32_t num_faces = get_operand_num_faces(unpA_operand_id);  // num faces in unpA and unpB are the same
 
    const uint32_t face_r_dim = get_operand_face_r_dim(unpA_operand_id); // face r dim in unpA and unpB are the same

    _llk_unpack_AB_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpack_src_format[unpB_operand_id], 
        unpack_dst_format[unpA_operand_id], 
        unpack_dst_format[unpB_operand_id], 
        face_r_dim, 
        within_face_16x16_transpose, 
        num_faces
    );
}

template <bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_AB_hw_configure_disaggregated(
    const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const int within_face_16x16_transpose = 0 ) {

    TT_LLK_DUMP("llk_unpack_AB_hw_configure_disaggregated<{}, {}>({}, {}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, unpB_operand, within_face_16x16_transpose);
    
    const llk_unpack_AB_params_t unpack_AB_params = {.unpA_operand = unpA_operand, .unpB_operand = unpB_operand};

    llk_unpack_AB_hw_configure<is_fp32_dest_acc_en, stoch_rnd_mode>(&unpack_AB_params, within_face_16x16_transpose);
}

template <BroadcastType BType = BroadcastType::NONE>
inline void llk_unpack_AB_mop_config(const bool transpose_of_faces=false, const std::uint32_t operand_id=0) {
    const std::uint32_t num_faces = get_operand_num_faces(operand_id); 
    const bool narrow_tile = get_operand_narrow_tile(operand_id); // if narrow tile read face 0 twice for row broadcast
                                                          // or read face 0 and 1 for col broadcast
    _llk_unpack_AB_mop_config_<BType>(
        transpose_of_faces, 
        num_faces, 
        narrow_tile
    );
}

template <BroadcastType BType = BroadcastType::NONE>
inline void llk_unpack_AB_init(const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t transpose=0, const std::uint32_t acc_to_dest=0) {
    TT_LLK_DUMP("llk_unpack_AB_init<{}>({}, {}, {}, {})", BType, operandA, operandB, transpose, acc_to_dest);

    const std::uint32_t operandA_id = get_operand_id(operandA);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operandA_id); // face r dim in unpA and unpB are the same
    const std::uint32_t num_faces = get_operand_num_faces(operandA_id); 
    const bool narrow_tile = get_operand_narrow_tile(operandA_id); // if narrow tile read face 0 twice for row broadcast

    _llk_unpack_AB_init_<BType>(
        face_r_dim, 
        num_faces, 
        narrow_tile, 
        transpose, 
        acc_to_dest
    );
}    

template <BroadcastType BType = BroadcastType::NONE>
inline void llk_unpack_AB(
    const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t tile_index_a, const std::uint32_t tile_index_b, const bool transpose_of_faces = 0 /*not used*/) {
    TT_LLK_DUMP("llk_unpack_AB<{}>({}, {}, {}, {}, {}, {})", BType, operandA, operandB, tile_index_a, tile_index_b, transpose_of_faces);

    std::uint32_t operandA_id = get_operand_id(operandA);
    std::uint32_t operandB_id = get_operand_id(operandB);
    std::uint32_t base_address_a = operands[operandA_id].f.fifo_rd_ptr;
    std::uint32_t offset_address_a = operands[operandA_id].f.tile_size_words * tile_index_a;
    std::uint32_t address_a = base_address_a + offset_address_a;
    std::uint32_t base_address_b = operands[operandB_id].f.fifo_rd_ptr;
    std::uint32_t offset_address_b = operands[operandB_id].f.tile_size_words * tile_index_b;
    std::uint32_t address_b = base_address_b + offset_address_b;

    _llk_unpack_AB_<BType>(
        address_a, 
        address_b, 
        transpose_of_faces>0
    );
}

/*************************************************************************
* LLK UNPACK AB MATMUL  
*************************************************************************/ 

template<bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_AB_matmul_hw_configure(const llk_unpack_AB_matmul_params_t *unpack_AB_params) {
    const bool transpose_xy_srca = unpack_AB_params->transpose_xy_srca;

    // In0 -> unpB 
    // In1 -> unpA 
    const uint32_t unpA_operand_id = get_operand_id(unpack_AB_params->unpB_operand);
    const uint32_t unpB_operand_id = get_operand_id(unpack_AB_params->unpA_operand);

    // unpA -> srcA
    // unpB -> srcB
    const uint32_t unpA_num_faces = get_operand_num_faces(unpA_operand_id);
    const uint32_t unpB_num_faces = get_operand_num_faces(unpB_operand_id);

    const uint32_t unpA_face_r_dim = get_operand_face_r_dim(unpA_operand_id);
    const uint32_t unpB_face_r_dim = get_operand_face_r_dim(unpB_operand_id);

    _llk_unpack_AB_matmul_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpack_src_format[unpB_operand_id], 
        unpack_dst_format[unpA_operand_id], 
        unpack_dst_format[unpB_operand_id], 
        unpA_face_r_dim, 
        unpB_face_r_dim, 
        transpose_xy_srca, 
        unpA_num_faces, 
        unpB_num_faces,
        operands[unpA_operand_id].f.tile_size_words,
        operands[unpB_operand_id].f.tile_size_words
    );
}

template<bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_AB_matmul_hw_configure_disaggregated(
    const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const std::uint32_t transpose_xy_srca = 0) {
    TT_LLK_DUMP("llk_unpack_AB_matmul_hw_configure_disaggregated<{}, {}>({}, {}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, unpB_operand, transpose_xy_srca);
    const llk_unpack_AB_matmul_params_t unpack_AB_matmul_params = {
        .unpA_operand = unpA_operand, .unpB_operand = unpB_operand, .transpose_xy_srca = transpose_xy_srca };
    llk_unpack_AB_matmul_hw_configure<is_fp32_dest_acc_en, stoch_rnd_mode>(&unpack_AB_matmul_params);
}

inline void llk_unpack_AB_matmul_mop_config(const bool transpose, const std::uint32_t ct_dim, const std::uint32_t rt_dim, const std::uint32_t kt_dim, const bool partial_face) {
    // in0 - loaded to SrcB
    // in1 - loaded to SrcA
    _llk_unpack_AB_matmul_mop_config_(
        transpose, 
        ct_dim, 
        rt_dim, 
        kt_dim, 
        partial_face
    );
}

__attribute__((always_inline)) inline void llk_unpack_AB_matmul_init(const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t transpose=0, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {
    TT_LLK_DUMP("llk_unpack_AB_matmul_init({}, {}, {}, {}, {}, {})", operandA, operandB, transpose, ct_dim, rt_dim, kt_dim);

    // In0 -> srcB (supports partial face)
    // In1 -> srcA
    const uint32_t operandA_id = get_operand_id(operandB);
    const uint32_t operandB_id = get_operand_id(operandA);

    const uint32_t unpA_face_r_dim = get_operand_face_r_dim(operandA_id);
    const uint32_t unpB_face_r_dim = get_operand_face_r_dim(operandB_id);

    const bool reuse_a = ct_dim >= rt_dim; 
    const bool partial_face = get_operand_partial_face(operandB_id);

    const uint32_t unpA_num_faces = get_operand_num_faces(operandA_id);
    const uint32_t unpB_num_faces = partial_face ? 1 : get_operand_num_faces(operandB_id); // if partial face -> unpack face by face

    _llk_unpack_AB_matmul_init_(
        transpose, 
        ct_dim, 
        rt_dim, 
        kt_dim, 
        unpA_face_r_dim, 
        unpB_face_r_dim, 
        unpA_num_faces, 
        unpB_num_faces, 
        partial_face
    );
}

inline void llk_unpack_AB_matmul(
    const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t tile_index_a, const std::uint32_t tile_index_b, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {
    TT_LLK_DUMP("llk_unpack_AB_matmul({}, {}, {}, {}, {}, {, {})", operandA, operandB, tile_index_a, tile_index_b, ct_dim, rt_dim, kt_dim);
    // In0/InA -> srcB (supports partial face)
    // In1/InB -> srcA

    volatile uint *cfg = get_cfg_pointer();  // get pointer to registers for current state ID

    const std::uint32_t operandA_id = get_operand_id(operandA);
    const std::uint32_t operandB_id = get_operand_id(operandB);
    const std::uint32_t unpA_face_r_dim = get_operand_face_r_dim(operandB_id); // In1/InB -> srcA
    const std::uint32_t unpB_face_r_dim = get_operand_face_r_dim(operandA_id); // In0/InA -> srcB

    const bool partial_face = get_operand_partial_face(operandA_id);

    std::uint32_t base_address_a = operands[operandA_id].f.fifo_rd_ptr;
    std::uint32_t base_address_b = operands[operandB_id].f.fifo_rd_ptr;

    std::uint32_t tile_size_a = operands[operandA_id].f.tile_size_words;
    std::uint32_t tile_size_b = operands[operandB_id].f.tile_size_words;

    _llk_unpack_AB_matmul_(
        base_address_a, 
        base_address_b, 
        tile_index_a, 
        tile_index_b, 
        tile_size_a, 
        tile_size_b, 
        unpA_face_r_dim, 
        unpB_face_r_dim, 
        partial_face, 
        ct_dim, 
        rt_dim, 
        kt_dim
    );
}   

/*************************************************************************
* LLK UNPACK REDUCE  
*************************************************************************/ 

template <PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_reduce_hw_configure(
    const llk_unpack_reduce_params_t *unpack_reduce_params, const float const_mult) {

    constexpr bool within_face_16x16_transpose  = (ReduceDim::REDUCE_ROW == dim);

    const std::uint32_t unpA_operand_id = get_operand_id(unpack_reduce_params->unpA_operand);
    const std::uint32_t unpA_num_faces = get_operand_num_faces(unpA_operand_id);
    const std::uint32_t unpA_face_r_dim = get_operand_face_r_dim(unpA_operand_id);

    constexpr std::uint32_t unpB_src_format = (std::uint32_t) DataFormat::Float32;
    const std::uint32_t unpB_dst_format = ((std::uint32_t)unpack_dst_format[unpA_operand_id] == (std::uint32_t) DataFormat::Int8) ? (std::uint32_t) DataFormat::Float16 : // Int8 is treated as fp16_a 
                               ((((std::uint32_t)unpack_dst_format[unpA_operand_id]>>2)&0x1) ? (std::uint32_t) DataFormat::Float16_b : (std::uint32_t) DataFormat::Float16);

    _llk_unpack_reduce_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpB_src_format,
        unpack_dst_format[unpA_operand_id], 
        unpB_dst_format,
        unpA_face_r_dim, 
        unpA_face_r_dim, 
        within_face_16x16_transpose, 
        unpA_num_faces,
        unpA_num_faces
    );

    if constexpr (type != PoolType::MAX) {
        union {
            float f;
            uint32_t u;
        } f2u = {.f = const_mult};

        for (uint i = 0; i < 16; i++) l1_buffer[i] = f2u.u;  // Load const into L1 buffer
    }    
}

template <PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en=false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_reduce_hw_configure_disaggregated(const std::uint32_t unpA_operand, const float mult) {
    TT_LLK_DUMP("llk_unpack_reduce_hw_configure_disaggregated<{}, {}, {}, {}>({}, {})", type, dim, is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, mult);
    const llk_unpack_reduce_params_t unpack_reduce_params = {.unpA_operand = unpA_operand};
    llk_unpack_reduce_hw_configure<type, dim, is_fp32_dest_acc_en, stoch_rnd_mode>(&unpack_reduce_params, mult);
}

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce_mop_config() {
    _llk_unpack_reduce_mop_config_<type, dim>();
}

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce_init(const std::uint32_t within_face_16x16_transpose=0) {
    TT_LLK_DUMP("llk_unpack_reduce_init<{}, {}>({})", type, dim, within_face_16x16_transpose);

    constexpr std::uint32_t unpA_operand_id = 0;

    constexpr std::uint32_t unpB_src_format = (std::uint32_t) DataFormat::Float32;
    constexpr std::uint32_t unpB_dst_format = ((std::uint32_t)unpack_dst_format[unpA_operand_id] == (std::uint32_t) DataFormat::Int8) ? (std::uint32_t) DataFormat::Float16 : // Int8 is treated as fp16_a 
                               ((((std::uint32_t)unpack_dst_format[unpA_operand_id]>>2)&0x1) ? (std::uint32_t) DataFormat::Float16_b : (std::uint32_t) DataFormat::Float16);

    cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG1_SrcB_RMW>(unpB_dst_format);

    cfg_reg_rmw_tensix<THCON_SEC1_REG0_TileDescriptor_ADDR32, 0, 0xf>(unpB_src_format);
    cfg_reg_rmw_tensix<THCON_SEC1_REG2_Out_data_format_RMW>(unpB_dst_format);

    TTI_WRCFG(p_gpr_unpack::L1_BUFFER_ADDR, p_cfg::WRCFG_32b, THCON_SEC1_REG3_Base_address_ADDR32);
    TTI_WRCFG(p_gpr_unpack::L1_BUFFER_ADDR, p_cfg::WRCFG_32b, THCON_SEC1_REG3_Base_cntx1_address_ADDR32);
    TTI_NOP; TTI_NOP;

    _llk_unpack_reduce_init_<type, dim>(
        within_face_16x16_transpose
    );
}

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce(const std::uint32_t operand, const std::uint32_t tile_index) {
    TT_LLK_DUMP("llk_unpack_reduce<{}, {}>({}, {})", type, dim, operand, tile_index);

    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;
    std::uint32_t offset_address = operands[operand_id].f.tile_size_words * tile_index;
    std::uint32_t address = base_address + offset_address;

    _llk_unpack_reduce_<type, dim>(
        address
    );
}

/*************************************************************************
* LLK UNPACK TILIZE  
*************************************************************************/ 

template <bool is_fp32_dest_acc_en = false>
inline void llk_unpack_tilize_hw_configure(const llk_unpack_A_params_t *unpack_tilize_params) {

    constexpr bool  within_face_16x16_transpose = false;
    constexpr StochRndType stoch_rnd_mode = StochRndType::None;

    const uint32_t unpA_operand_id = get_operand_id(unpack_tilize_params->unpA_operand);
    const uint32_t unpA_num_faces = get_operand_num_faces(unpA_operand_id);
    const uint32_t unpA_face_r_dim = get_operand_face_r_dim(unpA_operand_id);

    _llk_unpack_tilize_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpack_dst_format[unpA_operand_id],
        unpA_face_r_dim, 
        within_face_16x16_transpose, 
        unpA_num_faces
    );
}


template <bool is_fp32_dest_acc_en = false>
inline void llk_unpack_tilize_hw_configure_disaggregated(
    const std::uint32_t unpA_operand) {
    TT_LLK_DUMP("llk_unpack_tilize_hw_configure_disaggregated<{}>({})", is_fp32_dest_acc_en, unpA_operand);
    const llk_unpack_A_params_t unpack_tilize_params = {
        .unpA_operand = unpA_operand
    };
    llk_unpack_tilize_hw_configure<is_fp32_dest_acc_en>(&unpack_tilize_params);
}

inline void llk_unpack_tilize_mop_config(const std::uint32_t operand) {
    std::uint32_t operand_id = get_operand_id(operand);
    const bool narrow_tile = get_operand_narrow_tile(operand_id);
    _llk_unpack_tilize_mop_config_(narrow_tile);
}

inline void llk_unpack_tilize_init(const std::uint32_t operand = 0, const std::uint32_t ct_dim = 0) {
    TT_LLK_DUMP("llk_unpack_tilize_init()");
    cfg_reg_rmw_tensix<THCON_SEC0_REG2_Haloize_mode_RMW>(0);

    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operand_id);
    const bool narrow_tile = get_operand_narrow_tile(operand_id);

    // Save state of unpacker config for quick restore
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_TILIZER_STATE_0, THCON_SEC0_REG2_Out_data_format_ADDR32); // Save unpack config[0]
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_TILIZER_STATE_1, THCON_SEC0_REG5_Tile_x_dim_cntx0_ADDR32); // Save tile x dim per context

    _llk_unpack_tilize_init_(
        unpack_src_format[operand_id], 
        unpack_dst_format[operand_id], 
        ct_dim, 
        face_r_dim, 
        narrow_tile
    );

}

inline void llk_unpack_tilize_uninit(const std::uint32_t face_r_dim = FACE_R_DIM) {
    TT_SETADCXX(p_setadc::UNP_A, face_r_dim*FACE_C_DIM-1, 0x0);
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG2_Out_data_format_ADDR32+0-THCON_CFGREG_BASE_ADDR32, p_gpr_unpack::SR_UNPACK_TILIZER_STATE_0); // Restore unpack config[0]
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG5_Tile_x_dim_cntx0_ADDR32-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_TILIZER_STATE_1); // Restore tile x dim per context
}

inline void llk_unpack_tilize(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t block_ct_dim) {
    TT_LLK_DUMP("llk_unpack_tilize({}, {}, {})", operand, tile_index, block_ct_dim);

    std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(operand_id);
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);
    const bool narrow_tile = get_operand_narrow_tile(operand_id);

    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr - 1;  // Remove header size added by descriptor

    _llk_unpack_tilize_(
        base_address, 
        tile_index,
        unpack_src_format[operand_id], 
        block_ct_dim, 
        face_r_dim, 
        num_faces, 
        narrow_tile
    );
}

/*************************************************************************
* LLK UNPACK UNTILIZE  
*************************************************************************/ 
template <bool is_fp32_dest_acc_en = false>
inline void llk_unpack_untilize_hw_configure(const llk_unpack_A_params_t *unpack_untilize_params) {
    constexpr bool is_row_pool = false;
    constexpr bool within_face_16x16_transpose = false;
    constexpr StochRndType stoch_rnd_mode = StochRndType::None;

    const uint32_t unpA_operand_id = get_operand_id(unpack_untilize_params->unpA_operand);
    const uint32_t unpA_num_faces = 4;
    const uint32_t unpA_face_r_dim = FACE_R_DIM;

    _llk_unpack_untilize_hw_configure_<is_fp32_dest_acc_en, stoch_rnd_mode>(
        unpack_src_format[unpA_operand_id], 
        unpack_dst_format[unpA_operand_id], 
        unpA_face_r_dim, 
        within_face_16x16_transpose, 
        unpA_num_faces
    );
}

inline void llk_unpack_untilize_hw_configure_disaggregated(const std::uint32_t unpA_operand) {
    const llk_unpack_A_params_t unpack_untilize_params = {
        .unpA_operand = unpA_operand,
    };
    llk_unpack_untilize_hw_configure(&unpack_untilize_params);
}

inline void llk_unpack_untilize_mop_config() {
    _llk_unpack_untilize_mop_config_();
}

inline void llk_unpack_untilize_init(std::uint32_t operand = 0) { 
    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t face_r_dim = 1;
    const std::uint32_t num_faces = get_operand_num_faces(operand_id);

    // Save state of unpacker config for quick restore
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_0, UNP0_ADDR_CTRL_XY_REG_1_Ystride_ADDR32); // Save unpack stride config
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_1, THCON_SEC0_REG5_Tile_x_dim_cntx0_ADDR32); // Save tile x dim per context
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_2, THCON_SEC0_REG0_TileDescriptor_ADDR32+1); // Save descriptor 1

    _llk_unpack_untilize_init_(
        unpack_dst_format[operand_id],
        operands[operand_id].f.tile_size_words,
        face_r_dim,
        num_faces
    );
}

inline void llk_unpack_untilize_uninit(const std::uint32_t operand, const std::uint32_t face_r_dim = FACE_R_DIM) {
    unpacker_addr_counter_init();
    TT_SETADCXX(p_setadc::UNP_A, face_r_dim*FACE_C_DIM-1, 0x0);
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG5_Tile_x_dim_cntx0_ADDR32-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_1); // Restore tile x dim per context
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG0_TileDescriptor_ADDR32+1-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_2); // Restore descriptor 1
    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::UNPACK);
    TTI_WRCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_0, p_cfg::WRCFG_32b, UNP0_ADDR_CTRL_XY_REG_1_Ystride_ADDR32);
    TTI_NOP; TTI_NOP;
}

template <bool first_pass = true>
inline void llk_unpack_untilize_pass(std::uint32_t operand, std::uint32_t block_tile_cols) {
    const std::uint32_t operand_id = get_operand_id(operand);
    const std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;

    _llk_unpack_untilize_pass_<first_pass>(
        base_address, 
        block_tile_cols 
    );
}

inline void llk_unpack_untilize(std::uint32_t operand, std::uint32_t block_c_tiles) {
    llk_unpack_untilize_pass<true>(operand, block_c_tiles);
    llk_unpack_untilize_pass<false>(operand, block_c_tiles);
}

/*************************************************************************
* LLK UNPACK COMMON  
*************************************************************************/ 

void llk_zero_operand(std::uint32_t operand) {
    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t fifo_base_addr = (operands[operand_id].f.fifo_limit + 1) - operands[operand_id].f.fifo_size;
    std::uint32_t size = operands[operand_id].f.fifo_size;
    _llk_zero_buffer_(fifo_base_addr, size);
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_unpack_get_tile(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t *p_tile) {
    TT_LLK_DUMP("llk_unpack_get_tile<{}, {}>({}, {}, tile_pointer)", mail2math, mail2pack, operand, tile_index);
    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;
    std::uint32_t offset_address = operands[operand_id].f.tile_size_words * tile_index;
    std::uint32_t address = base_address + offset_address + TILE_HEADER_SIZE;
    _llk_unpack_get_tile_<mail2math, mail2pack>(address, p_tile);
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_unpack_release_tile(std::uint32_t operand) {
    TT_LLK_DUMP("llk_unpack_release_tile<{}, {}>({})", mail2math, mail2pack, operand);
    _llk_unpack_release_tile_<mail2math, mail2pack>();
}

inline void llk_unpack_debug_dump(std::uint8_t *data, std::uint32_t byte_size) {
    TT_LLK_DUMP("llk_unpack_debug_dump(ptr, {})", byte_size);
    _llk_unpack_debug_dump_(data, byte_size);
}

inline void llk_unpack_debug_dump_seek(std::uint8_t offset) {
    _llk_unpack_debug_dump_seek_(offset);
}

inline void llk_unpack_config_tile_dim_srca(const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_unpack_config_tile_dim_srca({})", srca_new_operand);
    const std::uint32_t srca_operand_id = get_operand_id(srca_new_operand);
    const std::uint32_t num_faces = get_operand_num_faces(srca_operand_id);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(srca_operand_id);
    
    _llk_unpack_config_tile_dim_srca_impl_(face_r_dim, num_faces);
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format_srca(const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srca({})", srca_new_operand);
    const std::uint32_t srca_operand_id = get_operand_id(srca_new_operand);

    _llk_unpack_reconfig_data_format_srca_impl_(
        unpack_src_format[srca_operand_id], 
        unpack_dst_format[srca_operand_id], 
        operands[srca_operand_id].f.tile_size_words
    );
}

inline void llk_unpack_config_tile_dim_srcb(const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_config_tile_dim_srcb({})", srcb_new_operand);
    std::uint32_t srcb_operand_id = get_operand_id(srcb_new_operand);
    const std::uint32_t num_faces = get_operand_num_faces(srcb_operand_id);
    const std::uint32_t face_r_dim = get_operand_face_r_dim(srcb_operand_id);
    
    _llk_unpack_config_tile_dim_srcb_impl_(face_r_dim, num_faces);
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format_srcb(const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srcb({})", srcb_new_operand);
    std::uint32_t srcb_operand_id = get_operand_id(srcb_new_operand);

    _llk_unpack_reconfig_data_format_srcb_impl_(
        unpack_src_format[srcb_operand_id], 
        unpack_dst_format[srcb_operand_id], 
        operands[srcb_operand_id].f.tile_size_words
    );
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format_srca(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srca({}, {})", srca_old_operand, srca_new_operand);
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);

    if constexpr (is_tile_dim_reconfig_en) {
        if((get_operand_num_faces(new_srca_operand_id) != get_operand_num_faces(old_srca_operand_id)) ||
            (get_operand_face_r_dim(new_srca_operand_id) != get_operand_face_r_dim(old_srca_operand_id))) {
            llk_unpack_config_tile_dim_srca(
                srca_new_operand
            );
        }
    }

    if (unpack_src_format[old_srca_operand_id] != unpack_src_format[new_srca_operand_id]) {
        llk_unpack_reconfig_data_format_srca(
            srca_new_operand
        );
    }
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format_srcb(const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srcb({}, {})", srcb_old_operand, srcb_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if constexpr (is_tile_dim_reconfig_en) {
        if((get_operand_num_faces(new_srcb_operand_id) != get_operand_num_faces(old_srcb_operand_id)) ||
            (get_operand_face_r_dim(new_srcb_operand_id) != get_operand_face_r_dim(old_srcb_operand_id))) {
            llk_unpack_config_tile_dim_srcb(
                srcb_new_operand
            );
        }
    }

    if (unpack_src_format[old_srcb_operand_id] != unpack_src_format[new_srcb_operand_id]) {
        llk_unpack_reconfig_data_format_srcb(
            srcb_new_operand
        );
    }
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format(const std::uint32_t srca_new_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format({}, {})", srca_new_operand, srcb_new_operand);
    llk_unpack_reconfig_data_format_srca<is_tile_dim_reconfig_en>(srca_new_operand);
    llk_unpack_reconfig_data_format_srcb<is_tile_dim_reconfig_en>(srcb_new_operand);
}

template <bool is_tile_dim_reconfig_en = false>
inline void llk_unpack_reconfig_data_format(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand, const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format({}, {}, {}, {})", srca_old_operand, srca_new_operand, srcb_old_operand, srcb_new_operand);
    llk_unpack_reconfig_data_format_srca<is_tile_dim_reconfig_en>(srca_old_operand, srca_new_operand);
    llk_unpack_reconfig_data_format_srcb<is_tile_dim_reconfig_en>(srcb_old_operand, srcb_new_operand);
}

inline void llk_unpack_dbg_feature_disable(){
    _llk_unpack_dbg_feature_disable_();
}

inline void llk_enable_int8_fpu_math() {
    _llk_enable_int8_fpu_math_();
}