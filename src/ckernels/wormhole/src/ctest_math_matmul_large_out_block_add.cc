
#include "llk_math_matmul.h"
#include "llk_math_eltwise_unary_datacopy.h"
#include "llk_math_eltwise_binary.h"
#include "ctest_math_matmul_large_out_block_add_params.h"

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

    llk_math_pack_sync_init<SyncFull>();
    uint sum0 = 0;

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_math_wait_for_dest_available<SyncFull>();

            if (b > 0) {
                llk_math_eltwise_unary_datacopy_init<A2D>();
                sum0 = 0;
                for (int r = 0; r < dst_tile_rows; ++r) {
                    for (int c = 0; c < dst_tile_cols; ++c) {
                        llk_math_eltwise_unary_datacopy<A2D>(sum0 + c);
                    }
                    sum0 += dst_tile_cols;
                }
            }

            llk_math_matmul_init<HF>(0, 1);

            sum0 = 0;
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    for (int i = 0; i < block_tile_dim; ++i) {
                        llk_math_matmul<HF>(sum0 + c);
                    }
                }
                sum0 += dst_tile_cols;
            }

            llk_math_dest_section_done<SyncFull>();
        }

        llk_math_eltwise_binary_init<ELWADD, BroadcastType::NONE>();

        llk_math_wait_for_dest_available<SyncFull>();
        sum0 = 0;
        for (int r = 0; r < dst_tile_rows; ++r) {
            for (int c = 0; c < dst_tile_cols; ++c) {
                llk_math_eltwise_binary<ELWADD, BroadcastType::NONE, SyncFull>(sum0 + c);
            }
            sum0 += dst_tile_cols;
        }
        llk_math_dest_section_done<SyncFull>();
    }

    return 0;
}
}  // namespace NAMESPACE
