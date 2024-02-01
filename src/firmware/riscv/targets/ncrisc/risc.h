// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _NCRISC_H_
#define _NCRISC_H_

#include <stdint.h>

#include "noc_parameters.h"

typedef struct active_stream_info_t {
  uint8_t  stream_id;
  uint8_t  unused0;
  uint16_t flags;
} active_stream_info_t;

inline __attribute__((always_inline)) void RISC_POST_STATUS(uint32_t status) {
  volatile uint32_t* ptr = (volatile uint32_t*)(NOC_CFG(ROUTER_CFG_2));
  ptr[0] = status;
}

inline __attribute__((always_inline)) void RISC_POST_DEBUG(uint32_t info) {
#ifdef ENABLE_NCRISC_DEBUG_POST_CODES
  volatile uint32_t* ptr = (volatile uint32_t*)(NOC_CFG(ROUTER_CFG_2));
  ptr[0] = info;
#endif
}

void set_risc_reset_vector();
uint8_t risc_streams_kernels_done();
void risc_initialize_tile_counters(uint32_t num_kernel_inputs, uint32_t num_kernel_outputs);

#define RISC_EPOCH_INFO_PTR EPOCH_INFO_PTR
#define RISC_L1_EPOCH_Q_PTR  L1_EPOCH_Q_PTR
#define RISC_DRAM_POLLING_CTRL_PTR DRAM_POLLING_CTRL_PTR
#define RISC_EPOCH_RUNTIME_CONFIG_PTR EPOCH_RUNTIME_CONFIG_PTR

#endif
