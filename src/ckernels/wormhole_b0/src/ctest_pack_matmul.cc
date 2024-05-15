#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_pack_matmul_params.h"
#include "llk_pack_api.h"
#include "llk_io_pack.h"

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
    llk_pack_hw_configure<UNTILIZE_EN, FP32_DEST_ACC_EN>(pack_params);

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;
    uint sum = 0;

    llk_pack_dest_init<DstSync::SyncFull, DstTileFaceLayout::RowMajor, UNTILIZE_EN, FP32_DEST_ACC_EN>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        llk_wait_for_free_tiles<false,false,true>(16, dst_tile_rows * dst_tile_cols);
        llk_packer_wait_for_math_done();

        sum = 0;
        for (int r = 0; r < dst_tile_rows; ++r) {
            for (int c = 0; c < dst_tile_cols; ++c) {
                llk_pack(sum + c, 16);
            }
            sum += dst_tile_cols;  // r*dst_tile_cols
        }

        llk_pack_dest_section_done<DstSync::SyncFull, FP32_DEST_ACC_EN>();
        llk_push_tiles<false,true>(16, dst_tile_rows * dst_tile_cols);
    }

    return 0;
}
}  // namespace NAMESPACE
