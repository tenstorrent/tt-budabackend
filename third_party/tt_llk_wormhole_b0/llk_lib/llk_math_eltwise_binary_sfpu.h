// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include "ckernel_include.h"
#include "ckernel_template.h"
#include <type_traits>

#include "cmath_common.h"
#include "llk_math_common.h"
#include "ckernel_globals.h"
#include "ckernel_sfpu.h"

using namespace ckernel;
// local function declarations
inline void eltwise_binary_sfpu_configure_addrmod(){
    // NOTE: this kernel is typically used in conjunction with
    //       A2D, which is using ADDR_MOD_0 and ADDR_MOD_2, so use one
    //       that doesn't conflict!

    addr_mod_t{
        .srca = {.incr = 0},
        .srcb = {.incr = 0},
        .dest = {.incr = 0},
    }.set(ADDR_MOD_7);

}
inline void eltwise_binary_sfpu_configure_mop();

template <DstSync Dst = DstSync::SyncFull>
inline void _llk_math_eltwise_binary_sfpu_start_(const uint dst_index) {
    if constexpr ((Dst == DstSync::SyncTile16) || (Dst == DstSync::SyncTile2)) {
        math::set_dst_write_addr<DstTileLayout::Default, DstTileShape::Tile32x32>(math_sync_tile_dst_index);
    } else {
        math::set_dst_write_addr<DstTileLayout::Default, DstTileShape::Tile32x32>(dst_index);
    }
    math::set_addr_mod_base();
    TTI_STALLWAIT(p_stall::STALL_SFPU, p_stall::MATH);
}

inline void _llk_math_eltwise_binary_sfpu_done_() {
    math::clear_dst_reg_addr();

    TTI_STALLWAIT(p_stall::STALL_CFG, p_stall::WAIT_SFPU);
    math::clear_addr_mod_base();
}

inline void _llk_math_eltwise_binary_sfpu_inc_dst_face_addr_() {
    math::inc_dst_addr<8>();
    math::inc_dst_addr<8>();
}

inline void _llk_math_eltwise_binary_sfpu_init_() {
    eltwise_binary_sfpu_configure_addrmod();
    math::reset_counters(p_setrwc::SET_ABD_F);
}
