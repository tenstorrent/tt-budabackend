// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_stream_math_eltwise_unary_sfpi_params.h"
#include "llk_math_api.h"

#include <type_traits>

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#define MATH_DATACOPY_OP A2D
#else
#define MATH_DATACOPY_OP B2D
#endif

#ifndef TRANSPOSE_XY
#define TRANSPOSE_XY 0
#endif

#ifndef SFPU_TYPE
#define SFPU_TYPE test1
#endif

#ifndef APPROX
#define APPROX true
#endif

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    // const int block_tile_dim = hlk_params->block_tile_dim;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_math_eltwise_unary_datacopy_init<MATH_DATACOPY_OP, BroadcastType::SRC_B_BROADCAST_TYPE>(TRANSPOSE_XY);
    llk_math_pack_sync_init<SyncTile2>();

    llk_math_eltwise_unary_sfpi_init();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_wait_for_dest_available<SyncTile2>();
                    llk_math_eltwise_unary_datacopy<MATH_DATACOPY_OP, BroadcastType::SRC_B_BROADCAST_TYPE, SyncTile2>(
                        0);

                    if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test1) {
                        llk_math_eltwise_unary_sfpi_test1<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test2) {
                        llk_math_eltwise_unary_sfpi_test2<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test3) {
                        llk_math_eltwise_unary_sfpi_test3<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test4) {
                        llk_math_eltwise_unary_sfpi_test4<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test5) {
                        llk_math_eltwise_unary_sfpi_test5<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test6) {
                        llk_math_eltwise_unary_sfpi_test6<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test7) {
                        llk_math_eltwise_unary_sfpi_test7<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test8) {
                        llk_math_eltwise_unary_sfpi_test8<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test9) {
                        llk_math_eltwise_unary_sfpi_test9<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test10) {
                        llk_math_eltwise_unary_sfpi_test10<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test11) {
                        llk_math_eltwise_unary_sfpi_test11<SyncTile2>(0);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test12) {
                        llk_math_eltwise_unary_sfpi_test12<SyncTile2>(0, sfpu_params[0]);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test13) {
                        llk_math_eltwise_unary_sfpi_test13<SyncTile2>(0, sfpu_params[0]);
                    } else if constexpr (SfpiTestType::SFPU_TYPE == SfpiTestType::test14) {
                        llk_math_eltwise_unary_sfpi_test14<SyncTile2>(0, sfpu_params[0]);
                    } else {
                        static_assert_sfpi_type_dependent<SfpiTestType::SFPU_TYPE>();
                    }

                    llk_math_dest_section_done<SyncTile2>();
                }
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
