// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hlk_api.h"
#include "eltwise_binary.h"
#include "binary_sfpu_inner_loop.h"
#include "wait_pop_tiles_utils.h"

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, args->transpose);

    // SFPU init
    if constexpr (binary_op == BinaryOp::Quantization) {
        hlk_sfpu_quant_int32_init(core_ptr, args->zero_point);
    } else if constexpr (binary_op == BinaryOp::Dequantization) {
        hlk_sfpu_dequant_int32_init(core_ptr, args->zero_point);
    } else if  constexpr (binary_op == BinaryOp::Requantization) {
        hlk_sfpu_requant_int32_init(core_ptr, args->zero_point);
    } else if constexpr (binary_op == BinaryOp::Add) {
        hlk_sfpu_add_int32_init(core_ptr);
    } else if constexpr (binary_op == BinaryOp::Maximum) {
        hlk_unary_sfpu_init<SfpuOp::Max>(core_ptr, HlkOperand::out0);
    }
}

template <BinaryOp binary_op>
TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    const int half_dest_size = args->is_32bit_dest_en ? 2 : 4;
    const int num_iter = args->block_tile_dim > half_dest_size ? 2 : 1;  // Number of iterations through dest per block

    wait_for_tiles_kernel_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr);

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        wait_for_tiles_kernel_broadcast_t<BINARY_MAX_NUM_INPUTS>(core_ptr);

        for (int block = 0; block < args->block_cnt; ++block) {
            // Wait for tiles on the input if streaming or it is first kernel_broadcast
            wait_for_tiles_no_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr, args);

            binary_sfpu_inner_loop<binary_op>(
                core_ptr,
                HlkOperand::in0,
                HlkOperand::in1,
                HlkOperand::out0,
                args->block_tile_dim,
                block,
                args->transpose,
                half_dest_size,
                num_iter,
                kernel_broadcast[0],
                kernel_broadcast[1]);

            // Pop tiles on the input if streaming or it is last kernel_broadcast
            pop_tiles_no_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr, args);
        }
        pop_tiles_kernel_broadcast_t<BINARY_MAX_NUM_INPUTS>(core_ptr);
    }

    pop_tiles_kernel_broadcast<BINARY_MAX_NUM_INPUTS>(core_ptr);
}