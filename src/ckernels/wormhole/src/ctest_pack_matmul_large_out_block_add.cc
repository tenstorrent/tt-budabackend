#include "llk_param_structs.h"
#include "ctest_pack_matmul_large_out_block_add_params.h"
#include "llk_pack.h"
#include "llk_pack_common.h"

namespace NAMESPACE {
//    uint kernel_main(const void* hlk_args=nullptr, const void* llk_args=nullptr, const uint loop_count=0)
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto pack_params = static_cast<const llk_pack_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_pack_hw_configure(pack_params);
    llk_pack_init(16);

    llk_setup_outputs();

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_pack_dest_init<DstSync::SyncFull, DstTileFaceLayout::ColMajor>();
    uint output_id = 0;
    uint sum0 = 0;

    for (uint batch = 0; batch < loop_count; ++batch) {
        output_id = 24;  // store matmul result into the intermediate buffer 0

        llk_init_packer_dest_offset_registers<DstSync::SyncFull, DstTileFaceLayout::ColMajor>(16);

        for (int b = 0; b < block_cnt; ++b) {
            llk_wait_for_free_tiles<false,false,true>(output_id, dst_tile_rows * dst_tile_cols);
            llk_packer_wait_for_math_done();
            sum0 = 0;
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_pack(sum0 + c, output_id);
                }
                sum0 += dst_tile_cols;
            }

            llk_pack_dest_section_done<DstSync::SyncFull>();
            llk_push_tiles<false,true>(output_id, dst_tile_rows * dst_tile_cols);
        }

        output_id = 25;  // accumulate matmul result into the intermediate buffer 1

        llk_init_packer_dest_offset_registers<DstSync::SyncFull, DstTileFaceLayout::RowMajor>(16);

        llk_wait_for_free_tiles<false,false,true>(output_id, dst_tile_rows * dst_tile_cols);
        llk_packer_wait_for_math_done();

        sum0 = 0;
        for (int r = 0; r < dst_tile_rows; ++r) {
            for (int c = 0; c < dst_tile_cols; ++c) {
                llk_pack(sum0 + c, output_id);
            }
            sum0 += dst_tile_cols;
        }

        llk_pack_dest_section_done<DstSync::SyncFull>();
        llk_push_tiles<false,true>(output_id, dst_tile_rows * dst_tile_cols);
    }

    return 0;
}
}  // namespace NAMESPACE
