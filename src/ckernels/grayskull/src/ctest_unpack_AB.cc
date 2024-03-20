#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_AB_params.h"
#include "llk_unpack_api.h"
#include "llk_io_unpack.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#endif

namespace NAMESPACE {
uint kernel_main(uint* param) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_AB_params = static_cast<const llk_unpack_AB_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int block_cnt = hlk_params->block_cnt;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;

    llk_unpack_AB_hw_configure(unpack_AB_params);
    llk_unpack_AB_init<BroadcastType::SRC_B_BROADCAST_TYPE>(0, 1);

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            llk_wait_tiles(0, dst_tile_rows * dst_tile_cols);
            llk_wait_tiles(1, dst_tile_rows * dst_tile_cols);

            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_unpack_AB<BroadcastType::SRC_B_BROADCAST_TYPE>(
                        0, 1, r * dst_tile_cols + c, r * dst_tile_cols + c);
                }
            }

            llk_pop_tiles(0, dst_tile_rows * dst_tile_cols);
            llk_pop_tiles(1, dst_tile_rows * dst_tile_cols);
        }
    }

    return 0;
}

}  // namespace NAMESPACE
