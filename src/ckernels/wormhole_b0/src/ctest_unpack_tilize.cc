
#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_tilize_params.h"
#include "llk_unpack_api.h"

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_tilize_params = static_cast<const llk_unpack_tilize_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int block_cnt = hlk_params->block_cnt;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;

    llk_unpack_tilize_hw_configure(unpack_tilize_params);
    llk_unpack_tilize_init();

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int c = 0; c < dst_tile_rows; ++c) {
                llk_wait_blocks(0, 1);

                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_unpack_tilize(0, c, unpack_tilize_params->unpA_block_c_dim);
                }

                llk_pop_blocks(0, 1, unpack_tilize_params->unpA_block_c_dim);
            }
        }
    }

    return 0;
}

}  // namespace NAMESPACE
