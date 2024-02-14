// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

//#include "kernel_types.h"
#include "ckernel.h"
#include "ckernel_noc.h"
#include "ckernel_template.h"
#include "ckernel_sfpu.h"
#include "ckernel_globals.h"

#include "fw_debug.h"

#ifndef SFPU_OP_PARAM
#define SFPU_OP_PARAM 0
#endif

#ifndef FUSE_SQRT_RECIP
#define FUSE_SQRT_RECIP 0
#endif

using namespace ckernel;

namespace ckernel::math
{

constexpr uint DstTileSize[3] = {
    64,     // 32x32 tile shape
    32,     // 32x16, 16x32 tile shape
    16      // 16x16 tile shape
};
constexpr uint DstTileSizeLog2[3] = {
    6,     // 32x32 tile shape
    5,     // 32x16, 16x32 tile shape
    4      // 16x16 tile shape
};


inline void reset_counters(const uint setrwc)
{
    TTI_SETRWC(p_setrwc::CLR_NONE, 0, 0, 0, 0, setrwc);
}

inline void incr_counters(const uint incr_a, const uint incr_b, const uint incr_d, const uint incr_cr)
{
    FWASSERT("Value exceeds RWC_A width of 4 bits", incr_a < 16);
    FWASSERT("Value exceeds RWC_B width of 4 bits", incr_b < 16);
    FWASSERT("Value exceeds RWC_D width of 4 bits", incr_d < 16);
    FWASSERT("Value exceeds RWC_CR width of 4 bits", incr_cr < 16);
    TT_INCRWC(incr_cr, incr_d, incr_b, incr_a);
}

inline void move_d2a_fixed_face(const uint8_t addrmod)
{
    TTI_STALLWAIT(p_stall::STALL_MATH, p_stall::SRCA_VLD); // MOVD2A for a whole face assumes unpacker will set a dummy data_valid, so we want to wait on that
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  0, addrmod, p_movd2a::MOV_4_ROWS,  0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  4, addrmod, p_movd2a::MOV_4_ROWS,  4);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  8, addrmod, p_movd2a::MOV_4_ROWS,  8);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 12, addrmod, p_movd2a::MOV_4_ROWS, 12);
}

inline void move_d2b_fixed_face(const uint8_t addrmod)
{
    TTI_STALLWAIT(p_stall::STALL_MATH, p_stall::SRCB_VLD); // MOVD2B for a whole face assumes unpacker will set a dummy data_valid, so we want to wait on that
    TTI_MOVD2B(0, p_movd2b::SRC_ZERO_OFFSET +  0, addrmod, p_movd2b::MOV_4_ROWS,  0);
    TTI_MOVD2B(0, p_movd2b::SRC_ZERO_OFFSET +  4, addrmod, p_movd2b::MOV_4_ROWS,  4);
    TTI_MOVD2B(0, p_movd2b::SRC_ZERO_OFFSET +  8, addrmod, p_movd2b::MOV_4_ROWS,  8);
    TTI_MOVD2B(0, p_movd2b::SRC_ZERO_OFFSET + 12, addrmod, p_movd2b::MOV_4_ROWS, 12);
}

inline void move_d2a_row_broadcast_fixed_face(const uint8_t addrmod)
{

    // // Seems to make things 200 clocks slower. Really shouldn't though.
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  0, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  1, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  2, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  3, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  4, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  5, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  6, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  7, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  8, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS +  9, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 10, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 11, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 12, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 13, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 14, addrmod, p_movd2a::MOV_1_ROW, 0);
    TTI_MOVD2A(0, p_mova2d::MATH_HALO_ROWS + 15, addrmod, p_movd2a::MOV_1_ROW, 0);

}

inline void move_a2d_fixed_face(const uint8_t addrmod)
{

    TTI_MOVA2D(0, p_mova2d::MATH_HALO_ROWS, addrmod, p_mova2d::MOV_8_ROWS, 0);
    TTI_MOVA2D(0, p_mova2d::MATH_HALO_ROWS, addrmod, p_mova2d::MOV_8_ROWS, 0);
}

