// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/wait_pop_tiles_utils.h"


TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_hw_config_single_operand(core_ptr, HlkOperand::in0, args->transpose);
    hlk_copy_tile_to_dst_init(core_ptr, HlkOperand::in0, args->transpose, args->transpose);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        for(int block = 0; block < args->block_cnt; ++block) {
            // Wait for tiles on the input if streaming or it is first kernel_broadcast
            wait_for_tiles_no_broadcast<1>(core_ptr, args);
            pop_tiles_no_broadcast<1>(core_ptr, args);   
        }
    }
}
