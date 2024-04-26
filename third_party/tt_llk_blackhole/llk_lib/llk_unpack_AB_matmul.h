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

// transpose is unused, math is adjusted to take into account srca face layout when transpose=true
inline void _llk_unpack_AB_matmul_mop_config_(const bool transpose, const std::uint32_t ct_dim, const std::uint32_t rt_dim, const std::uint32_t kt_dim, const bool partial_face) {
    // in0 - loaded to SrcB
    // in1 - loaded to SrcA

    const bool reuse_a = ct_dim >= rt_dim;
    const std::uint32_t replay_buf_prog_len = (!reuse_a && partial_face) ? 16 : 12;
    const std::uint32_t replay_buf_run_len  = replay_buf_prog_len/2;

    if (reuse_a) {
        #if SKIP_UNP == 1
            load_replay_buf<0, 1>([] {
                TTI_NOP;
            });
        #else
            load_replay_buf(0, replay_buf_prog_len, false, 
                // Lambda function to set up replay buffer
                [] {
                    TTI_UNPACR(SrcA, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    TTI_RDCFG(p_gpr_unpack::TMP0, THCON_SEC0_REG3_Base_address_ADDR32);
                    TTI_ADDDMAREG(0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP0, p_gpr_unpack::TILE_SIZE_A);
                    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::THCON);
                    TTI_WRCFG(p_gpr_unpack::TMP0,0,THCON_SEC0_REG3_Base_address_ADDR32);
                    TTI_NOP;
                    TTI_UNPACR(SrcA, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    TTI_RDCFG(p_gpr_unpack::TMP0, THCON_SEC0_REG3_Base_cntx1_address_ADDR32);
                    TTI_ADDDMAREG(0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP0, p_gpr_unpack::TILE_SIZE_A);
                    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::THCON);
                    TTI_WRCFG(p_gpr_unpack::TMP0,0,THCON_SEC0_REG3_Base_cntx1_address_ADDR32);
                    TTI_NOP;
                }
            );
        #endif
    } else {
        #if SKIP_UNP == 1
            load_replay_buf<0, 1>([] {
                TTI_NOP;
            });
        #else
            load_replay_buf(0, replay_buf_prog_len, false, 
                // Lambda function to set up replay buffer
                [partial_face] {
                    if (partial_face) {
                        TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 0 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                        TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                        TTI_SETADCZW(p_setadc::UNP_B, 0, 0, 0, 0, 0b0101); // Set ch0_z=0, ch1_z=0
                    } else {
                        TTI_UNPACR(SrcB, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    }    
                    TTI_RDCFG(p_gpr_unpack::TMP0, THCON_SEC1_REG3_Base_address_ADDR32);
                    TTI_ADDDMAREG(0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP_LO);
                    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::THCON);
                    TTI_WRCFG(p_gpr_unpack::TMP0,0,THCON_SEC1_REG3_Base_address_ADDR32);
                    TTI_NOP;
                    if (partial_face) {
                        TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 0 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                        TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                        TTI_SETADCZW(p_setadc::UNP_B, 0, 0, 0, 0, 0b0101); // Set ch0_z=0, ch1_z=0
                    } else {
                        TTI_UNPACR(SrcB, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    }    
                    TTI_RDCFG(p_gpr_unpack::TMP0, THCON_SEC1_REG3_Base_cntx1_address_ADDR32);
                    TTI_ADDDMAREG(0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP0, p_gpr_unpack::TMP_LO);
                    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::THCON);
                    TTI_WRCFG(p_gpr_unpack::TMP0,0,THCON_SEC1_REG3_Base_cntx1_address_ADDR32);
                    TTI_NOP;
                }
            );
        #endif

    }

    ckernel_unpack_template tmp = ckernel_unpack_template(
        false,  // src B
        false,  // halo - just used for 4 unpacks
        TT_OP_REPLAY(0, replay_buf_run_len, 0, 0), // runs when context is 0
        0,
        0,
        0,
        TT_OP_REPLAY(replay_buf_run_len, replay_buf_run_len, 0, 0), // runs when context is 1
        0,
        0);

    tmp.program(instrn_buffer);


}

