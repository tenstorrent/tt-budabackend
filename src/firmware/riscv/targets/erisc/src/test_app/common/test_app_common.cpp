// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "eth_init.h"
#include "overlay.h"
#include "noc.h"
#include "risc.h"
#include "noc_nonblocking_api.h"
#include "eth_routing_v2.h"
#include "eth_ss.h"
#include "test_app_common.h"

void (*rtos_context_switch_ptr)();
volatile uint32_t *RtosTable;

volatile uint32_t* const test_app_args = (volatile uint32_t*)(0x12000);
volatile uint32_t* const test_results = (volatile uint32_t*)(0x12400);
volatile uint32_t* const test_app_debug = (volatile uint32_t*)(0x12800);

node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);
volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
volatile uint32_t* debug_buf = (volatile uint32_t*)(DEBUG_BUF_ADDR);
volatile uint32_t* results_buf = (volatile uint32_t*)(RESULTS_BUF_ADDR);
volatile uint32_t* erisc_fw_ver = (volatile uint32_t*)(0x210);

void init_rtos_table() {
  // Rtos table location has moved in the past, and this test needs to be backwards compatible
  if ((*erisc_fw_ver & 0xFFFFFF) > 0x050200) {
    RtosTable = (volatile uint32_t *) 0x9020;
  } else {
    RtosTable = (volatile uint32_t *) 0x8000;
  }

  rtos_context_switch_ptr = (void (*)())RtosTable[0];
}

void risc_context_switch()
{ 
  ncrisc_noc_full_sync();
  rtos_context_switch_ptr();
  ncrisc_noc_counters_init();
}

void wait_cycles(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    asm volatile("ADDI x0, x0, 0");
  }
}

uint32_t get_tile_rnd_seed(uint32_t test_rnd_seed, uint32_t tx_eth_id, uint32_t iter, uint32_t tile_index) {
  return test_rnd_seed ^ ((tx_eth_id << 24) | ((iter & 0xFFFF) << 8) | tile_index);
}

uint8_t eth_id_get_x(uint8_t eth_id, uint8_t noc_id) {  
  uint8_t result;
  if ((eth_id % 2) == 1) {
    result = 1 + ((eth_id % 8) / 2);
  }  else {
    result = 9 - ((eth_id % 8) / 2);
  }
  return get_noc_x(result, noc_id);
}


uint8_t eth_id_get_y(uint8_t eth_id, uint8_t noc_id) {
  uint8_t result = (eth_id > 7) ? 6 : 0;
  return get_noc_y(result, noc_id);
}

uint32_t eth_get_index(uint32_t noc_x, uint32_t noc_y) {
  if (noc_x < 5) {
    return (noc_x * 2 - 1) + (noc_y == 0 ? 0 : 8);
  } else {
    return (9 - noc_x) * 2 + (noc_y == 0 ? 0 : 8);
  }
}

uint32_t eth_get_my_index() {
  uint32_t my_x = READ_REG(0xFFB2002C) & 0x3F;
  uint32_t my_y = (READ_REG(0xFFB2002C) >> 6) & 0x3F;
  return eth_get_index(my_x, my_y);
}

// 0-galaxy, 1-nebula x1, 2-nebula_x2_left, 3-nebula_x2_right, 4-mobo, 5-other
uint32_t get_other_eth_id(uint32_t eth01, bool parallel_cable_mode, bool parallel_loopback_mode, uint32_t board_type) {
  node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);

  volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
  volatile uint32_t* results_buf = (volatile uint32_t*)(RESULTS_BUF_ADDR);
  volatile uint32_t* erisc_fw_ver = (volatile uint32_t*)(0x210);

  if (parallel_cable_mode) {
    if (((config_buf->port_enable_force >> (ENABLE_CREDO_LOOPBACK_SHIFT + eth01)) & 0x1) == 0) {
      // For above 5.2.0, the remote eth_id will already be passed back during send_chip_info(), 
      // and this will apply to all loopback cases as well
      if ((*erisc_fw_ver & 0xFFFFFF) > 0x050200) {
        return results_buf[76];
      } else {
        // For below 6.0.0, remote_eth_sys_addr stores the partner port's chip_y, chip_x, noc_y, noc_x, 
        // and we can dynamically determine the correct partner ID from that
        // this will only work if the port is NOT in loopback (when not LINK_TRAIN_SUCCESS, remote_eth_sys_addr will be empty)
        uint32_t other_eth_noc_x = (my_node_info->remote_eth_sys_addr >> NOC_ADDR_LOCAL_BITS) & 0x3F;
        uint32_t other_eth_noc_y = (my_node_info->remote_eth_sys_addr >> (NOC_ADDR_LOCAL_BITS + NOC_ADDR_NODE_ID_BITS)) & 0x3F;
        return eth_get_index(other_eth_noc_x, other_eth_noc_y);
      }
    } else {
      // Keep for backwards compatibility
      // This section is specifically to handle the loopback cases, so board_type is required as well
      if (board_type == 0) {
        if ((eth01 < 4) || ((eth01 > 7) && (eth01 < 12))) {
          eth01 += 4;
        } else {
          eth01 -= 4;
        }
      } else if (board_type == 1 || board_type == 2) {
        // QSFP loopback
        if ((eth01 == 0) || (eth01 == 1)) {
          return (eth01 + 6);
        } else if ((eth01 == 6) || (eth01 == 7)) {
          return (eth01 - 6);
        // TFLY loopback
        } else if (eth01 == 14) {
          return 15;
        } else if (eth01 == 15) {
          return 14;
        }
      } else if (board_type == 3) {
        // TFLY loopback
        if (eth01 == 6) {
          return 7;
        } else if (eth01 == 7) {
          return 6;
        }
      } else if (board_type == 4) {
        // Credo loopback
        return eth01;
      } else {
        return eth01;
      }
    }
  } else if (parallel_loopback_mode) {
    return eth01;
  } else {
    eth01 = 1 - eth01;
  }
  return eth01;
}
