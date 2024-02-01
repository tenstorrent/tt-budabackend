// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include "tensix.h"
#include "tensix_types.h"
#include "noc.h"

typedef struct active_stream_info_t {
  uint8_t  stream_id;
  uint8_t  active_streams_idx;
  uint8_t  start_phase_num_cfg_regs;
  uint8_t  dram_prefetch_stream_need_restart;
  uint16_t unused0;
  uint16_t flags;
  uint32_t epoch_iters_remaining; 
} active_stream_info_t;


inline void RISC_POST_STATUS(uint32_t status) {
  volatile uint32_t* ptr = (volatile uint32_t*)(NOC_CFG(ROUTER_CFG_2) + NOC_INSTANCE_OFFSET);
  ptr[0] = status;
}

inline __attribute__((always_inline)) void RISC_POST_DEBUG(uint32_t info) {
}

void set_risc_reset_vector();
void risc_reset_check();

#define RISC_EPOCH_INFO_PTR EPOCH_INFO_PTR
#define RISC_DRAM_POLLING_CTRL_PTR DRAM_POLLING_CTRL_PTR
#define RISC_EPOCH_RUNTIME_CONFIG_PTR EPOCH_RUNTIME_CONFIG_PTR

// FW loop info structure, 16b fields should be enough
struct LoopInfo {
    uint32_t curr_iter = 0;
    uint32_t last_iter = 0;  // loop_count-1, so we fit power-of-2 numbers
    uint32_t epoch_ptr = 0;
};