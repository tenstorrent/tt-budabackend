// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "ckernel_include.h"
#include "ckernel_template.h"

#include "cmath_common.h"
#include "llk_math_common.h"

#ifndef HF
#define HF 0
#endif

using namespace ckernel;

// local function declarations
inline void matmul_configure_addrmod();
inline void matmul_configure_mop();

template <int MATH_FIDELITY_PHASES, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void _llk_math_matmul_(uint dst_index, bool transpose = false) {

    math::set_dst_write_addr<DstTileLayout::Default, DstTileShape::Tile32x32>(dst_index);
    if constexpr (FaceLayout == DstTileFaceLayout::ColMajor) {
        if constexpr (MATH_FIDELITY_PHASES > 0) {
            ckernel_template::run(instrn_buffer);
            ckernel_template::run(instrn_buffer);
            TTI_SETRWC(p_setrwc::CLR_B, 0, 0, 0, 0, p_setrwc::SET_D);
            ckernel_template::run(instrn_buffer);
            ckernel_template::run(instrn_buffer);  // Run 4 times
            TTI_SETRWC(p_setrwc::CLR_B, 0, 0, 0, 0, p_setrwc::SET_D);
        } else {
            ckernel_template::run(instrn_buffer);
            ckernel_template::run(instrn_buffer);  // Run mop twice
        }
    } else {
        if constexpr (MATH_FIDELITY_PHASES > 0) {
            for (int j=0; j<2; j++) {
                if (transpose) TTI_TRNSPSRCA;
                for (int i=0; i<MATH_FIDELITY_PHASES; i++) {
                    ckernel_template::run(instrn_buffer);
                }
                TTI_SETRWC(p_setrwc::CLR_A, p_setrwc::CR_D, 8, 0, 0, p_setrwc::SET_D);          // DEST = 8, DEST_CR = 8
                TTI_SETRWC(p_setrwc::CLR_NONE, p_setrwc::CR_D, 8, 0, 0, p_setrwc::SET_D_F);     // DEST = 16, DEST_CR = 16, FIDELITY_PHASE = 0
                if (transpose) TTI_TRNSPSRCA;
                for (int i=0; i<MATH_FIDELITY_PHASES; i++) {
                    ckernel_template::run(instrn_buffer);
                }
                TTI_SETRWC(p_setrwc::CLR_AB, 0, 0, 0, 0, p_setrwc::SET_D_F);                      // DEST = 0, DEST_CR = 0, FIDELITY_PHASE = 0
            }
        } else {
            if (transpose) TTI_TRNSPSRCA;
            ckernel_template::run(instrn_buffer);
            if (transpose) TTI_TRNSPSRCA;
            ckernel_template::run(instrn_buffer);
            TTI_SETRWC(p_setrwc::CLR_B, 0, 0, 0, 0, p_setrwc::SET_D);
            if (transpose) TTI_TRNSPSRCA;
            ckernel_template::run(instrn_buffer);
            if (transpose) TTI_TRNSPSRCA;
            ckernel_template::run(instrn_buffer);
            TTI_SETRWC(p_setrwc::CLR_B, 0, 0, 0, 0, p_setrwc::SET_D);
        }
    }
}

template <int MATH_FIDELITY_DESC, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void _llk_math_matmul_block_(
    uint dst_index,
    bool transpose = false,
    const std::uint32_t ct_dim=1,
    const std::uint32_t rt_dim=1,
    const std::uint32_t kt_dim=1) {

    constexpr int MATH_FIDELITY_PHASES = get_math_num_fidelity_phases(MATH_FIDELITY_DESC);

    for (std::uint32_t rt=0; rt<rt_dim; rt++) {
        for (std::uint32_t ct=0; ct<ct_dim; ct++) {
            _llk_math_matmul_<MATH_FIDELITY_PHASES, FaceLayout>(dst_index+rt*ct_dim+ct, transpose);
        }
    }
}


