// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ckernel.h"
#include "ckernel_defs.h"
#include "ckernel_template.h"
#include "cunpack_common.h"
#include "ckernel_globals.h"

using namespace ckernel;
using namespace ckernel::unpacker;

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

inline void llk_unpack_A_hw_configure(const llk_unpack_A_params_t *unpack_A_params, const int transpose_xy = 0) {
    const uint32_t operand_id = get_operand_id(unpack_A_params->unpA_operand);

    _llk_unpack_A_hw_configure_(
        unpack_src_format[operand_id],
        unpack_dst_format[operand_id],
        transpose_xy
    );
}

template <bool is_fp32_dest_acc_en = false /* unused */, StochRndType stoch_rnd_mode = StochRndType::None /* unused */>
inline void llk_unpack_A_hw_configure_disaggregated(const std::uint32_t unpA_operand, const int within_face_16x16_transpose) {
    TT_LLK_DUMP("llk_unpack_A_hw_configure_disaggregated<{}, {}>({}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, within_face_16x16_transpose);
    const llk_unpack_A_params_t unpack_A_params = {
        .unpA_operand = unpA_operand,
    };
    llk_unpack_A_hw_configure(&unpack_A_params, within_face_16x16_transpose);
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false>
inline void llk_unpack_A_mop_config(const bool transpose_of_faces) {
    _llk_unpack_A_mop_config_<BType, acc_to_dest>(transpose_of_faces);
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false, EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE>
inline void llk_unpack_A_init(const std::uint32_t transpose_of_faces=0, const std::uint32_t within_face_16x16_transpose=0, const std::uint32_t operand = 255) {
    TT_LLK_DUMP("llk_unpack_A_init<{}, {}, {}>({}, {}, {})", BType, acc_to_dest, binary_reuse_dest, transpose_of_faces, within_face_16x16_transpose, operand);   
    
    // If passed in operand is default (255), it means that it has not been passed by llk, and we should assume default tile dims.
    _llk_unpack_A_init_<BType, acc_to_dest, binary_reuse_dest>(
        transpose_of_faces,
        within_face_16x16_transpose
    );
}

template <BroadcastType BType = BroadcastType::NONE, bool acc_to_dest = false, EltwiseBinaryReuseDestType binary_reuse_dest = EltwiseBinaryReuseDestType::NONE>
inline void llk_unpack_A(const std::uint32_t operand, const std::uint32_t tile_index, const int transpose_of_faces = 0) {
    TT_LLK_DUMP("llk_unpack_A<{}, {}, {}>({}, {}, {})", BType, acc_to_dest, binary_reuse_dest, operand, tile_index, transpose_of_faces);
    
    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;
    std::uint32_t offset_address = MUL_TILE_SIZE_AND_INDEX((uint)unpack_src_format[operand_id], tile_index);
    std::uint32_t address = base_address + offset_address;

    _llk_unpack_A_<BType, acc_to_dest, binary_reuse_dest>(address, transpose_of_faces);
}

/*************************************************************************
* LLK UNPACK AB 
*************************************************************************/

inline void llk_unpack_AB_hw_configure(const llk_unpack_AB_params_t *unpack_AB_params) {
    const uint32_t unpA_operand_id = get_operand_id(unpack_AB_params->unpA_operand);
    const uint32_t unpB_operand_id = get_operand_id(unpack_AB_params->unpB_operand);

    _llk_unpack_AB_hw_configure_(
        unpack_src_format[unpA_operand_id],
        unpack_src_format[unpB_operand_id],
        unpack_dst_format[unpA_operand_id],
        unpack_dst_format[unpB_operand_id]
    );
}

template <bool is_fp32_dest_acc_en = false /* unused */, StochRndType stoch_rnd_mode = StochRndType::None /* unused */>
inline void llk_unpack_AB_hw_configure_disaggregated(
    const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const int within_face_16x16_transpose = 0 /*not used*/) {
    TT_LLK_DUMP("llk_unpack_AB_hw_configure_disaggregated<{}, {}>({}, {}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, unpB_operand, within_face_16x16_transpose);
    const llk_unpack_AB_params_t unpack_AB_params = {
        .unpA_operand = unpA_operand,
        .unpB_operand = unpB_operand
    };
    llk_unpack_AB_hw_configure(&unpack_AB_params);
}

template <BroadcastType BType = BroadcastType::NONE>
inline void llk_unpack_AB_mop_config() {
    _llk_unpack_AB_mop_config_<BType>();
}

template <BroadcastType BType = BroadcastType::NONE>
// within_face_16x16_transpose is used on WH but not used for GS, this transpose is done in math on GS
inline void llk_unpack_AB_init(const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const std::uint32_t transpose=0, const std::uint32_t acc_to_dest=0) {
    TT_LLK_DUMP("llk_unpack_AB_init<{}>({}, {}, {}, {})", BType, unpA_operand, unpB_operand, transpose, acc_to_dest);
    _llk_unpack_AB_init_<BType>(transpose, acc_to_dest);
}

template <BroadcastType BType = BroadcastType::NONE>
inline void llk_unpack_AB(
    const std::uint32_t operandA, const std::uint32_t operandB,
    const std::uint32_t tile_index_a, const std::uint32_t tile_index_b, const bool transpose_of_faces = 0 /*not used*/) {
    TT_LLK_DUMP("llk_unpack_AB<{}>({}, {}, {}, {}, {}, {})", BType, operandA, operandB, tile_index_a, tile_index_b, transpose_of_faces);
    
    std::uint32_t inputA = get_operand_id(operandA);
    std::uint32_t inputB = get_operand_id(operandB);
    std::uint32_t base_address_a = operands[inputA].f.fifo_rd_ptr;
    std::uint32_t offset_address_a = MUL_TILE_SIZE_AND_INDEX((uint)unpack_src_format[inputA], tile_index_a);
    std::uint32_t address_a = base_address_a + offset_address_a;
    std::uint32_t base_address_b = operands[inputB].f.fifo_rd_ptr;
    std::uint32_t offset_address_b = MUL_TILE_SIZE_AND_INDEX((uint)unpack_src_format[inputB], tile_index_b);
    std::uint32_t address_b = base_address_b + offset_address_b;

    _llk_unpack_AB_<BType>(
        address_a,
        address_b,
        transpose_of_faces
    );
}

/*************************************************************************
* LLK UNPACK AB MATMUL 
*************************************************************************/

inline void llk_unpack_AB_matmul_hw_configure(const llk_unpack_AB_matmul_params_t *unpack_AB_params) {
    const uint32_t unpA_operand_id = get_operand_id(unpack_AB_params->unpB_operand);
    const uint32_t unpB_operand_id = get_operand_id(unpack_AB_params->unpA_operand);
    
    _llk_unpack_AB_matmul_hw_configure_(
        unpack_src_format[unpA_operand_id],
        unpack_src_format[unpB_operand_id],
        unpack_dst_format[unpA_operand_id],
        unpack_dst_format[unpB_operand_id]
    );
}

template<bool is_fp32_dest_acc_en = false /* unused */, StochRndType stoch_rnd_mode = StochRndType::None /* unused */>
inline void llk_unpack_AB_matmul_hw_configure_disaggregated(
    const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const std::uint32_t transpose_xy_srca = 0) {
    TT_LLK_DUMP("llk_unpack_AB_matmul_hw_configure_disaggregated<{}, {}>({}, {}, {})", is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, unpB_operand, transpose_xy_srca);
    const llk_unpack_AB_matmul_params_t unpack_AB_matmul_params = {
        .unpA_operand = unpA_operand,
        .unpB_operand = unpB_operand,
        .transpose_xy_srca = transpose_xy_srca
    };
    llk_unpack_AB_matmul_hw_configure(&unpack_AB_matmul_params);
}

inline void llk_unpack_AB_matmul_mop_config(const bool transpose) {
    _llk_unpack_AB_matmul_mop_config_(transpose);
}

inline void llk_unpack_AB_matmul_init(const std::uint32_t unpA_operand, const std::uint32_t unpB_operand, const std::uint32_t transpose=0, const std::uint32_t ct_dim=0, const std::uint32_t rt_dim=0, const std::uint32_t kt_dim=0) {
    TT_LLK_DUMP("llk_unpack_AB_matmul_init({}, {}, {}, {}, {}, {})", unpA_operand, unpB_operand, transpose, ct_dim, rt_dim, kt_dim);
    // TODO: figure out tile dims based on unpA and unpB operands
    _llk_unpack_AB_matmul_init_(transpose, ct_dim, rt_dim, kt_dim);
}

inline void llk_unpack_AB_matmul(
    const std::uint32_t operandA, const std::uint32_t operandB, const std::uint32_t tile_index_a,
    const std::uint32_t tile_index_b, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {
    TT_LLK_DUMP("llk_unpack_AB_matmul({}, {}, {}, {}, {}, {}, {})", operandA, operandB, tile_index_a, tile_index_b, ct_dim, rt_dim, kt_dim);

    std::uint32_t unpA_operand_id  = get_operand_id(operandA);
    std::uint32_t unpB_operand_id  = get_operand_id(operandB);
    std::uint32_t base_address_a = operands[unpA_operand_id].f.fifo_rd_ptr;
    std::uint32_t base_address_b = operands[unpB_operand_id].f.fifo_rd_ptr;
    std::uint32_t unpA_src_format = (std::uint32_t)unpack_src_format[unpA_operand_id];
    std::uint32_t unpB_src_format = (std::uint32_t)unpack_src_format[unpB_operand_id];

    for (std::uint32_t rt=0; rt<rt_dim; rt++) {
        std::uint32_t offset_address_a = MUL_TILE_SIZE_AND_INDEX(unpA_src_format, (tile_index_a + rt*kt_dim));
        std::uint32_t address_a = base_address_a + offset_address_a;

        for (std::uint32_t ct=0; ct<ct_dim; ct++) {

            std::uint32_t offset_address_b = MUL_TILE_SIZE_AND_INDEX(unpB_src_format, (tile_index_b+ct));
            std::uint32_t address_b = base_address_b + offset_address_b;

            _llk_unpack_AB_matmul_(
                address_a,
                address_b
            );
        }
    }
}

/*************************************************************************
* LLK UNPACK REDUCE 
*************************************************************************/

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce_hw_configure(
    const llk_unpack_reduce_params_t *unpack_reduce_params, const float const_mult) {
    const std::uint32_t unpA_operand_id = get_operand_id(unpack_reduce_params->unpA_operand);

    _llk_unpack_reduce_hw_configure_<type, dim>(
        unpack_src_format[unpA_operand_id],
        unpack_dst_format[unpA_operand_id]
    );

    if constexpr (type != PoolType::MAX) {
        union {
            float f;
            uint32_t u;
        } f2u = {.f = const_mult};

        for (uint i = 0; i < 16; i++) l1_buffer[i] = f2u.u;  // Load const into L1 buffer
    }
}

template <PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false /* unused */, StochRndType stoch_rnd_mode = StochRndType::None>
inline void llk_unpack_reduce_hw_configure_disaggregated(const std::uint32_t unpA_operand, const float mult) {
    TT_LLK_DUMP("llk_unpack_reduce_hw_configure_disaggregated<{}, {}, {}, {}>({}, {})", type, dim, is_fp32_dest_acc_en, (uint8_t)stoch_rnd_mode, unpA_operand, mult);
    const llk_unpack_reduce_params_t unpack_reduce_params = {.unpA_operand = unpA_operand};
    llk_unpack_reduce_hw_configure<type, dim>(&unpack_reduce_params, mult);
}

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce_mop_config() {
    _llk_unpack_reduce_mop_config_<type, dim>();
}

template <PoolType type, ReduceDim dim>
// within_face_16x16_transpose is used on WH but not used for GS, this transpose is done in math on GS
inline void llk_unpack_reduce_init(const std::uint32_t within_face_16x16_transpose=0) {
    TT_LLK_DUMP("llk_unpack_reduce_init<{}, {}>({})", type, dim, within_face_16x16_transpose);

    volatile uint tt_reg_ptr *cfg = get_cfg_pointer();  // get pointer to registers for current state ID

    constexpr std::uint32_t unpA_operand_id = 0;

    // Set first 32 bites of tile descriptor, only need data format change
    unpack_tile_descriptor_u tile_descriptor = {0};

    tile_descriptor.f.in_data_format  = (uint) DataFormat::Float32;
    tile_descriptor.f.uncompressed = 1; // Input tile is uncompressed
    tile_descriptor.f.x_dim        = 256; 

    unpack_config_u config = {0};

    config.f.out_data_format = (((uint)unpack_dst_format[unpA_operand_id]>>2)&0x1) ? (uint) DataFormat::Float16_b : (uint) DataFormat::Float16;
    config.f.throttle_mode = 2;

    wait_for_idle();

    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::UNPACK1);

    uint32_t alu_config_data = gl_alu_format_spec_reg;

    gl_alu_format_spec_reg = cfg_rmw_mmio_rd_tensix_wr(ALU_FORMAT_SPEC_REG_SrcA_val_ADDR32, ALU_FORMAT_SPEC_REG1_SrcB_SHAMT, ALU_FORMAT_SPEC_REG1_SrcB_MASK, 
                                                        config.f.out_data_format, 
                                                        alu_config_data);

    cfg[THCON_SEC1_REG0_TileDescriptor_ADDR32] = tile_descriptor.val[0];
    cfg[THCON_SEC1_REG2_Out_data_format_ADDR32] = config.val[0];

    cfg[THCON_SEC1_REG3_Base_address_ADDR32] = (((uint)l1_buffer) >> 4) - 1;        // Set l1 buffer address
    cfg[THCON_SEC1_REG3_Base_cntx1_address_ADDR32] = (((uint)l1_buffer) >> 4) - 1;  // Set l1 buffer address

    _llk_unpack_reduce_init_<type, dim>(within_face_16x16_transpose);
}

template <PoolType type, ReduceDim dim>
inline void llk_unpack_reduce(const std::uint32_t operand, const std::uint32_t tile_index) {
    TT_LLK_DUMP("llk_unpack_reduce<{}, {}>({}, {})", type, dim, operand, tile_index);
    
    std::uint32_t input = get_operand_id(operand);
    std::uint32_t base_address = operands[input].f.fifo_rd_ptr;
    std::uint32_t offset_address = MUL_TILE_SIZE_AND_INDEX((uint)unpack_src_format[input], tile_index);
    std::uint32_t address = base_address + offset_address;

    _llk_unpack_reduce_<type, dim>(address);
}

/*************************************************************************
* LLK UNPACK TILIZE 
*************************************************************************/

inline void llk_unpack_tilize_hw_configure(const llk_unpack_A_params_t *unpack_tilize_params) {
    const uint32_t unpA_operand_id = get_operand_id(unpack_tilize_params->unpA_operand);

    _llk_unpack_tilize_hw_configure_(
        unpack_src_format[unpA_operand_id],
        unpack_dst_format[unpA_operand_id]
    );
}

template <bool is_fp32_dest_acc_en = false /* unused */>
inline void llk_unpack_tilize_hw_configure_disaggregated(const std::uint32_t unpA_operand) {
    TT_LLK_DUMP("llk_unpack_tilize_hw_configure_disaggregated<{}>({})", is_fp32_dest_acc_en, unpA_operand);
    const llk_unpack_A_params_t unpack_tilize_params = {
        .unpA_operand = unpA_operand,
    };
    llk_unpack_tilize_hw_configure(&unpack_tilize_params);
}

inline void llk_unpack_tilize_mop_config() {
    _llk_unpack_tilize_mop_config_();
}

inline void llk_unpack_tilize_init(const std::uint32_t operand=0, const std::uint32_t ct_dim=0) {
    TT_LLK_DUMP("llk_unpack_tilize_init()");

    std::uint32_t input = get_operand_id(operand);
    std::uint32_t src_format = (std::uint32_t)unpack_src_format[input];
    std::uint32_t dst_format = (std::uint32_t)unpack_dst_format[input];

    _llk_unpack_tilize_init_(src_format, dst_format, ct_dim);
}

inline void llk_unpack_tilize_uninit(const std::uint32_t face_r_dim = FACE_R_DIM /* not used*/) {
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG2_Out_data_format_ADDR32+0-THCON_CFGREG_BASE_ADDR32, p_gpr_unpack::SR_UNPACK_TILIZER_STATE_0); // Restore unpack config[0]
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG5_Tile_x_dim_cntx0_ADDR32-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_TILIZER_STATE_1); // Restore tile x dim per context
}

inline void llk_unpack_tilize(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t block_ct_dim) {
    TT_LLK_DUMP("llk_unpack_tilize({}, {}, {})", operand, tile_index, block_ct_dim);
    std::uint32_t input = get_operand_id(operand);
    std::uint32_t base_address = operands[input].f.fifo_rd_ptr - 1;  // Remove header size added by descriptor
    std::uint32_t src_format = (uint)unpack_src_format[input];

    _llk_unpack_tilize_(
        base_address,
        tile_index,
        src_format,
        block_ct_dim
    );
}

/*************************************************************************
* LLK UNPACK UNTILIZE 
*************************************************************************/

inline void llk_unpack_untilize_hw_configure(const llk_unpack_untilize_params_t *unpack_untilize_params) {
    const uint32_t unpA_operand_id = get_operand_id(unpack_untilize_params->unpA_operand);
    
    _llk_unpack_untilize_hw_configure_(
        unpack_src_format[unpA_operand_id],
        unpack_dst_format[unpA_operand_id]
    );
}

inline void llk_unpack_untilize_hw_configure_disaggregated(const std::uint32_t unpA_operand) {
    const llk_unpack_untilize_params_t unpack_untilize_params = {
        .unpA_operand = unpA_operand,
    };
    llk_unpack_untilize_hw_configure(&unpack_untilize_params);
}

inline void llk_unpack_untilize_mop_config() {
    _llk_unpack_untilize_mop_config_();
}

inline void llk_unpack_untilize_init(const std::uint32_t operand) { 
    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t face_r_dim = 1;
    std::uint32_t src_format = (uint)unpack_src_format[operand_id];
    std::uint32_t dst_format = (uint)unpack_dst_format[operand_id];

    // Save state of unpacker config for quick restore
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_0, UNP0_ADDR_CTRL_XY_REG_1_Ystride_ADDR32); // Save unpack stride config
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_1, THCON_SEC0_REG0_TileDescriptor_ADDR32+0); // Save descriptor 0
    TTI_RDCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_2, THCON_SEC0_REG0_TileDescriptor_ADDR32+1); // Save descriptor 1

    _llk_unpack_untilize_init_(
        face_r_dim,
        src_format,
        dst_format,
        GET_L1_TILE_SIZE(src_format)
    );
}

