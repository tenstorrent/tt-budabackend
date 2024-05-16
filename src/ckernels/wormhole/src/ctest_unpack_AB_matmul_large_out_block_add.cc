// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "llk_param_structs.h"
#include "ctest_unpack_AB_matmul_large_out_block_add_params.h"
#include "llk_unpack_AB_matmul.h"
#include "llk_unpack_A.h"
#include "llk_unpack_AB.h"

namespace NAMESPACE {
uint kernel_main(uint* params) {
    const auto hlk_params = static_cast<const hlk_args_t*>(&hlk_args);
    const auto unpack_AB_matmul_params =
        static_cast<const llk_unpack_AB_matmul_params_t*>(&llk_args.llk_unpack_AB_matmul_args);
    const auto unpack_AB_params = static_cast<const llk_unpack_AB_params_t*>(&llk_args.llk_unpack_AB_args);
    const auto loop_count = static_cast<const uint>(arg_loop_count);

    llk_setup_operands();

    const int dst_tile_rows = hlk_params->dst_tile_rows;
    const int dst_tile_cols = hlk_params->dst_tile_cols;
    const int block_cnt = hlk_params->block_cnt;
    const int block_tile_dim = hlk_params->block_tile_dim;
    uint sum0 = 0, sum1 = 0;

    for (uint batch = 0; batch < loop_count; ++batch) {
        llk_unpack_AB_matmul_hw_configure(unpack_AB_matmul_params);

        for (int b = 0; b < block_cnt; ++b) {
            llk_wait_tiles(0, dst_tile_rows * block_tile_dim);
            llk_wait_tiles(1, dst_tile_cols * block_tile_dim);

            if (b > 0) {
                // Configure MOP for unpack A2D op with face transpose
                // Matmul layout in dest is ColMajor while input tiles are in RowMajor format
                // Unpack will transpose faces in XY which is equivalent to Row->Col major format change
                llk_unpack_A_init<BroadcastType::NONE>(true);

                // Load intermediate operand into dest
                llk_wait_tiles(24, dst_tile_rows * dst_tile_cols);
                sum0 = 0;
                for (int r = 0; r < dst_tile_rows; ++r) {
                    for (int c = 0; c < dst_tile_cols; ++c) {
                        llk_unpack_A<BroadcastType::NONE>(24, sum0 + c, true);
                    }
                    sum0 += dst_tile_cols;
                }
                llk_pop_tiles(24, dst_tile_rows * dst_tile_cols);
            }

            // Configure MOP for matmul op
            llk_unpack_AB_matmul_init(0, 1);

            sum0 = 0;
            for (int r = 0; r < dst_tile_rows; ++r) {
                for (int c = 0; c < dst_tile_cols; ++c) {
                    sum1 = 0;
                    for (int i = 0; i < block_tile_dim; ++i) {
                        llk_unpack_AB_matmul(0, 1, sum0 + i, sum1 + c);
                        sum1 += dst_tile_cols;
                    }
                }
                sum0 += block_tile_dim;
            }

            llk_pop_tiles(0, dst_tile_rows * block_tile_dim);
            llk_pop_tiles(1, dst_tile_cols * block_tile_dim);
        }

        sum0 = 0;

        // Configure MOP for eltwise binary add
        llk_unpack_AB_hw_configure(unpack_AB_params);
        llk_unpack_AB_init(0, 1);
        llk_wait_tiles(24, dst_tile_rows * dst_tile_cols);
        llk_wait_tiles(25, dst_tile_rows * dst_tile_cols);

        for (int r = 0; r < dst_tile_rows; ++r) {
            for (int c = 0; c < dst_tile_cols; ++c) {
                llk_unpack_AB(24, 25, sum0 + c, sum0 + c);
            }
            sum0 += dst_tile_cols;
        }

        llk_pop_tiles(24, dst_tile_rows * dst_tile_cols);
        llk_pop_tiles(25, dst_tile_rows * dst_tile_cols);
    }

    return 0;
}

}  // namespace NAMESPACE
