// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "llk_param_structs.h"
#include "ctest_db_math_matmul_params.h"
#include "llk_math_matmul.h"

#ifndef HF
#define HF 0
#endif
namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;
    const int block_tile_dim = hlk_params->block_tile_dim;
    uint sum = 0;

    llk_math_matmul_init<HF>(0, 1);
    llk_math_pack_sync_init<SyncHalf>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        llk_math_wait_for_dest_available<SyncHalf>();
        for (int b = 0; b < block_cnt; ++b) {
            sum = 0;
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    for (int i = 0; i < block_tile_dim; ++i) {
                        llk_math_matmul<HF>(sum + c);
                    }
                }
                sum += dst_tile_cols;  // r*dst_tile_cols
            }
        }
        llk_math_dest_section_done<SyncHalf>();
    }

    return 0;
}
}  // namespace NAMESPACE