template <int MATH_FIDELITY_DESC, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void matmul_configure_addrmod() {
    // MVMUL does D = B*A

    // Inner Loop --> 8 times for the full 16x32 face
    // DEST -- 4 rows are calculated each time
    // SRCB -- 4 rows are needed
    // SRCA -- full 16x16 gets used -- hardware will pair cols of A with rows of B
    // D[4,16] = B[4,16] * A[16,16]

    constexpr int MATH_FIDELITY_PHASES = get_math_num_fidelity_phases(MATH_FIDELITY_DESC);
    constexpr int MATH_FIDELITY_INCREMENT = get_math_fidelity_increment(MATH_FIDELITY_DESC);

    addr_mod_t{
        .srca = {.incr = 0, .clr = 0, .cr = 0},
        .srcb = {.incr = 4, .clr = 0, .cr = 0},
        .dest = {.incr = 4, .clr = 0, .cr = 0},
    }
    .set(ADDR_MOD_0);

    if constexpr (FaceLayout == DstTileFaceLayout::ColMajor) {

        if constexpr (MATH_FIDELITY_PHASES > 0) {
            // Fidelity Loop
            // DEST -- CR on dest for next fidelity phase
            // SRCB -- Go back to beginning of srcB + Clear B Dvalid
            // SRCA -- Clear A Dvalid
            // Fidelity -- increment phase
            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 0, .clr = 1, .cr = 0},
                .dest = {.incr = 0, .clr = 0, .cr = 1},
                .fidelity = {.incr = MATH_FIDELITY_INCREMENT, .clr = 0}}
                .set(ADDR_MOD_1);
        } else {
            // Last outer loop,
            // DEST -- CR on dest for next fidelity phase
            // SRCB -- Go back to beginning of srcB + Clear B Dvalid
            // SRCA -- Clear A Dvalid
            // Fidelity -- increment phase
            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 0, .clr = 1, .cr = 0},
                .dest = {.incr = 0, .clr = 1, .cr = 1},
                .fidelity = {.incr = 0, .clr = 0}}
                .set(ADDR_MOD_1);

        }

        // Last inner loop,
        // DEST -- keep incrementing
        // SRCB -- Go back to beginning of srcB
        // SRCA -- Clear A Dvalid
        // Fidelity -- reset fidelity
        addr_mod_t{
            .srca = {.incr = 0, .clr = 1, .cr = 0},
            .srcb = {.incr = 0, .clr = 1, .cr = 0},
            .dest = {.incr = 32, .clr = 0, .cr = 1},
            .fidelity = {.incr = 0, .clr = 1}}
            .set(ADDR_MOD_2);

        if constexpr (MATH_FIDELITY_PHASES == 0) {
         // Last outer loop,
         // DEST -- CR on dest for next fidelity phase
         // SRCB -- Go back to beginning of srcB + Clear B Dvalid
         // SRCA -- Clear A Dvalid
         // Fidelity -- increment phase
         addr_mod_t{
             .srca = {.incr = 0, .clr = 1, .cr = 0},
             .srcb = {.incr = 0, .clr = 1, .cr = 0},
             .dest = {.incr = 0, .clr = 1, .cr = 1},
             .fidelity = {.incr = 0, .clr = 0}}
             .set(ADDR_MOD_3);
        }
    } else {

        if constexpr (MATH_FIDELITY_PHASES > 0) {

            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 4, .clr = 0, .cr = 0},
                .dest = {.incr = 20, .clr = 0, .cr = 0},
                .fidelity = {.incr = 0, .clr = 0}}
                .set(ADDR_MOD_1);

            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 0, .clr = 1, .cr = 0},
                .dest = {.incr = 0, .clr = 0, .cr = 1},
                .fidelity = {.incr = MATH_FIDELITY_INCREMENT, .clr = 0}}
                .set(ADDR_MOD_2);


        } else {

            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 4, .clr = 0, .cr = 0},
                .dest = {.incr = 20, .clr = 0, .cr = 0},
                .fidelity = {.incr = 0, .clr = 0}}
                .set(ADDR_MOD_1);

            addr_mod_t{
                .srca = {.incr = 0, .clr = 1, .cr = 0},
                .srcb = {.incr = 0, .clr = 1, .cr = 0},
                .dest = {.incr = 16, .clr = 0, .cr = 1},
                .fidelity = {.incr = 0, .clr = 0}}
                .set(ADDR_MOD_2);


        }
    }
}

template <int NUM_FIDELITY_PHASES, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void matmul_configure_mop(bool transpose) {
    if constexpr (FaceLayout == DstTileFaceLayout::ColMajor) {
        if constexpr (NUM_FIDELITY_PHASES > 0) {
            ckernel_template tmp(NUM_FIDELITY_PHASES, 8, TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_0, 0));
            if (transpose) {
                tmp.set_start_op(TT_OP_TRNSPSRCA);
            }
            tmp.set_last_inner_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_1, 0));
            tmp.set_last_outer_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_A, 0, ADDR_MOD_2, 0));
            tmp.program(instrn_buffer);
        } else {
            // 4x16x16 * 2x32x16

            ckernel_template tmp(2, 8, TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_0, 0));
            if (transpose) {
                tmp.set_start_op(TT_OP_TRNSPSRCA);
            }
            tmp.set_last_inner_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_A, 0, ADDR_MOD_2, 0));
            tmp.set_last_outer_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_AB, 0, ADDR_MOD_3, 0));
            tmp.program(instrn_buffer);
        }
    } else {
        if constexpr (NUM_FIDELITY_PHASES > 0) {
            ckernel_template tmp(2, 4, TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_0, 0));
            tmp.set_last_inner_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_1, 0));
            tmp.set_last_outer_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_2, 0));
            tmp.program(instrn_buffer);
        } else {
            ckernel_template tmp(2, 4, TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_0, 0));
            tmp.set_last_inner_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_NONE, 0, ADDR_MOD_1, 0));
            tmp.set_last_outer_loop_instr(TT_OP_MVMUL(p_setrwc::CLR_A, 0, ADDR_MOD_2, 0));
            tmp.program(instrn_buffer);
        }
    }
}

template <int MATH_FIDELITY_DESC, DstTileFaceLayout FaceLayout=DstTileFaceLayout::ColMajor>
inline void _llk_math_matmul_init_(const std::uint32_t transpose=0, const std::uint32_t ct_dim=0, const std::uint32_t rt_dim=0, const std::uint32_t kt_dim=0) {

    constexpr int MATH_FIDELITY_PHASES = get_math_num_fidelity_phases(MATH_FIDELITY_DESC);

    matmul_configure_addrmod<MATH_FIDELITY_DESC, FaceLayout>();
    matmul_configure_mop<MATH_FIDELITY_PHASES, FaceLayout>(transpose>0);
    math::reset_counters(p_setrwc::SET_ABD_F);
}
