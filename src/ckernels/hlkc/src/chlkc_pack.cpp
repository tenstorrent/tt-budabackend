// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include "llk_pack_common.h" 
#include "llk_pack.h" 
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

void pack_main(const struct hlk_args_t *args,const void *llk_params,const int outer_loop_cnt)
{
const auto llk_args = static_cast < const struct llk_pack_params_t * >  (llk_params);
llk_pack_hw_configure(llk_args);
llk_pack_init(16);
llk_setup_outputs();
int __outer_loop_iter;
for (__outer_loop_iter = 0; __outer_loop_iter < outer_loop_cnt; __outer_loop_iter += 1) {
  llk_pack_dest_init<DstSync::SyncFull, DstTileFaceLayout::ColMajor>();
  llk_packer_wait_for_math_done();
  llk_wait_for_free_tiles(16,args -> out_block_tile_cnt);
  for (int i = 0; i < args -> out_block_tile_cnt; ++i) {
    llk_pack(i,16);
  }
  llk_push_tiles(16,args -> out_block_tile_cnt);
  llk_pack_dest_section_done<SyncFull>();
}
}
}
