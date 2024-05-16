// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_math_api.h"
#include "ctest_math_matmul_large_l1_acc_params.h"

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

    llk_math_matmul_init<HF, DstTileFaceLayout::RowMajor>(0, 1);

    llk_math_pack_sync_init<DstSync::SyncFull>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                llk_math_wait_for_dest_available<DstSync::SyncFull>();

                for (int c = 0; c < dst_tile_cols; ++c) {
                    for (int i = 0; i < block_tile_dim; ++i) {
                        llk_math_matmul<HF, DstTileFaceLayout::RowMajor>(c);
                    }
                }
                llk_math_dest_section_done<DstSync::SyncFull>();
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
