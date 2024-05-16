// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_pack_reduce_params.h"
#include "llk_pack_api.h"
#include "llk_io_pack.h"

#ifndef REDUCE_TYPE
#define REDUCE_TYPE AVG
#endif

#ifndef REDUCE_DIM
#define REDUCE_DIM REDUCE_ROW
#endif

#ifndef UNTILIZE_EN
#define UNTILIZE_EN false
#endif

#ifndef FP32_DEST_ACC_EN
#define FP32_DEST_ACC_EN false
#endif

namespace NAMESPACE {
//    uint kernel_main(const void* hlk_args=nullptr, const void* llk_args=nullptr, const uint loop_count=0)
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto pack_params = static_cast<const llk_pack_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_pack_init(16);
    llk_setup_outputs();
    llk_pack_reduce_hw_configure<UNTILIZE_EN, PoolType::REDUCE_TYPE, ReduceDim::REDUCE_DIM, FP32_DEST_ACC_EN>(pack_params);

    const int num_block_rows = hlk_params->num_block_rows;
    const int num_block_columns = hlk_params->num_block_columns;
    const int num_tile_rows_per_block = hlk_params->num_tile_rows_per_block;
    const int num_tile_columns_per_block = hlk_params->num_tile_columns_per_block;

    llk_pack_dest_init<SyncFull, RowMajor, UNTILIZE_EN, FP32_DEST_ACC_EN>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        int dest_offset = 0;
        int num_blocks = num_block_rows;
        if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
            num_blocks =
                1;  // Similar to bottom comment, cannot accumulat multiple blocks of columns unless intermediates
        }
        for (int b = 0; b < num_blocks; ++b) {
            int dest_tiles_per_block = num_tile_rows_per_block;
            if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_ROW) {
                dest_tiles_per_block = num_tile_rows_per_block;
            } else if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
                dest_tiles_per_block = num_tile_columns_per_block;
            }

            llk_wait_for_free_tiles<false,false,true>(16, dest_tiles_per_block);
            llk_packer_wait_for_math_done();

            for (int d = 0; d < dest_tiles_per_block; ++d) {
                constexpr bool out_of_order_output = false;
                constexpr bool untilize = false;
                llk_pack<out_of_order_output, DstSync::SyncFull, untilize, FP32_DEST_ACC_EN>(d + dest_offset, 16);
            }

            llk_pack_dest_section_done<DstSync::SyncFull, FP32_DEST_ACC_EN>();
            llk_push_tiles<false,true>(16, dest_tiles_per_block);
            if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_ROW) {
                dest_offset += dest_tiles_per_block;
            } else if constexpr (ReduceDim::REDUCE_DIM == ReduceDim::REDUCE_COL) {
                // Reduce Col cannot accumulate more than a dst space worth of columns,
                // 16*32=512 wide unless the order of the blocks are column major or
                // we can push the results to intermediates.  For now we don't support
                // this operation, so the width of the input tensor is restricted to what fits in dst.
                dest_offset = 0;
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
