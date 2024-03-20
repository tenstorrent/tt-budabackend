#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_db_pack_matmul_large_out_block_params.h"
#include "llk_pack_api.h"
#include "llk_io_pack.h"

namespace NAMESPACE {
//    uint kernel_main(const void* hlk_args=nullptr, const void* llk_args=nullptr, const uint loop_count=0)
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto pack_params = static_cast<const llk_pack_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_pack_init(16);

    llk_setup_outputs();
    llk_pack_hw_configure(pack_params);

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_pack_dest_init<DstSync::SyncHalf, DstTileFaceLayout::RowMajor>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                uint output_id = (b == (block_cnt - 1)) ? 16 : 24;

                llk_wait_for_free_tiles<false,false,true>(output_id, dst_tile_cols);
                llk_packer_wait_for_math_done();

                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_pack(c, output_id);
                }

                llk_pack_dest_section_done<DstSync::SyncHalf>();
                llk_push_tiles<false,true>(output_id, dst_tile_cols);
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
