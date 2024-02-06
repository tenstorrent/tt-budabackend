// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include "llk_math_common.h" 
#include "llk_math_matmul.h" 
namespace NAMESPACE
{

struct hlk_args_t 
{
int block_tile_dim;
int dst_tile_rows;
int dst_tile_cols;
int block_cnt;
int in0_block_tile_cnt;
int in1_block_tile_cnt;
int out_block_tile_cnt;
}
;

void math_main(const struct hlk_args_t *args,const void *llk_params,const int outer_loop_cnt)
{
const auto llk_args = static_cast < const struct llk_math_matmul_params_t * >  (llk_params);
llk_math_matmul_init<MATH_FIDELITY>();
int __outer_loop_iter;
for (__outer_loop_iter = 0; __outer_loop_iter < outer_loop_cnt; __outer_loop_iter += 1) {
  llk_math_pack_sync_init<SyncFull>();
  llk_math_wait_for_dest_available<SyncFull>();
  for (int b = 0; b < args -> block_cnt; ++b) {
    int dst_tile_index = 0;
    int in0_block_tile_index = 0;
    for (int r = 0; r < args -> dst_tile_rows; ++r) {
      for (int c = 0; c < args -> dst_tile_cols; ++c) {
        int in1_block_tile_index = 0;
        for (int i = 0; i < args -> block_tile_dim; ++i) {
          llk_math_matmul<MATH_FIDELITY>(dst_tile_index);
          in1_block_tile_index += args -> dst_tile_cols;
        }
        dst_tile_index++;
      }
      in0_block_tile_index += args -> block_tile_dim;
    }
  }
  llk_math_dest_section_done<SyncFull>();
}
}
}