inline void llk_unpack_untilize_uninit(const std::uint32_t operand, const std::uint32_t face_r_dim = FACE_R_DIM) {
    unpacker_addr_counter_init();
    TT_SETADCXX(p_setadc::UNP_A, face_r_dim*FACE_C_DIM-1, 0x0);
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG0_TileDescriptor_ADDR32+0-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_1); // Restore descriptor 0
    TTI_REG2FLOP(1,0,0,0,THCON_SEC0_REG0_TileDescriptor_ADDR32+1-THCON_CFGREG_BASE_ADDR32,  p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_2); // Restore descriptor 1
    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::UNPACK);
    TTI_WRCFG(p_gpr_unpack::SR_UNPACK_UNTILIZER_STATE_0, p_cfg::WRCFG_32b, UNP0_ADDR_CTRL_XY_REG_1_Ystride_ADDR32);
    TTI_WRCFG(p_gpr::ZERO, p_cfg::WRCFG_32b, UNP0_ADDR_BASE_REG_0_Base_ADDR32); // Clear base address register
    TTI_NOP; TTI_NOP;
}

template <bool first_pass = true>
inline void llk_unpack_untilize_pass(std::uint32_t operand, std::uint32_t block_tile_cols) {
    std::uint32_t operand_id = get_operand_id(operand);
    std::uint32_t base_address = operands[operand_id].f.fifo_rd_ptr;

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

inline void llk_zero_operand(std::uint32_t operand) {
    std::uint32_t input = get_operand_id(operand);
    std::uint32_t size = operands[input].f.fifo_size;
    std::uint32_t fifo_base_addr = (operands[input].f.fifo_limit + 1) - size;

    _llk_zero_operand_(fifo_base_addr, size);
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_unpack_get_tile(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t *p_tile) {
    TT_LLK_DUMP("llk_unpack_get_tile<{}, {}>({}, {}, tile_pointer)", mail2math, mail2pack, operand, tile_index);
    std::uint32_t input = get_operand_id(operand);
    std::uint32_t base_address = operands[input].f.fifo_rd_ptr;
    std::uint32_t offset_address = MUL_TILE_SIZE_AND_INDEX((uint)unpack_src_format[input], tile_index);
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

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format_srca(const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srca<{}>({})", is_tile_dim_reconfig_en, srca_new_operand);
    
    const std::uint32_t srca_operand_id = get_operand_id(srca_new_operand);

    _llk_unpack_reconfig_data_format_srca_impl_(
        unpack_src_format[srca_operand_id],
        unpack_dst_format[srca_operand_id]
    );
}

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format_srcb(const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srcb<{}>({})", is_tile_dim_reconfig_en, srcb_new_operand);

    const std::uint32_t srcb_operand_id = get_operand_id(srcb_new_operand);

    _llk_unpack_reconfig_data_format_srcb_impl_(
        unpack_src_format[srcb_operand_id],
        unpack_dst_format[srcb_operand_id]
    );
}

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format_srca(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srca<{}>({}, {})", is_tile_dim_reconfig_en, srca_old_operand, srca_new_operand);
    
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);

    if((unpack_src_format[old_srca_operand_id] != unpack_src_format[new_srca_operand_id])) {
        _llk_unpack_reconfig_data_format_srca_impl_(
            unpack_src_format[new_srca_operand_id],
            unpack_dst_format[new_srca_operand_id]
        );
    }
}

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format_srcb(const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format_srcb<{}>({}, {})", is_tile_dim_reconfig_en, srcb_old_operand, srcb_new_operand);
    
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if((unpack_src_format[old_srcb_operand_id] != unpack_src_format[new_srcb_operand_id])) {
        _llk_unpack_reconfig_data_format_srcb_impl_(
            unpack_src_format[new_srcb_operand_id],
            unpack_dst_format[new_srcb_operand_id]
        );
    }
}

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format(const std::uint32_t srca_new_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format<{}>({}, {})", is_tile_dim_reconfig_en, srca_new_operand, srcb_new_operand);
    
    const std::uint32_t srca_operand_id = get_operand_id(srca_new_operand);
    const std::uint32_t srcb_operand_id = get_operand_id(srcb_new_operand);
    
    _llk_unpack_reconfig_data_format_impl_(
        unpack_src_format[srca_operand_id],
        unpack_src_format[srcb_operand_id],
        unpack_dst_format[srca_operand_id],
        unpack_dst_format[srcb_operand_id]
    );
}

template <bool is_tile_dim_reconfig_en = false/* unused */>
inline void llk_unpack_reconfig_data_format(
    const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand,
    const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_unpack_reconfig_data_format<{}>({}, {}, {}, {})", is_tile_dim_reconfig_en, srca_old_operand, srca_new_operand, srcb_old_operand, srcb_new_operand);
    
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if ((unpack_src_format[old_srca_operand_id] != unpack_src_format[new_srca_operand_id]) &&
        (unpack_src_format[old_srcb_operand_id] != unpack_src_format[new_srcb_operand_id])) {
        _llk_unpack_reconfig_data_format_impl_(
            unpack_src_format[new_srca_operand_id],
            unpack_src_format[new_srcb_operand_id],
            unpack_dst_format[new_srca_operand_id],
            unpack_dst_format[new_srcb_operand_id]
        );
    } else if ((unpack_src_format[old_srca_operand_id] != unpack_src_format[new_srca_operand_id])) {
        _llk_unpack_reconfig_data_format_srca_impl_(
            unpack_src_format[new_srca_operand_id],
            unpack_dst_format[new_srca_operand_id]
        );
    } else if ((unpack_src_format[old_srcb_operand_id] != unpack_src_format[new_srcb_operand_id])) {
        _llk_unpack_reconfig_data_format_srcb_impl_(
            unpack_src_format[new_srcb_operand_id],
            unpack_dst_format[new_srcb_operand_id]
        );
    }
}

inline void llk_unpack_dbg_feature_disable(){
    _llk_unpack_dbg_feature_disable_();
}

