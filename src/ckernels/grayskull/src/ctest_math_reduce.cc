// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_math_api.h"
#include "ctest_math_reduce_params.h"

#ifndef REDUCE_TYPE
#define REDUCE_TYPE AVG
#endif
#ifndef REDUCE_DIM
#define REDUCE_DIM REDUCE_ROW
#endif
#ifndef HF
#define HF 0
#endif
namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int num_block_rows = hlk_params->num_block_rows;
    const int num_block_columns = hlk_params->num_block_columns;
    const int num_tile_rows_per_block = hlk_params->num_tile_rows_per_block;
    const int num_tile_columns_per_block = hlk_params->num_tile_columns_per_block;

    llk_math_reduce_init<PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM, HF>();

    llk_math_pack_sync_init<SyncFull>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
            llk_math_wait_for_dest_available<SyncFull>();
        }
        int dest_offset = 0;
        for (int br = 0; br < num_block_rows; ++br) {
            for (int bc = 0; bc < num_block_columns; ++bc) {
                if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_ROW) {
                    llk_math_wait_for_dest_available<SyncFull>();
                }
                for (int r = 0; r < num_tile_rows_per_block; ++r) {
                    for (int c = 0; c < num_tile_columns_per_block; ++c) {
                        int dest_index = dest_offset;
                        if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_ROW) {
                            dest_index += r;
                        } else if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
                            dest_index += c;
                        }
                        llk_math_reduce<PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM, HF>(dest_index);
                    }
                }
            }
            if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_ROW) {
                dest_offset += num_tile_rows_per_block;
                llk_math_dest_section_done<SyncFull>();
            } else if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
                dest_offset = 0;
            }
        }

        if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
            llk_math_dest_section_done<SyncFull>();
        } else if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_SCALAR) {
            llk_math_dest_section_done<SyncFull>();
        }
    }

    return 0;
}
}  // namespace NAMESPACE
