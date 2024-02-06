// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include "noc_parameters.h"
#include "noc_nonblocking_api.h"
#include "risc.h"
#include "unpack_pack_stream_intf.h"
#include "dram_stream_intf.h"
#include "risc_common.h"
#include "epoch.h"

////

const uint32_t DRAM_HEADER_LAST = 7; // last byte of the header
const uint32_t PACKET_END_MARKER = 0xabcd1234;

const uint32_t DRAM_STREAM_1 = 4; // Actually unused in GS
const uint32_t DRAM_STREAM_2 = 5; // Actually unused in GS

void init_tile_clear();
void wait_till_tile_clear_done(uint32_t stream_id);
void process_tile_clearing(kernel_input_stream_state_t* input_stream_state, uint32_t streams_to_clear);

int get_epoch_table_x(int my_x, int my_y) __attribute__((const));
int get_epoch_table_y(int my_x, int my_y) __attribute__((const));
int get_epoch_index_x(int my_x) __attribute__((const));
int get_epoch_index_y(int my_y) __attribute__((const));

inline __attribute__((always_inline)) uint16_t op_pack_tiles_ptr_add(uint16_t a, uint16_t b) {
  return a + b;
}

inline __attribute__((always_inline)) uint16_t op_pack_tiles_ptr_sub(uint16_t a, uint16_t b) {
  return a - b;
}

inline __attribute__((always_inline)) bool addr_is_pcie(uint64_t dram_ptr_addr) {
  uint32_t x = NOC_UNICAST_ADDR_X(dram_ptr_addr);
  uint32_t y = NOC_UNICAST_ADDR_Y(dram_ptr_addr);
  return x == 0 && y == 4;
}

inline void set_noc_trans_table(uint32_t noc, uint8_t& noc_trans_table_en, uint8_t& my_logical_x, uint8_t& my_logical_y) {
  noc_trans_table_en = false;
}

inline __attribute__((always_inline)) bool check_packet_end_marker(uint32_t l1_addr) {
  volatile uint32_t tt_l1_ptr * l1_ptr = (volatile uint32_t tt_l1_ptr *)l1_addr;
  uint32_t val = l1_ptr[0];
  return (val != PACKET_END_MARKER);
}

inline __attribute__((always_inline)) void set_packet_end_marker(uint32_t l1_addr) {
  volatile uint32_t tt_l1_ptr * l1_ptr = (volatile uint32_t tt_l1_ptr*)l1_addr;
  l1_ptr[0] = PACKET_END_MARKER;
}

inline __attribute__((always_inline)) bool header_reads_flushed(uint32_t noc, uint32_t transaction_id, volatile uint32_t tt_l1_ptr * l1_ptr_addr) {
  return (ncrisc_noc_reads_flushed(noc, transaction_id) || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST]))));
}

inline __attribute__((always_inline)) uint32_t get_total_in_flight_tiles(dram_output_stream_state_t* curr_dram_output_stream_state) {
  uint32_t total_in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles;
  return total_in_flight_tiles;
}

void dram_input_stream_issue_scatter_read_init(uint32_t data_rec_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_dest_addr, uint32_t& transaction_id);
bool dram_input_stream_check_next_chunk_flushed(uint32_t input_noc, uint32_t chunk_pending_start_addr, uint32_t chunk_size_bytes, uint32_t scatter_chunk_size_bytes, uint32_t& transaction_id);
void risc_wait_for_cmd_buf(uint32_t noc, uint32_t cmd_buf);
void risc_dram_write_init(uint32_t dram_stream);
void risc_dram_write (uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t noc, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t len_tiles, uint32_t vc, uint32_t stream_msg_info_buf_addr, uint32_t transaction_id);
bool risc_dram_write_ok(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t output_noc);
bool risc_dram_writes_sent(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream);
void replicate(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id);
void __attribute__((section("code_l1"))) replicate_l1(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id);
void process_dram_write(
  uint32_t &num_dram_output_streams, dram_output_stream_state_t *dram_output_stream_state, uint32_t &dram_ptr_update_cnt, uint32_t &total_tiles_to_clear
);
void process_dram_write_clear(uint32_t &num_dram_output_streams, dram_output_stream_state_t *dram_output_stream_state, uint32_t& total_tiles_to_clear);