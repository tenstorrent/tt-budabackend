// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/splice_common.h"
#include "hlks/inc/wait_pop_tiles_utils.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, false);
    hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::in0, false, false);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {

    for (int input = 0; input < args->num_inputs; ++input) {
        if (kernel_broadcast[input] && !kernel_broadcast_per_t[input]) {
            hlk_wait_tiles(core_ptr, HlkOperand::in0 + input, kernel_broadcast[input]);
        }
    }

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        for (int input = 0; input < args->num_inputs; ++input) {
            if (kernel_broadcast[input] && kernel_broadcast_per_t[input]) {
                hlk_wait_tiles(core_ptr, HlkOperand::in0 + input, kernel_broadcast[input]);
            }
        }

        int block_index = 0;
        while (block_index < args->block_cnt) {
            for (int input = 0; input < args->num_inputs; ++input) {
                int index = args->input_idx[input][0];
                int length = args->input_idx[input][1];
                int stride = args->input_idx[input][2];

                // Drop blocks before index
                if (not kernel_broadcast[input]) {
                    for (int d = 0; d < index; d++) {
                        hlk_wait_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                        hlk_pop_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                    }
                }

                // Take blocks
                for (int t = 0; t < length; t++) {
                    if (not kernel_broadcast[input]) {
                        hlk_wait_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                    }
                    hlk_acquire_dst(core_ptr);
                    for (int t = 0; t < args->block_tile_dim; ++t) {
                        hlk_copy_tile_to_dst(
                            core_ptr,
                            HlkOperand::in0 + input,
                            (not kernel_broadcast[input]) ? t : t % kernel_broadcast[input],
                            t,
                            false);
                    }
                    if (not kernel_broadcast[input]) {
                        hlk_pop_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                    }
                    hlk_wait_for_free_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);

                    for (int t = 0; t < args->block_tile_dim; ++t) {
                        hlk_pack_tile_to_stream(core_ptr, t, HlkOperand::out0);
                    }

                    hlk_push_tiles(core_ptr, HlkOperand::out0, args->block_tile_dim);

                    hlk_release_dst(core_ptr);
                }

                // Skip blocks
                if (not kernel_broadcast[input]) {
                    for (int t = 0; t < (stride - length); t++) {
                        hlk_wait_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                        hlk_pop_tiles(core_ptr, HlkOperand::in0 + input, args->block_tile_dim);
                    }
                }

                block_index += length;
            }
        }

        for (int input = 0; input < args->num_inputs; ++input) {
            if (kernel_broadcast[input] && kernel_broadcast_per_t[input]) {
                hlk_pop_tiles(core_ptr, HlkOperand::in0 + input, kernel_broadcast[input]);
            }
        }
    }
    for (int input = 0; input < args->num_inputs; ++input) {
        if (kernel_broadcast[input] && !kernel_broadcast_per_t[input]) {
            hlk_pop_tiles(core_ptr, HlkOperand::in0 + input, kernel_broadcast[input]);
        }
    }
}
