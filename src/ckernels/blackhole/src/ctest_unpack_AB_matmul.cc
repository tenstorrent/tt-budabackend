#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_AB_matmul_params.h"
#include "llk_unpack_api.h"
#include "llk_io_unpack.h"

#ifndef FP32_DEST_ACC_EN
#define FP32_DEST_ACC_EN false
#endif
namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_AB_matmul_params = static_cast<const llk_unpack_AB_matmul_params_t*>(&llk_args);

    llk_unpack_AB_matmul_hw_configure<FP32_DEST_ACC_EN>(unpack_AB_matmul_params);
    llk_unpack_AB_matmul_init(0, 1, unpack_AB_matmul_params->transpose_xy_srca);

    llk_setup_operands();

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;
    const int block_tile_dim = hlk_params->block_tile_dim;
    uint sum0 = 0, sum1 = 0;

    for (int b = 0; b < block_cnt; ++b) {
        llk_wait_tiles(0, dst_tile_rows * block_tile_dim);
        llk_wait_tiles(1, dst_tile_cols * block_tile_dim);

        sum0 = 0;
        for (int r = 0; r < dst_tile_rows; ++r) {
            for (int c = 0; c < dst_tile_cols; ++c) {
                sum1 = 0;
                for (int i = 0; i < block_tile_dim; ++i) {
                    llk_unpack_AB_matmul(0, 1, sum0 + i, sum1 + c);
                    sum1 += dst_tile_cols;

                    // core_ptr->llk_mm_tile(0,1, r*block_tile_dim+i, i*dst_tile_cols+c, dst_tile_cols*r+c);
                }
            }
            sum0 += block_tile_dim;
        }

        llk_pop_tiles(0, dst_tile_rows * block_tile_dim);
        llk_pop_tiles(1, dst_tile_cols * block_tile_dim);
    }

    return 0;
}

}  // namespace NAMESPACE
