// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "llk_io_pack.h"
#include "llk_defs.h"
#include "llk_param_structs.h"

#include "ckernel.h"
#include "ckernel_template.h"
#include "llk_pack_common.h"
#include "ckernel_globals.h"

using namespace ckernel;
using namespace ckernel::packer;

template <bool untilize = false, bool zero_output = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor>
inline void llk_pack_mop_config() {
    addr_mod_pack_t{
        .y_src = {.incr = untilize ? 0 : 1},
        .y_dst = {.incr = 1},
    }
        .set(ADDR_MOD_0);

    if constexpr (untilize) {
       addr_mod_pack_t{
           .y_src = { .incr = 1, .clr = 0, .cr = 1  },
           .y_dst = { .incr = 1, .clr = 0, .cr = 0  },
       }.set(ADDR_MOD_1);
    } else {
       addr_mod_pack_t{
           .y_src = {.incr = 0, .clr = 1, .cr = 0},
           .y_dst = {.incr = 0, .clr = 1, .cr = 0},
           .z_src = {.incr = 0, .clr = 0},
           .z_dst = {.incr = 0, .clr = 0},
       }
       .set(ADDR_MOD_1);
    }


    addr_mod_pack_t{
        .y_src = { .incr = 0, .clr = 0, .cr = 0  },
        .y_dst = { .incr = 0, .clr = 0, .cr = 0  },
    }.set(ADDR_MOD_2);

    const uint MOP_INNER_LOOP = 16;
    const uint MOP_UNTILIZE_INNER_LOOP = FaceLayout == DstTileFaceLayout::ColMajor ? 8 : 4;
    const uint MOP_OUTER_LOOP = 1;
    const uint MOP_UNTILIZE_OUTER_LOOP = 8;
    const uint PACKCNT = 4;
    const uint MEGAROW = 1;
    constexpr uint ZERO_OUTPUT_FLAG = zero_output ? p_pacr::P_ZERO_OUTPUT_ENABLED : p_pacr::P_ZERO_OUTPUT_DISABLED;

    ckernel::ckernel_template tmp(
        untilize ? MOP_UNTILIZE_OUTER_LOOP : MOP_OUTER_LOOP, untilize ? MOP_UNTILIZE_INNER_LOOP : MOP_INNER_LOOP, TT_OP_PACR(ADDR_MOD_0, ZERO_OUTPUT_FLAG, PACK_SEL(PACKCNT), 0, MEGAROW, 0, 0));

    // Write header to l1
    if constexpr (!untilize) {
        tmp.set_last_inner_loop_instr(TT_OP_PACR(ADDR_MOD_1, ZERO_OUTPUT_FLAG, PACK_SEL(PACKCNT), 0, 0, 0, 1));
        tmp.set_last_outer_loop_instr(TT_OP_PACR(ADDR_MOD_1, ZERO_OUTPUT_FLAG, PACK_SEL(PACKCNT), 0, 0, 0, 1));
        // Write header to l1
        tmp.set_end_op(TT_OP_STOREIND(
            1, 0, p_ind::LD_16B, LO_16(0), p_ind::INC_NONE, p_gpr_pack::TILE_HEADER, p_gpr_pack::OUTPUT_ADDR));
    } else {
        tmp.set_start_op(TT_OP_PACR(ADDR_MOD_0, ZERO_OUTPUT_FLAG, PACK_SEL(PACKCNT), 0, MEGAROW, 0, 0));
        tmp.set_loop_op0(TT_OP_INCADCXY(p_setadc::PAC, 0, 0, 4, 0));
        tmp.set_end_op(TT_OP_PACR(ADDR_MOD_1, ZERO_OUTPUT_FLAG, PACK_SEL(PACKCNT), 0, MEGAROW, 0, 0));
        tmp.set_last_inner_loop_instr(TT_OP_INCADCXY(p_setadc::PAC, 0, 0, 4, 0));
        tmp.set_last_outer_loop_instr(TT_OP_INCADCXY(p_setadc::PAC, 0, 0, 4, 0));
    }

    tmp.program(instrn_buffer);
}

template <bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack_hw_configure(const llk_pack_params_t *pack_params) {
    configure_pack<is_fp32_dest_acc_en>(get_output_id(pack_params->pack_output), pack_params->relu_config.val);

    std::uint32_t output = get_output_id(pack_params->pack_output);
    if constexpr (untilize) {
        regfile[p_gpr_pack::ONE_MSG_RECEIVED] =
            ((1 * GET_L1_HEADERLESS_TILE_SIZE((uint)pack_dst_format[output])) << 12) |
            1; /*SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE=12*/
        sync_regfile_write(p_gpr_pack::ONE_MSG_RECEIVED);
    }
}

