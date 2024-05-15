// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "hlks/inc/eltwise_unary_sfpu.h"

TT_HLK_ALWAYS_INLINE void hlk_setup_kernel(tt_core* core_ptr, const hlk_args_t *args) {
    hlk_sfpu_op_setup_kernel<SFPU_KERNEL_TYPE>(core_ptr, args);
}

TT_HLK_ALWAYS_INLINE void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_sfpu_op_main<SFPU_KERNEL_TYPE>(core_ptr, args);
}
