// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define WRITE_REG(addr, val) ((*((volatile uint32_t*)((addr)))) = (val))
#define READ_REG(addr)       (*((volatile uint32_t*)((addr))))

extern void (*rtos_context_switch_ptr)();
extern volatile uint32_t *RtosTable;

extern volatile uint32_t* const test_app_args;
extern volatile uint32_t* const test_results;
extern volatile uint32_t* const test_app_debug;

extern node_info_t* const my_node_info;
extern volatile boot_params_t* const config_buf;
extern volatile uint32_t* debug_buf;
extern volatile uint32_t* results_buf;
extern volatile uint32_t* erisc_fw_ver;

// All the functions defined in test_app_common.cpp are declared here.
void init_rtos_table();
void risc_context_switch();
void wait_cycles(uint32_t n);
uint32_t get_tile_rnd_seed(uint32_t test_rnd_seed, uint32_t tx_eth_id, uint32_t iter, uint32_t tile_index);
uint8_t eth_id_get_x(uint8_t eth_id, uint8_t noc_id);
uint8_t eth_id_get_y(uint8_t eth_id, uint8_t noc_id);
uint32_t eth_get_index(uint32_t noc_x, uint32_t noc_y);
uint32_t eth_get_my_index();
uint32_t get_other_eth_id(uint32_t eth01, bool parallel_cable_mode, bool parallel_loopback_mode, uint32_t board_type);

inline void fast_prng_init(volatile uint32_t* start_addr, uint32_t num_16B_words, uint32_t seed) {

  uint32_t tmp0;
  uint32_t tmp1;
  uint32_t tmp2;
  uint32_t tmp3;

  volatile uint32_t* dest = start_addr;
  volatile uint32_t* dest_end = start_addr + (num_16B_words * 4);
  tmp3 = seed;
  while (dest != dest_end) {
    tmp0 = prng_next(tmp3);
    tmp1 = prng_next(tmp0);
    tmp2 = prng_next(tmp1);
    tmp3 = prng_next(tmp2);
    dest[0] = tmp0;
    dest[1] = tmp1;
    dest[2] = tmp2;
    dest[3] = tmp3;
    dest += 4;
  }
}