template <bool untilize = false, bool is_fp32_dest_acc_en = false, ReluType relu_type = ReluType::NO_RELU, std::uint32_t relu_threshold = 0>
inline void llk_pack_hw_configure_disaggregated(std::uint32_t pack_output) {
    llk_pack_params_t llk_pack_params = {
        .pack_output = pack_output, .relu_config = {.f = {.ApplyRelu = (std::uint32_t)relu_type, .Threshold = relu_threshold}}};
    llk_pack_hw_configure<untilize, is_fp32_dest_acc_en>(&llk_pack_params);
}

// FIXME: Remove once edge mask spec is defined
template <bool untilize = false, PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false>
inline void llk_pack_reduce_hw_configure(const llk_pack_params_t *pack_params) {
    configure_pack<is_fp32_dest_acc_en>(get_output_id(pack_params->pack_output), pack_params->relu_config.val);
    volatile uint tt_reg_ptr *cfg = get_cfg_pointer();

    if constexpr (dim == ReduceDim::REDUCE_ROW) {
        for (uint i = 0; i < 4; i++) cfg[PCK_EDGE_OFFSET_SEC0_mask_ADDR32 + i] = 0x00000001;
    } else if constexpr (dim == ReduceDim::REDUCE_SCALAR) {
        cfg[PCK_EDGE_OFFSET_SEC0_mask_ADDR32+0] = 0x00000000;
        cfg[PCK_EDGE_OFFSET_SEC0_mask_ADDR32+1] = 0x00000001;
        cfg[TILE_ROW_SET_MAPPING_0_row_set_mapping_0_ADDR32] = 0x00000001;
    } else {
        cfg[PCK_EDGE_OFFSET_SEC0_mask_ADDR32+0] = 0x00000000;
        cfg[PCK_EDGE_OFFSET_SEC0_mask_ADDR32+1] = 0x0000ffff;
        
        if constexpr (untilize) {
            cfg[TILE_ROW_SET_MAPPING_0_row_set_mapping_0_ADDR32] = 0x00000005;
        } else {
            cfg[TILE_ROW_SET_MAPPING_0_row_set_mapping_0_ADDR32] = 0x00000001;
        }
    }

    if constexpr (untilize) {
        std::uint32_t output = get_output_id(pack_params->pack_output);
        regfile[p_gpr_pack::ONE_MSG_RECEIVED] =
            ((1 * GET_L1_HEADERLESS_TILE_SIZE((uint)pack_dst_format[output])) << 12) |
            1; /*SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE=12*/
        sync_regfile_write(p_gpr_pack::ONE_MSG_RECEIVED);
    }
}

template <bool untilize = false, PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false, ReluType relu_type = ReluType::NO_RELU, std::uint32_t relu_threshold = 0>
inline void llk_pack_reduce_hw_configure_disaggregated(std::uint32_t pack_output) {
    llk_pack_params_t llk_pack_params = {
        .pack_output = pack_output, .relu_config = {.f = {.ApplyRelu = (std::uint32_t)relu_type, .Threshold = relu_threshold}}};
    llk_pack_reduce_hw_configure<untilize, type, dim, is_fp32_dest_acc_en>(&llk_pack_params);
}

template <bool untilize = false, bool zero_output = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor>
inline void llk_pack_init(const uint32_t pack_output) {
    // Get tile dims based on pack_output
    llk_pack_mop_config<untilize, zero_output, FaceLayout>();
}

