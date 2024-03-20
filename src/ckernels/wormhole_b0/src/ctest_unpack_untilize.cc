
#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_untilize_params.h"
#include "llk_unpack_api.h"

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_untilize_params = static_cast<const llk_unpack_untilize_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int block_cnt = hlk_params->block_cnt;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;

    llk_unpack_untilize_hw_configure(unpack_untilize_params);
    llk_unpack_untilize_init();

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                llk_wait_tiles(0, dst_tile_cols);

                // Output upper 16 rows
                llk_unpack_untilize<true>(0, dst_tile_cols);

                llk_unpack_untilize<false>(0, dst_tile_cols);

                llk_pop_tiles(0, dst_tile_cols);
            }
        }
    }

    return 0;
}

}  // namespace NAMESPACE
