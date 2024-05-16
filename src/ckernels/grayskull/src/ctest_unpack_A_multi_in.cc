// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "hlks/inc/hlk_api.h"
#include "llk_param_structs.h"
#include "ctest_unpack_A_multi_in_params.h"
#include "llk_unpack_api.h"

#ifndef SRC_B_BROADCAST_TYPE
#define SRC_B_BROADCAST_TYPE NONE
#endif

#ifndef TRANSPOSE_XY
#define TRANSPOSE_XY 0
#endif

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_A_params = static_cast<const llk_unpack_A_params_t*>(&llk_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    const int block_cnt = hlk_params->block_cnt;
    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;

    const int num_ouput_tiles = hlk_num_output_tiles;

    const uint num_inputs = hlk_num_inputs;
    const uint* num_input_tiles = hlk_num_input_tiles;
    uint tile_index = 0;
    uint input = 0;

    llk_unpack_A_hw_configure(unpack_A_params);
    llk_unpack_A_init<BroadcastType::SRC_B_BROADCAST_TYPE, TRANSPOSE_XY>();

    llk_setup_operands();

    for (uint batch = 0; batch < loop_count; ++batch) {
        for (uint b = 0; b < num_inputs; ++b) {
            llk_wait_tiles(0 + b, num_input_tiles[b]);
        }

        for (uint t = 0; t < num_ouput_tiles; ++t) {
            tile_index = tile_info[t].tile_index_within_in_buf;
            input = tile_info[t].buffer_index;
            llk_unpack_A<BroadcastType::SRC_B_BROADCAST_TYPE, TRANSPOSE_XY>(input, tile_index);
        }

        for (uint b = 0; b < num_inputs; ++b) {
            llk_pop_tiles(0 + b, num_input_tiles[b]);
        }
    }

    return 0;
}

}  // namespace NAMESPACE
