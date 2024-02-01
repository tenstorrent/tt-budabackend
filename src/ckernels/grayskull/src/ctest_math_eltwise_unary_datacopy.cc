
#include "hlks/inc/hlk_api.h"
#include "llk_math_api.h"
#include "ctest_math_eltwise_unary_params.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#define MATH_DATACOPY_OP A2D
#else
#define MATH_DATACOPY_OP B2D
#endif

#ifndef TRANSPOSE_XY
#define TRANSPOSE_XY 0
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
    llk_math_pack_sync_init<SyncFull>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_math_wait_for_dest_available<SyncFull>();

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_math_eltwise_unary_datacopy<MATH_DATACOPY_OP, BroadcastType::SRC_B_BROADCAST_TYPE, SyncFull>(
                        r * dst_tile_cols + c);
                }
            }

            llk_math_dest_section_done<SyncFull>();
        }
    }

    return 0;
}
}  // namespace NAMESPACE
