
#include "llk_param_structs.h"
#include "ctest_unpack_A_params.h"
#include "llk_unpack_A.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#endif

#ifndef TRANSPOSE_XY
#define TRANSPOSE_XY 0
#endif

#define ACC_TO_DEST false

#ifndef FP32_DEST_ACC_EN
#define FP32_DEST_ACC_EN false
#endif

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_A_params = static_cast<const llk_unpack_A_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int block_cnt = hlk_params->block_cnt;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;

    llk_unpack_A_hw_configure<FP32_DEST_ACC_EN>(unpack_A_params, TRANSPOSE_XY);
    llk_unpack_A_init<BroadcastType::SRC_B_BROADCAST_TYPE, ACC_TO_DEST>(TRANSPOSE_XY, TRANSPOSE_XY);

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_wait_tiles(0, dst_tile_rows * dst_tile_cols);

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_unpack_A<BroadcastType::SRC_B_BROADCAST_TYPE, ACC_TO_DEST>(0, r * dst_tile_cols + c, TRANSPOSE_XY);
                }
            }

            llk_pop_tiles(0, dst_tile_rows * dst_tile_cols);
        }
    }

    return 0;
}

}  // namespace NAMESPACE
