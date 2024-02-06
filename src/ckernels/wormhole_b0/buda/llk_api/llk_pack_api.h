// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ckernel.h"
#include "ckernel_defs.h"
#include "ckernel_template.h"
#include "cpack_common.h"
#include "ckernel_globals.h"

#include "llk_io.h"
#include "llk_defs.h"
#include "llk_outputs.h"
#include "llk_param_structs.h"
#include "llk_pack.h"
#include "llk_pack_common.h"

/*************************************************************************
* LLK PACK
*************************************************************************/ 

template <bool untilize = false, bool zero_output = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor, bool write_tile_header = true>
inline void llk_pack_mop_config(const uint32_t output) {

    const std::uint32_t output_id = get_output_id(output);
    const std::uint32_t num_faces = get_output_num_faces(output_id);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const bool partial_face = get_output_partial_face(output_id) && IS_BFP_FORMAT((uint)pack_dst_format[output_id]);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    _llk_pack_mop_config_<untilize, zero_output, FaceLayout, write_tile_header>(
        pack_dst_format[output_id],
        face_r_dim, 
        num_faces, 
        partial_face,
        narrow_tile
    );
}

template <bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack_hw_configure(const llk_pack_params_t *pack_params) {

    const std::uint32_t output_id = get_output_id(pack_params->pack_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const std::uint32_t num_faces = get_output_num_faces(output_id);
    const bool partial_face = get_output_partial_face(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    const std::uint32_t tile_size = outputs[output_id].f.tile_size_words;

    _llk_pack_hw_configure_<untilize, is_fp32_dest_acc_en>(
        pack_src_format[output_id],
        pack_dst_format[output_id],
        tile_size,
        face_r_dim,
        num_faces,
        partial_face,
        narrow_tile,
        pack_params->relu_config.val
    );
}

template <bool untilize = false, bool is_fp32_dest_acc_en = false, ReluType relu_type = ReluType::NO_RELU, std::uint32_t relu_threshold = 0>
inline void llk_pack_hw_configure_disaggregated(std::uint32_t pack_output) {
    TT_LLK_DUMP("llk_pack_hw_configure_disaggregated<{}, {}, {}, {}>({})", untilize, is_fp32_dest_acc_en, relu_type, relu_threshold, pack_output);

    llk_pack_params_t llk_pack_params = {
        .pack_output = pack_output, .relu_config = {.f = {.ApplyRelu = (std::uint32_t)relu_type, .Threshold = relu_threshold,}}};
    llk_pack_hw_configure<untilize, is_fp32_dest_acc_en>(&llk_pack_params);
}

template <bool untilize = false, PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false>
inline void llk_pack_reduce_hw_configure(const llk_pack_params_t *pack_params) {
    const std::uint32_t output_id = get_output_id(pack_params->pack_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const std::uint32_t num_faces = get_output_num_faces(output_id);
    const bool partial_face = get_output_partial_face(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    const std::uint32_t tile_size = outputs[output_id].f.tile_size_words;

    _llk_pack_reduce_hw_configure_<untilize, type, dim, is_fp32_dest_acc_en>(
        pack_src_format[output_id],
        pack_dst_format[output_id],
        tile_size,
        face_r_dim,
        num_faces,
        partial_face,
        narrow_tile,
        pack_params->relu_config.val
    );
}

template <bool untilize = false, PoolType type, ReduceDim dim, bool is_fp32_dest_acc_en = false, ReluType relu_type = ReluType::NO_RELU, std::uint32_t relu_threshold = 0>
inline void llk_pack_reduce_hw_configure_disaggregated(std::uint32_t pack_output) {
    TT_LLK_DUMP("llk_pack_reduce_hw_configure_disaggregated<{}, {}, {}, {}, {}, {}>({})", untilize, type, dim, is_fp32_dest_acc_en, relu_type, relu_threshold, pack_output);
    llk_pack_params_t llk_pack_params = {
        .pack_output = pack_output, .relu_config = {.f = {.ApplyRelu = (std::uint32_t)relu_type, .Threshold = relu_threshold}}};
    llk_pack_reduce_hw_configure<untilize, type, dim, is_fp32_dest_acc_en>(&llk_pack_params);
} 

template <std::uint32_t block_ct_dim = 8>
inline void llk_pack_untilize_init() {
    TT_LLK_DUMP("llk_pack_untilize_init<{}>()", block_ct_dim);

    _llk_pack_untilize_init_<block_ct_dim>();
}

template <bool untilize = false, bool zero_output = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor, bool write_tile_header = true>
inline void llk_pack_init(const std::uint32_t pack_output) {
    TT_LLK_DUMP("llk_pack_init<{}, {}, {}>({})", untilize, zero_output, FaceLayout, pack_output);

    const std::uint32_t output_id = get_output_id(pack_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const std::uint32_t num_faces = get_output_num_faces(output_id);
    const bool partial_face = get_output_partial_face(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    _llk_pack_init_<untilize, zero_output, FaceLayout, write_tile_header>(
        pack_dst_format[output_id],
        face_r_dim, 
        num_faces, 
        partial_face,
        narrow_tile
    );
}

template <bool out_of_order_output, bool untilize>
inline std::uint32_t get_output_tile_address(std::uint8_t output_id, std::uint32_t output_tile_index) {

    std::uint32_t pack_tile_addr;
    if constexpr (out_of_order_output) {
        pack_tile_addr = outputs[output_id].f.fifo_wr_ptr +
                        (std::uint32_t)(outputs[output_id].f.tile_size_words)*output_tile_index;
    } else {
        if constexpr (untilize) {
            std::uint16_t out_tile_index = (outputs[output_id].f.ublock_tile_cnt/outputs[output_id].f.ublock_ct)*outputs[output_id].f.row_tile_dim +
                                            outputs[output_id].f.ublock_tile_cnt%outputs[output_id].f.ublock_ct; //FIXME: optimize perf
            pack_tile_addr = outputs[output_id].f.fifo_wr_ptr + outputs[output_id].f.fifo_wr_tile_ptr - 1;
            pack_tile_addr += out_tile_index*(std::uint32_t)(outputs[output_id].f.tile_size_words);

            outputs[output_id].f.ublock_tile_cnt++;

            if (outputs[output_id].f.ublock_tile_cnt == outputs[output_id].f.ublock_tile_dim) {
               outputs[output_id].f.ublock_tile_cnt=0;
               outputs[output_id].f.fifo_wr_tile_ptr += (std::uint32_t)(outputs[output_id].f.tile_size_words)*outputs[output_id].f.ublock_ct;
            }
        } else {
            pack_tile_addr = outputs[output_id].f.fifo_wr_ptr + outputs[output_id].f.fifo_wr_tile_ptr;
            outputs[output_id].f.fifo_wr_tile_ptr += outputs[output_id].f.tile_size_words;
        }
    }
    return pack_tile_addr;
}

template <bool out_of_order_output = false, DstSync Dst = SyncFull, bool untilize = false, bool is_fp32_dest_acc_en = false, bool pack_l1_acc_en = false>
inline void llk_pack_decouple(std::uint32_t tile_index, std::uint32_t output, std::uint32_t output_tile_index = 0, bool pack_l1_acc = false) {
#if defined(PERF_DUMP) && MATH_PACK_DECOUPLE
    TT_LLK_DUMP("llk_pack_decouple<{}, {}, {}, {}, {}>({}, {}, {}, {})", out_of_order_output, Dst, untilize, is_fp32_dest_acc_en, pack_l1_acc_en, tile_index, output, output_tile_index, pack_l1_acc);
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
#endif
}

template <std::uint32_t block_ct_dim = 8>
inline void llk_pack_untilize(std::uint32_t num_blocks, std::uint32_t output) {
    TT_LLK_DUMP("llk_pack_untilize<{}>({}, {})", block_ct_dim, num_blocks, output);

    const std::uint32_t output_id = get_output_id(output);
    std::uint32_t pack_tile_addr = outputs[output_id].f.fifo_wr_ptr  - 1;

    for (std::uint32_t block=0; block<num_blocks; block++) {

        _llk_pack_untilize_<block_ct_dim>(
            pack_tile_addr,
            pack_dst_format[output_id]
        );

        pack_tile_addr += block_ct_dim*(std::uint32_t)(outputs[output_id].f.tile_size_words);
    }
}

template <bool out_of_order_output = false, DstSync Dst = SyncFull, bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack(std::uint32_t tile_index, std::uint32_t output, std::uint32_t output_tile_index = 0) {
    TT_LLK_DUMP("llk_pack<{}, {}, {}, {}>({}, {}, {})", out_of_order_output, Dst, untilize, is_fp32_dest_acc_en, tile_index, output, output_tile_index);

    std::uint8_t output_id = get_output_id(output);

    static_assert((!(untilize && out_of_order_output)) && "untilize out of order packing is not supported!");

    std::uint32_t pack_tile_addr = get_output_tile_address<out_of_order_output, untilize>(output_id, output_tile_index);

    _llk_pack_<Dst, untilize, is_fp32_dest_acc_en>(
        tile_index,
        pack_tile_addr
    );
}

/*************************************************************************
* LLK PACK COMMON
*************************************************************************/ 


inline void llk_packer_wait_for_math_done() {
    TT_LLK_DUMP("llk_packer_wait_for_math_done()");
    _llk_packer_wait_for_math_done_();
}

template <uint WaitRes = p_stall::NONE>
inline void llk_packer_set_math_semaphore() {
    _llk_packer_set_math_semaphore_<WaitRes>();
}

template <DstSync Dst, bool is_fp32_dest_acc_en = false>
inline void llk_pack_dest_section_done() {
    TT_LLK_DUMP("llk_pack_dest_section_done<{}, {}>()", Dst, is_fp32_dest_acc_en);
    _llk_pack_dest_section_done_<Dst, is_fp32_dest_acc_en>();
} 

template <DstSync Dst, DstTileFaceLayout FaceLayout, bool untilize = false>
inline void llk_init_packer_dest_offset_registers(const std::uint32_t pack_output) {
    TT_LLK_DUMP("llk_init_packer_dest_offset_registers<{}, {}, {}>({})", Dst, FaceLayout, untilize, pack_output);
    const std::uint32_t output_id = get_output_id(pack_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    _llk_init_packer_dest_offset_registers_<Dst, FaceLayout, untilize>(
        face_r_dim,
        narrow_tile
    );
}

template <DstSync Dst, DstTileFaceLayout FaceLayout = RowMajor, bool untilize = false, bool is_fp32_dest_acc_en = false>
inline void llk_pack_dest_init(const std::uint32_t pack_output = 0) {
    TT_LLK_DUMP("llk_pack_dest_init<{}, {}, {}, {}>({})", Dst, FaceLayout, untilize, is_fp32_dest_acc_en, pack_output);

    const std::uint32_t output_id = get_output_id(pack_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    _llk_pack_dest_init_<Dst, FaceLayout, untilize, is_fp32_dest_acc_en>(
        face_r_dim,
        narrow_tile
    );
}    

template <bool mail2math=true, bool mail2pack=true>
inline void llk_pack_get_tile(std::uint32_t output, std::uint32_t tile_index, std::uint32_t *p_tile) {
    TT_LLK_DUMP("llk_pack_get_tile<{}, {}>({}, {}, tile_pointer)", mail2math, mail2pack, output, tile_index);
    _llk_pack_get_tile_<mail2math, mail2pack>(tile_index, p_tile);
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_pack_release_tile(std::uint32_t output) {
    TT_LLK_DUMP("llk_pack_release_tile<{}, {}>({})", mail2math, mail2pack, output);
    _llk_pack_release_tile_<mail2math, mail2pack>();
}

inline void llk_pack_debug_dump(std::uint8_t *data, std::uint32_t byte_size) {
    TT_LLK_DUMP("llk_pack_debug_dump(ptr, {})", byte_size);
    _llk_pack_debug_dump_(data, byte_size);
}

inline void llk_pack_debug_dump_seek(std::uint8_t offset) {
    _llk_pack_debug_dump_seek_(offset);
}

template <bool is_fp32_dest_acc_en = false, bool is_tile_dim_reconfig_en = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor>
inline void llk_pack_reconfig_data_format(const std::uint32_t new_output) {
    TT_LLK_DUMP("llk_pack_reconfig_data_format<{}>({})", is_fp32_dest_acc_en, new_output);

    const std::uint32_t output_id = get_output_id(new_output);
    const std::uint32_t face_r_dim = get_output_face_r_dim(output_id);
    const std::uint32_t num_faces = get_output_num_faces(output_id);
    const bool partial_face = get_output_partial_face(output_id);
    const bool narrow_tile = get_output_narrow_tile(output_id);

    _llk_pack_reconfig_data_format_<is_fp32_dest_acc_en, is_tile_dim_reconfig_en, FaceLayout>(
        pack_src_format[output_id], 
        pack_dst_format[output_id],
        outputs[output_id].f.tile_size_words,
        face_r_dim,
        num_faces,
        partial_face,
        narrow_tile
    );
}

template <bool is_fp32_dest_acc_en = false, bool is_tile_dim_reconfig_en = false, DstTileFaceLayout FaceLayout = DstTileFaceLayout::RowMajor, bool write_tile_header = true>
inline void llk_pack_reconfig_data_format(const std::uint32_t old_output, const std::uint32_t new_output) {
    TT_LLK_DUMP("llk_pack_reconfig_data_format<{}>({}, {})",is_fp32_dest_acc_en, old_output, new_output);
    std::uint32_t old_output_id = get_output_id(old_output);
    std::uint32_t new_output_id = get_output_id(new_output);

    if((pack_dst_format[old_output_id] != pack_dst_format[new_output_id])
       && (pack_dst_format[old_output_id] != (uint)DataFormat::Invalid) 
       && (pack_dst_format[new_output_id] != (uint)DataFormat::Invalid)) {
        llk_pack_reconfig_data_format<is_fp32_dest_acc_en, is_tile_dim_reconfig_en, FaceLayout>(new_output);
    } else if constexpr (is_tile_dim_reconfig_en) {
        // Same format but different tile dims
        llk_pack_mop_config<false, false, FaceLayout, write_tile_header>(new_output);
    }
}

TT_ALWAYS_INLINE void llk_pack_relu_config(const std::uint32_t config) {
    TT_LLK_DUMP("llk_pack_relu_config({})", config);
    _llk_pack_relu_config_(config);
}

inline void llk_pack_reconfig_l1_acc(const std::uint32_t enable) {
    TT_LLK_DUMP("llk_pack_reconfig_l1_acc({})", enable);
    _llk_pack_reconfig_l1_acc_(enable);
}

template <bool untilize = false, ReduceDim dim>
inline void llk_pack_reduce_mask_config() {
    TT_LLK_DUMP("llk_pack_reduce_mask_config<{}, {}>()", untilize, dim);
    _llk_pack_reduce_mask_config_<untilize, dim>();
}

inline void llk_pack_reduce_mask_clear() {
    TT_LLK_DUMP("llk_pack_reduce_mask_clear()");
    _llk_pack_reduce_mask_clear_();
}