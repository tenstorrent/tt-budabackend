// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "llk_param_structs.h"
#include "ctest_math_eltwise_binary_params.h"
#include "llk_math_eltwise_binary.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#endif

#ifndef ELTWISE_BINARY_TYPE
#define ELTWISE_BINARY_TYPE ELWADD
#endif

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_math_eltwise_binary_init<EltwiseBinaryType::ELTWISE_BINARY_TYPE, BroadcastType::SRC_B_BROADCAST_TYPE>();
    llk_math_pack_sync_init<SyncFull>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            // llk_wait_tiles(0,dst_tile_rows*block_tile_dim);
            // llk_wait_tiles(1,dst_tile_cols*block_tile_dim);
            llk_math_wait_for_dest_available<SyncFull>();

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_eltwise_binary<EltwiseBinaryType::ELTWISE_BINARY_TYPE, BroadcastType::SRC_B_BROADCAST_TYPE, SyncFull>(
                        r * dst_tile_cols + c);
                }
            }

            // llk_pop_tiles(0,dst_tile_rows*block_tile_dim);
            // llk_pop_tiles(1,dst_tile_cols*block_tile_dim);
            llk_math_dest_section_done<SyncFull>();
        }
    }

    return 0;
}
}  // namespace NAMESPACE
