// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/eltwise_binary_sfpu_common.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_setup_kernel<BinaryOp::Add>(core_ptr, args);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_process_single_input<BinaryOp::Add>(core_ptr, args);
}