template <uint SrcReg>
inline void wait_bank_valid()
{
    if constexpr (SrcReg == Srcs::SrcA)
    {
        TTI_STALLWAIT(p_stall::STALL_MATH, p_stall::SRCA_VLD);
    }
    else
    {
        TTI_STALLWAIT(p_stall::STALL_MATH, p_stall::SRCB_VLD);
    }
}

template <uint SrcReg>
inline void clear_bank_valid()
{
    if constexpr (SrcReg == Srcs::SrcA)
    {
        TTI_SETRWC(p_setrwc::CLR_A, 0, 0, 0, 0, p_setrwc::SET_A);
    }
    else
    {
        TTI_SETRWC(p_setrwc::CLR_B, 0, 0, 0, 0, p_setrwc::SET_B);
    }
}

inline void update_dest_offset_id()
{
    //ping-pong between 0 and 1
    dest_offset_id = 1 - dest_offset_id;
}

inline uint32_t get_dest_buffer_base()
{
    return (0 != dest_offset_id) ? DEST_REGISTER_HALF_SIZE : 0x0;
}

inline void wait_math_semaphores()
{
    // wait while math semaphore is on max, no room to write math results
    TTI_SEMWAIT(p_stall::STALL_MATH|p_stall::STALL_SFPU, semaphore::t6_sem(semaphore::MATH_PACK), p_stall::STALL_ON_MAX);
}

inline void set_math_semaphores()
{
    // Tell packer that it has something to pack
    t6_semaphore_post<p_stall::MATH|p_stall::WAIT_SFPU>(semaphore::MATH_PACK);
}

template <DstTileLayout layout, DstTileShape tile_shape>
inline void set_dst_write_addr(uint32_t tile_index)
{
    if constexpr (layout == DstTileLayout::Default) {
        uint dst_index = tile_index << DstTileSizeLog2[tile_shape];
        dst_index = dst_index + get_dest_buffer_base();
        TT_SETC16(DEST_TARGET_REG_CFG_MATH_Offset_ADDR32, dst_index);
    } else {
        // FIXME MT: add this mapping for other layout
    }
    
}

// Programming a dst write addr offset that gets added to base
//
inline void clear_dst_reg_addr()
{
    TTI_SETRWC(p_setrwc::CLR_NONE, 0, 0, 0, 0, p_setrwc::SET_D);
}

inline void math_dest_wait()
{
    FWLOG0("XX math_full_dest_sync()->wait for whole dest available");
    TTI_SEMWAIT(p_stall::STALL_MATH|p_stall::STALL_SFPU, semaphore::t6_sem(semaphore::MATH_PACK), p_stall::STALL_ON_MAX);
}

inline void dest_section_flip()
{
    update_dest_offset_id();
    uint base_addr = get_dest_buffer_base();
    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::MATH|p_stall::SFPU1);
    TT_SETC16(DEST_TARGET_REG_CFG_MATH_Offset_ADDR32, base_addr);
    // TTI_NOP;TTI_NOP;
}

template <DstStart Dst>
inline void set_dest_section_base()
{
    uint base_addr;
    if constexpr (Dst == DstStart::StartZero) {
        base_addr = 0;
    } else {
        base_addr = DEST_REGISTER_HALF_SIZE;
    }
    TT_SETC16(DEST_TARGET_REG_CFG_MATH_Offset_ADDR32, base_addr);
}

inline uint32_t get_operand_id(uint32_t operand) 
{
    const int INTERMEDIATE_BASE_ID = 24;
    const int OPERAND_BASE_ID = 0;
    return (operand>=INTERMEDIATE_BASE_ID) ? operand - 8 : operand - OPERAND_BASE_ID;
}

} // namespace ckernel::math
