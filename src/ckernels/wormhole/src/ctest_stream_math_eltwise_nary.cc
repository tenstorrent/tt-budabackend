// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "llk_math_eltwise_binary.h"
#include "ctest_stream_math_eltwise_nary_params.h"

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
    const auto num_inputs = static_cast<const uint>(hlk_num_inputs);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_math_eltwise_binary_init<EltwiseBinaryType::ELTWISE_BINARY_TYPE, BroadcastType::SRC_B_BROADCAST_TYPE,0,true>();
    llk_math_pack_sync_init<SyncTile16>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_wait_for_dest_available<SyncTile16>();
                    for (uint input = 0; input < num_inputs; ++input) {
                       llk_math_eltwise_binary<EltwiseBinaryType::ELTWISE_BINARY_TYPE, BroadcastType::SRC_B_BROADCAST_TYPE, SyncTile16 , 0, true>(0, input==0);
                    } 
                    llk_math_dest_section_done<SyncTile16>();
                }
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