template <bool out_of_order_output, bool untilize>
inline std::uint32_t get_output_tile_address(std::uint8_t output_id, std::uint32_t output_tile_index) {

    std::uint32_t pack_tile_addr;
    if constexpr (out_of_order_output) {
        pack_tile_addr = outputs[output_id].f.fifo_wr_ptr +
                         MUL_TILE_SIZE_AND_INDEX((std::uint8_t)pack_dst_format[output_id], (std::uint16_t)output_tile_index);
    } else {
        if constexpr (untilize) {
            std::uint16_t out_tile_index = (outputs[output_id].f.ublock_tile_cnt/outputs[output_id].f.ublock_ct)*outputs[output_id].f.row_tile_dim +
                                            outputs[output_id].f.ublock_tile_cnt%outputs[output_id].f.ublock_ct; //FIXME: optimize perf
            pack_tile_addr = outputs[output_id].f.fifo_wr_ptr + outputs[output_id].f.fifo_wr_tile_ptr - 1;
            pack_tile_addr += out_tile_index*GET_L1_HEADERLESS_TILE_SIZE((std::uint8_t)pack_dst_format[output_id]);

            //outputs[output_id].f.fifo_wr_tile_ptr += GET_L1_HEADERLESS_TILE_SIZE((std::uint8_t)pack_dst_format[OUTPUT_BASE_ID]);

            outputs[output_id].f.ublock_tile_cnt++;

            if (outputs[output_id].f.ublock_tile_cnt == outputs[output_id].f.ublock_tile_dim) {
               outputs[output_id].f.ublock_tile_cnt=0;
               outputs[output_id].f.fifo_wr_tile_ptr += GET_L1_HEADERLESS_TILE_SIZE((std::uint8_t)pack_dst_format[output_id])*outputs[output_id].f.ublock_ct; //FIXME: optimize perf
            }
        } else {
            pack_tile_addr = outputs[output_id].f.fifo_wr_ptr + outputs[output_id].f.fifo_wr_tile_ptr;
            outputs[output_id].f.fifo_wr_tile_ptr += GET_L1_TILE_SIZE((std::uint8_t)pack_dst_format[output_id]);
        }
    }
    return pack_tile_addr;
}

#if defined(PERF_DUMP) && MATH_PACK_DECOUPLE
template <bool out_of_order_output = false, DstSync Dst = SyncFull, bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack_decouple(std::uint32_t tile_index, std::uint32_t output, std::uint32_t output_tile_index = 0) {

    std::uint8_t output_id = get_output_id(output);

    static_assert((!(untilize && out_of_order_output)) && "untilize out of order packing is not supported!");

    std::uint32_t pack_tile_addr = get_output_tile_address<out_of_order_output, untilize>(output_id, output_tile_index);

    if (operand_is_intermediate(output)) {
        return;
    }

    if constexpr (!untilize) {
        uint32_t tile_header[4];
        uint32_t* l1_dest = reinterpret_cast<uint32_t*>(pack_tile_addr << 4);
        for (int i = 0; i < 4; i++) {
            tile_header[i] = regfile[p_gpr_pack::TILE_HEADER + i];
            l1_dest[i] = tile_header[i];
        }
    }
}
#endif

template <bool out_of_order_output = false, DstSync Dst = SyncFull, bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack(std::uint32_t tile_index, std::uint32_t output, std::uint32_t output_tile_index = 0) {

    std::uint8_t output_id = get_output_id(output);
    // Get tile dims based on output_id

    static_assert((!(untilize && out_of_order_output)) && "untilize out of order packing is not supported!");

    std::uint32_t pack_tile_addr = get_output_tile_address<out_of_order_output, untilize>(output_id, output_tile_index);

    constexpr uint32_t DEST_NUM_TILES_SHIFT = is_fp32_dest_acc_en ? (1) : (0);
    constexpr uint32_t DEST_NUM_TILES = DEST_NUM_TILES_FP16 >> DEST_NUM_TILES_SHIFT;

    if constexpr (Dst == DstSync::SyncTile16) {
        // W-counter points to the next tile in dest
        TT_SETADC(p_setadc::PAC, p_setadc::CH_0, p_setadc::SET_W, pack_sync_tile_dst_ptr);
        pack_sync_tile_dst_ptr += 1;
        pack_sync_tile_dst_ptr = pack_sync_tile_dst_ptr & (DEST_NUM_TILES - 1);
    } else if constexpr (Dst == DstSync::SyncTile2) {
        TT_SETADC(p_setadc::PAC, p_setadc::CH_0, p_setadc::SET_W, pack_sync_tile_dst_ptr);
        pack_sync_tile_dst_ptr = 0;
    } else {
        TT_SETADC(p_setadc::PAC, p_setadc::CH_0, p_setadc::SET_W, tile_index);
    }

    program_packer_destination(pack_tile_addr, output_id);

    mop_run(1, 1);

    if constexpr (untilize) {
        TTI_PACR(ADDR_MOD_2, 0, 0xf, 0, 0, 1, 1); // close tile
        TTI_SETADC(p_setadc::PAC, p_setadc::CH_0, p_setadc::SET_Y, 0);
        TTI_INCADCZW(p_setadc::PAC, 0, 0, 1, 0);
    }

}
