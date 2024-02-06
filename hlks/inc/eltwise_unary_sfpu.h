// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include <cstdint>

constexpr int SFPU_MAX_NUM_INPUTS = 1;
struct hlk_args_t {
    // Common arguments for all sfpu ops
    std::uint32_t block_tile_dim;
    std::uint32_t block_cnt;
    std::uint32_t batch_cnt;
    std::uint32_t num_m_sub_blocks;
    std::uint32_t num_n_sub_blocks;
    std::uint32_t num_tiles_per_m_sub_block;
    std::uint32_t num_tiles_per_n_sub_block;
    std::uint32_t vector_mode;
    std::uint32_t gradient_op;
    std::uint32_t transpose;
    std::uint32_t kernel_broadcast[SFPU_MAX_NUM_INPUTS];
    std::uint32_t kernel_broadcast_per_t[SFPU_MAX_NUM_INPUTS];

    // op specific arguments

    // dropout
    std::uint32_t probability;
    std::uint32_t scale;
    std::uint32_t seed;

    // lrelu
    std::uint32_t slope;

    // power
    std::uint32_t exponent;
};

#include "hlks/inc/wait_pop_tiles_utils.h"

// SFPU_KERNEL_TYPE will be defined when the appropriate sfpu kernel is ran.
// It's definition will be in tt_build/test_name/graph_name/op_index/hlk_compile_time_constants.h
#if !defined(SFPU_KERNEL_TYPE_DEFINED)
constexpr SfpuOp SFPU_KERNEL_TYPE = SfpuOp::Abs;
#endif

template <SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE unsigned int get_sfpu_specific_parameter_for_init(const hlk_args_t *args) {
    if constexpr (sfpu_op == SfpuOp::Dropout) {
        return args->seed;
    }
    return 0;
}

template <SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE unsigned int get_sfpu_specific_parameter_0_for_op(const hlk_args_t *args) {
    if constexpr (sfpu_op == SfpuOp::Power) {
        return args->exponent;
    }

    if constexpr (sfpu_op == SfpuOp::Dropout) {
        return args->probability;
    }

    if constexpr (sfpu_op == SfpuOp::LRelu) {
        return args->slope;
    }

    return 0;
}

template <SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE unsigned int get_spfu_specific_parameter_1_for_op(const hlk_args_t *args) {
    if constexpr (sfpu_op == SfpuOp::Dropout) {
        return args->scale;
    }
    return 0;
}

template<SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_sfpu_op_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_unary_sfpu_init<sfpu_op>(core_ptr, HlkOperand::in0, get_sfpu_specific_parameter_for_init<sfpu_op>(args));
    hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::in0, args->transpose, args->transpose);
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, args->transpose);
}

template<SfpuOp sfpu_op>
TT_HLK_ALWAYS_INLINE void hlk_sfpu_op_main(tt_core *core_ptr, const hlk_args_t *args) {
    unsigned int sfpu_op_specific_param_0 = get_sfpu_specific_parameter_0_for_op<sfpu_op>(args);
    unsigned int sfpu_op_specific_param_1 = get_spfu_specific_parameter_1_for_op<sfpu_op>(args);

    wait_for_tiles_kernel_broadcast<SFPU_MAX_NUM_INPUTS>(core_ptr);

    for (unsigned int batch = 0; batch < args->batch_cnt; ++batch) {
        wait_for_tiles_kernel_broadcast_t<SFPU_MAX_NUM_INPUTS>(core_ptr);
        for(unsigned int block = 0; block < args->block_cnt; ++block) {
            hlk_acquire_dst(core_ptr);

            wait_for_tiles_no_broadcast<SFPU_MAX_NUM_INPUTS>(core_ptr, args);

            for (unsigned int t = 0; t < args->block_tile_dim; ++t) {
                hlk_copy_tile_to_dst(core_ptr, HlkOperand::in0, get_tile_index<0>(args, t, block), t, args->transpose);
                hlk_unary_sfpu_op<sfpu_op>(
                    core_ptr,
                    HlkOperand::in0,
                    t,
                    args->vector_mode,
                    sfpu_op_specific_param_0,
                    sfpu_op_specific_param_1);
            }

            pop_tiles_no_broadcast<SFPU_MAX_NUM_INPUTS>(core_ptr, args);

            hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);

            for(unsigned int t = 0; t < args->block_tile_dim; ++t) {
                hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::out0);
            }

            hlk_push_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);
            hlk_release_dst(core_ptr);
        }
        pop_tiles_kernel_broadcast_t<SFPU_MAX_NUM_INPUTS>(core_ptr);
    }

    pop_tiles_kernel_broadcast<SFPU_MAX_NUM_INPUTS>(core_ptr);
}
