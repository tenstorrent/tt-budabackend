// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _UNPACK_PACK_STREAM_INTF_H_
#define _UNPACK_PACK_STREAM_INTF_H_

#include "epoch.h"
#include "stream_interface.h"

#define BRISC_PACKER_SENDING_IN_PROGRESS 0x8000

const uint32_t TILE_CLEAR_STREAM_ID = 3;
const int32_t TILE_CLEAR_THRESHOLD = 32;

#define BLOB_CFG_DW(val, reg_index) ((val << 8) | (reg_index))

typedef struct kernel_input_stream_state_t {
  uint8_t  input_idx; 
  uint8_t  epoch_done; 
  uint16_t stream_id;
  uint8_t prev_ack_thresh;
  uint8_t eth_fw_stream;
  uint16_t unused2;
  uint32_t epoch_tiles_remaining_to_clear;
  uint16_t epoch_tiles_cleared;
  uint16_t prev_phases_tiles_received;
  uint32_t num_iter_tiles;
  uint32_t tiles_to_clear;
  uint16_t  fork_prev_pushed_num_tiles;
  uint8_t  no_hw_clearing;
  uint8_t  num_fork_streams;
  uint8_t  fork_stream_ids[EPOCH_MAX_OUTPUT_FORKS];
#ifdef ERISC
  uint32_t msg_rd_addr;
  uint32_t msg_wr_addr;
#endif
  volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr;
  volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr;
  struct kernel_input_stream_state_t* next;
} kernel_input_stream_state_t;


typedef struct kernel_output_stream_state_t {
  uint8_t eth_fw_stream;
  uint8_t output_idx;
  uint16_t unused1;
  uint8_t  active_streams_idx;
  uint8_t epoch_done; 
  uint16_t stream_id;
  uint32_t scatter_inner_loop_done;
  uint32_t scatter_inner_loop_count;
  uint16_t num_mblock_buffering;
  uint16_t mblock_buffering_count;
  uint16_t num_msgs_in_block;
  uint8_t  skip_processing;
  uint8_t  num_fork_streams;
  uint8_t  fork_idxs[EPOCH_MAX_OUTPUT_FORKS];
  volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr;
  volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr;
  struct kernel_output_stream_state_t* next;
} kernel_output_stream_state_t;


void risc_unpack_pack_stream_handler_init(
  uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
  uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state
);

void risc_unpack_pack_stream_handler_loop(
  uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
  uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state,
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info,
  bool erisc = false
);

#ifdef ERISC
void __attribute__((section("code_l1"))) __attribute__((noinline)) risc_stream_resart_check
#else
void risc_stream_resart_check
#endif
(
  uint32_t &stream_restart_check_cnt,
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info
);

void risc_all_streams_done_check(
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info
);

#endif

