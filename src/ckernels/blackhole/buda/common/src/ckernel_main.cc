// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

// This c-file's purpose is:
// 1) include the generated list of kernels
//      The files hold run_kernel() definition and inline kernel_main functions for every ckernel
//      Need to make sure no other file includes these lists since it also include global parameter definitions
// 2) instantiate global variables


#include "ckernel_globals.h"

#if defined(UCK_CHLKC_UNPACK) || defined(UCK_CHLKC_MATH) || defined(UCK_CHLKC_PACK)
#include "chlkc_list.h"
#else
#include "ckernel_list.h"
#endif

// Global vars
uint32_t unp_cfg_context = 0;
uint32_t pack_sync_tile_dst_ptr = 0;
uint32_t math_sync_tile_dst_index = 0;
volatile uint32_t tt_l1_ptr l1_buffer[16] __attribute__ ((section (".text#"))) __attribute__ ((aligned (16)));

// used for tracking which stage of the TopK op is currently in the replay buffer
// when ==  0 : no stage is in the buffer
// when == -1 : the local sort stage is in the buffer
// when >=  1 : the corresponding loop of the rebuild stage is in the buffer
int32_t topk_replay_init = 0;