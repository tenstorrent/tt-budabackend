// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_stream_pack_multi_out_params.h"
#include "llk_pack_api.h"
#include "llk_io_pack.h"

namespace NAMESPACE {
//    uint kernel_main(const void* hlk_args=nullptr, const void* llk_args=nullptr, const uint loop_count=0)
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto pack_params = static_cast<const llk_pack_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_pack_hw_configure(pack_params);
    llk_pack_init(16);

    llk_setup_outputs();

    const int num_input_tiles = hlk_num_input_tiles;

    const uint num_outputs = hlk_num_outputs;
    const uint* num_output_tiles = hlk_num_output_tiles;
    uint tile_index = 0;
    uint output = 0;

    llk_pack_dest_init<SyncTile16>();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (uint b = 0; b < num_outputs; ++b) {
            llk_wait_for_free_tiles<false, false, true>(16 + b, num_output_tiles[b]);
        }

        for (uint t = 0; t < num_input_tiles; ++t) {
            tile_index = tile_info[t].tile_index_within_out_buf;
            output = tile_info[t].buffer_index;
            llk_packer_wait_for_math_done();
            llk_pack<true, DstSync::SyncTile16>(0, output, tile_index);
            llk_pack_dest_section_done<DstSync::SyncTile16>();
        }

        for (uint b = 0; b < num_outputs; ++b) {
            llk_push_tiles<false, true>(16 + b, num_output_tiles[b]);
        }
    }

    return 0;
}
}  // namespace NAMESPACE