template<bool is_fp32_dest_acc_en = false, StochRndType stoch_rnd_mode = StochRndType::None>
inline void _llk_unpack_AB_matmul_hw_configure_(const std::uint32_t unpA_src_format, const std::uint32_t unpB_src_format, const std::uint32_t unpA_dst_format, const std::uint32_t unpB_dst_format,  const std::uint32_t unpA_face_r_dim = FACE_R_DIM, const std::uint32_t unpB_face_r_dim = FACE_R_DIM, const std::uint32_t within_face_16x16_transpose = 0, const std::uint32_t unpA_num_faces = 4, const std::uint32_t unpB_num_faces = 4, const std::uint32_t unpA_tile_size = 0, const std::uint32_t unpB_tile_size = 0) {

    constexpr bool is_row_pool = false;
    constexpr bool stoch_rnd_en = (stoch_rnd_mode == StochRndType::All);
    constexpr bool fpu_srnd_en = stoch_rnd_en || (stoch_rnd_mode == StochRndType::Fpu);
    constexpr bool pack_srnd_en = stoch_rnd_en ||(stoch_rnd_mode == StochRndType::Pack);

    configure_unpack_AB<is_row_pool, is_fp32_dest_acc_en, fpu_srnd_en, pack_srnd_en>(
        unpA_src_format,
        unpB_src_format,
        unpA_dst_format,
        unpB_dst_format,
        unpA_face_r_dim,
        unpB_face_r_dim,
        within_face_16x16_transpose,
        unpA_num_faces,
        unpB_num_faces);

    // Configure tile size in datums
    const uint32_t unpA_x_end = unpA_num_faces*unpA_face_r_dim*FACE_C_DIM-1;
    const uint32_t unpB_x_end = unpB_num_faces*unpB_face_r_dim*FACE_C_DIM-1;
    TT_SETADCXX(p_setadc::UNP_A, unpA_x_end, 0x0);
    TT_SETADCXX(p_setadc::UNP_B, unpB_x_end, 0x0);

    regfile[p_gpr_unpack::TILE_SIZE_A] = unpA_tile_size;
    regfile[p_gpr_unpack::TILE_SIZE_B] = unpB_tile_size;
    sync_regfile_write(p_gpr_unpack::TILE_SIZE_B);
}

__attribute__((always_inline)) inline void _llk_unpack_AB_matmul_init_(const std::uint32_t transpose=0, const std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1, const std::uint32_t unpA_face_r_dim=FACE_R_DIM, const std::uint32_t unpB_face_r_dim=FACE_R_DIM, const std::uint32_t unpA_num_faces=4, const std::uint32_t unpB_num_faces=4, const bool partial_face=false) {

    const bool reuse_a = ct_dim >= rt_dim;

    // also turn on within_face_16x16_transpose if it was turned off by datacopy at runtime
    // on WH, the unpacker performs both transpose of faces as well as transpose each face.
    // the former is configured in mop, the latter is configured in cfg register in hw_configure
    // in large matmul, datacopy will disable the transpose of faces, so we need it turn it back on for matmul.
    cfg_reg_rmw_tensix<THCON_SEC0_REG2_Haloize_mode_RMW>(transpose);

    TTI_SETADCZW(0b011, 0, 0, 0, 0, 0b1111);

    // Loading partial face into srcA is not supported
    const uint32_t unpA_x_end = unpA_num_faces*unpA_face_r_dim*FACE_C_DIM-1;
    TT_SETADCXX(p_setadc::UNP_A, unpA_x_end, 0x0);

    if (partial_face) {
        // Do face by face unpacking. Need to program correct face dim
        // to compute address of the next face
        config_unpacker_x_end<p_setadc::UNP_B>(unpB_face_r_dim);
    } else {
        // Do full tile unpacking. No need to program face dim
        // as address counter pointing to the face is not incremented
        const uint32_t unpB_x_end = unpB_num_faces*unpB_face_r_dim*FACE_C_DIM-1;
        TT_SETADCXX(p_setadc::UNP_B, unpB_x_end, 0x0);
    }

    TT_SETDMAREG(0, LOWER_HALFWORD(kt_dim), 0, LO_16(p_gpr_unpack::KT_DIM)); // store kt_dim to gpr for scaling tile size

    _llk_unpack_AB_matmul_mop_config_(transpose != 0, ct_dim, rt_dim, kt_dim, partial_face);
}

