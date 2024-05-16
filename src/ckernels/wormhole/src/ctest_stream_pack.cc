// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "llk_param_structs.h"
#include "ctest_stream_pack_params.h"
#include "llk_pack.h"

#ifndef ZERO_OUTPUT
#define ZERO_OUTPUT false
#endif

#ifndef SYNC_TYPE
#define SYNC_TYPE SyncTile16
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

    llk_pack_hw_configure<UNTILIZE_EN, FP32_DEST_ACC_EN>(pack_params);
    llk_pack_init<UNTILIZE_EN, ZERO_OUTPUT, RowMajor>(16);

    llk_setup_outputs();

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_pack_dest_init<DstSync::SYNC_TYPE, RowMajor, UNTILIZE_EN, FP32_DEST_ACC_EN>();

    constexpr bool out_of_order_output = false;

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    if (not ZERO_OUTPUT) {
                        llk_packer_wait_for_math_done();
                    }
                    llk_wait_for_free_tiles<false,false,true>(16, 1);
                    llk_pack<out_of_order_output, DstSync::SYNC_TYPE, UNTILIZE_EN, FP32_DEST_ACC_EN>(0, 16);
                    llk_push_tiles<false,true>(16, 1);
                    if (not ZERO_OUTPUT) {
                        llk_pack_dest_section_done<DstSync::SYNC_TYPE, FP32_DEST_ACC_EN>();
                    }
                }
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
