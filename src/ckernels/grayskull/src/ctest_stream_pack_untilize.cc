
#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_stream_pack_untilize_params.h"
#include "llk_pack_api.h"
#include "llk_io_pack.h"
#include "ckernel_ops.h"

#ifndef SYNC_TYPE
#define SYNC_TYPE SyncTile16
#endif
namespace NAMESPACE {
//    uint kernel_main(const void* hlk_args=nullptr, const void* llk_args=nullptr, const uint loop_count=0)
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto pack_params = static_cast<const llk_pack_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_pack_hw_configure<true>(pack_params);
    llk_pack_init<false>(16); // use unpacker to untilize
    // custom mop override
    volatile uint *mop_cfg = reinterpret_cast<volatile uint *>(TENSIX_MOP_CFG_BASE);
    mop_sync(); // wait until previous mops have completed
    mop_cfg[3] = TT_OP_NOP; // skip header write

    llk_setup_outputs();

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;

    llk_pack_dest_init<DstSync::SYNC_TYPE, DstTileFaceLayout::RowMajor, false>(); // use unpacker to untilize

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (int b = 0; b < block_cnt; ++b) {
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    llk_packer_wait_for_math_done();
                    llk_wait_for_free_blocks(16, 1);
                    //llk_pack<false, DstSync::SYNC_TYPE, true>(0, 16);

                    // custom untilize pack
                    std::uint8_t output_id = get_output_id(16);
                    constexpr std::uint8_t OUTPUT_BASE_ID = (std::uint8_t) get_output_base_id();
                    std::uint16_t pack_tile_addr = outputs[output_id].f.fifo_wr_ptr + outputs[output_id].f.fifo_wr_tile_ptr - 1;
                    outputs[output_id].f.fifo_wr_tile_ptr += GET_L1_TILE_SIZE<true>((std::uint8_t)pack_dst_format[OUTPUT_BASE_ID]);
                    program_packer_destination(pack_tile_addr, OUTPUT_BASE_ID);
                    mop_run(1, 1);

                    llk_push_blocks(16, 1);
                    llk_pack_dest_section_done<DstSync::SYNC_TYPE>();
                }
            }
        }
    }

    return 0;
}
}  // namespace NAMESPACE