inline void _llk_unpack_AB_matmul_(
    const std::uint32_t base_address_a, const std::uint32_t base_address_b, const std::uint32_t tile_index_a, const std::uint32_t tile_index_b, const std::uint32_t tile_size_a, const std::uint32_t tile_size_b, const std::uint32_t unpA_face_r_dim=FACE_R_DIM, const std::uint32_t unpB_face_r_dim=FACE_R_DIM, const bool partial_face=false, std::uint32_t ct_dim=1, const std::uint32_t rt_dim=1, const std::uint32_t kt_dim=1) {
    // In0/InA -> srcB (supports partial face)
    // In1/InB -> srcA

    volatile uint *cfg = get_cfg_pointer();  // get pointer to registers for current state ID

    const bool reuse_a = ct_dim >= rt_dim;
    const std::uint32_t t_dim = reuse_a ? rt_dim : ct_dim;

    if (!reuse_a) {
        TTI_MULDMAREG(0, p_gpr_unpack::TMP_LO, p_gpr_unpack::TILE_SIZE_B, p_gpr_unpack::KT_DIM);
    }

    for (uint t = 0; t < t_dim; t++) {

        std::uint32_t offset_address_a =tile_size_a*(tile_index_a + (reuse_a ? (t*kt_dim) : (0)));
        std::uint32_t offset_address_b = tile_size_b*(tile_index_b + (reuse_a ? (0       ) : (t)));
        std::uint32_t address_a = base_address_a + offset_address_a;
        std::uint32_t address_b = base_address_b + offset_address_b;

        // Wait for free context
        wait_for_next_context(2);

        semaphore_post(semaphore::UNPACK_SYNC);  // Trisc::SEMPOST for context acquire

        // Program unpacker 1 base address
        if (0 == unp_cfg_context) {
            cfg[THCON_SEC0_REG3_Base_address_ADDR32] = address_b;
            cfg[THCON_SEC1_REG3_Base_address_ADDR32] = address_a;
        } else {
            cfg[THCON_SEC0_REG3_Base_cntx1_address_ADDR32] = address_b;
            cfg[THCON_SEC1_REG3_Base_cntx1_address_ADDR32] = address_a;
        }

        if (reuse_a) {
            #if SKIP_UNP == 1
                TTI_NOP;
            #else
                if (partial_face) {
                    // Do face by face unpacking
                    TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 0 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    TTI_UNPACR(SrcB, 0b00010001, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                    TTI_SETADCZW(p_setadc::UNP_B, 0, 0, 0, 0, 0b0101); // Set ch0_z=0, ch1_z=0
                } else {
                    TTI_UNPACR(SrcB, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
                }
            #endif
        } else {
            #if SKIP_UNP == 1
                TTI_NOP;
            #else
                TTI_UNPACR(SrcA, 0, 0, 0, 0, 1 /*Set OvrdThreadId*/, 1 /*Set Dvalid*/, p_unpacr::RAREFYB_DISABLE, 0, 0 /* Set ContextIdInc */, 0, 0, 1);
            #endif
        }


        TT_MOP(0, (reuse_a ? ct_dim : rt_dim) - 1, unp_cfg_context == 0 ? 0 : 0xff); // Run the MOP

        // T6::SEMGET for context release
        t6_semaphore_get(semaphore::UNPACK_SYNC);

        // Switch unpacker config context
        switch_config_context(unp_cfg_context);
    }
    

    #ifdef PERF_DUMP
        first_unpack_recorded = true;
    #endif

}
