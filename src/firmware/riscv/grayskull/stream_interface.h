// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _STREAM_INTERFACE_H_
#define _STREAM_INTERFACE_H_

#include "noc_overlay_parameters.h"
#include "noc_parameters.h"
#include "noc_nonblocking_api.h"

// Low-level chip-dependent stream/NOC functions

#define STREAM_PTR_REG_MASK ((uint32_t)0xFFFF)
#define EPOCH_SHIFT 15
#define MAX_TILES_MSG_INFO_BUF_PER_PHASE 2048
#define USE_2K_TILE_HEADER_BUFFER_RESET

inline uint32_t NOC1_X_ID(uint32_t x) {
  return NOC_X_SIZE - 1 - x;
}

inline uint32_t NOC1_Y_ID(uint32_t y) {
  return NOC_Y_SIZE - 1 - y;
}


inline __attribute__((always_inline)) void stream_phase_blob_run(uint32_t stream_id, uint32_t blob_start_addr, uint32_t start_phase_num_cfg_regs) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, blob_start_addr);
  NOC_STREAM_WRITE_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, start_phase_num_cfg_regs << NEXT_PHASE_NUM_CFG_REG_WRITES);
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MISC_CFG_REG_INDEX, (0x1 << PHASE_AUTO_CONFIG) | (1 << NEXT_PHASE_SRC_CHANGE) | (1 << NEXT_PHASE_DEST_CHANGE));
}

inline __attribute__((always_inline)) void stream_phase_blob_run_offset(uint32_t stream_id, uint32_t blob_base_addr, uint32_t blob_start_addr, uint32_t blob_size) {
  uint32_t blob_offset = NOC_STREAM_READ_REG(stream_id, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX) - blob_base_addr;
  while (blob_offset >= blob_size)
    blob_offset -= blob_size;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, blob_start_addr + blob_offset);
  uint32_t misc_cfg_reg = NOC_STREAM_READ_REG(stream_id, STREAM_MISC_CFG_REG_INDEX);
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MISC_CFG_REG_INDEX, (0x1 << PHASE_AUTO_CONFIG) | misc_cfg_reg);
}

inline __attribute__((always_inline)) uint32_t stream_get_auto_cfg_ptr(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_get_auto_cfg_header(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_get_auto_cfg_header_phase_num_cfg_regs(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX) >> (NEXT_PHASE_NUM_CFG_REG_WRITES-PHASE_NUM_INCR_WIDTH);
}

inline __attribute__((always_inline)) void stream_set_auto_cfg_header(uint32_t stream_id, uint32_t val) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, val);
}

inline __attribute__((always_inline)) uint32_t stream_phase_is_active(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_WAIT_STATUS_REG_INDEX, MSG_FWD_ONGOING);
}

inline __attribute__((always_inline)) uint32_t stream_get_curr_phase(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_CURR_PHASE_REG_INDEX);
}

inline __attribute__((always_inline)) void set_fork_scatter_inner_loop_count(uint32_t stream_id, uint32_t val) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_REG_INDEX, val);
}

inline __attribute__((always_inline)) uint32_t get_fork_scatter_inner_loop_count(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_REG_INDEX);
}

inline __attribute__((always_inline)) void set_fork_num_msgs_in_block(uint32_t stream_id, uint32_t val) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX, val);
}

inline __attribute__((always_inline)) uint32_t get_fork_num_msgs_in_block(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX);
}

inline __attribute__((always_inline)) bool stream_phase_id_is_active(uint32_t stream_id, uint32_t phase_id) {
  uint32_t curr_phase = stream_get_curr_phase(stream_id);
  bool phase_active = stream_phase_is_active(stream_id);
  return (curr_phase == phase_id) && phase_active;
}

inline __attribute__((always_inline)) uint32_t stream_phase_advance_wait(uint32_t stream_id) {
  uint32_t advance_wait = NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_WAIT_STATUS_REG_INDEX, WAIT_SW_PHASE_ADVANCE_SIGNAL);
  uint32_t num_tiles_pending = NOC_STREAM_READ_REG(stream_id, STREAM_DEBUG_STATUS_REG_INDEX+8) >> MEM_WORD_ADDR_WIDTH;
  return advance_wait && (num_tiles_pending == 0);
}

inline __attribute__((always_inline)) uint32_t stream_get_input_noc(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, INCOMING_DATA_NOC);
}

inline __attribute__((always_inline)) void stream_get_remote_src_coord(uint32_t stream_id, uint32_t& x, uint32_t& y) {
  uint32_t val = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_REG_INDEX);
  x = NOC_STREAM_GET_REG_FIELD(val, STREAM_REMOTE_SRC_X);
  y = NOC_STREAM_GET_REG_FIELD(val, STREAM_REMOTE_SRC_Y);
}

inline __attribute__((always_inline)) uint32_t stream_get_output_noc(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, OUTGOING_DATA_NOC);
}

inline __attribute__((always_inline)) uint32_t stream_get_output_unicast_vc(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, UNICAST_VC_REG);
}

inline __attribute__((always_inline)) void stream_get_remote_dest_coord(uint32_t stream_id, uint32_t& x, uint32_t& y) {
  uint32_t val = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX);
  x = NOC_STREAM_GET_REG_FIELD(val, STREAM_REMOTE_DEST_X);
  y = NOC_STREAM_GET_REG_FIELD(val, STREAM_REMOTE_DEST_Y);
}

inline __attribute__((always_inline)) uint32_t stream_get_msg_info_rd_ptr(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_PTR_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_get_data_buf_addr(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_BUF_START_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_get_data_buf_size(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SIZE_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_phase_next_recved_tile_addr(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_NEXT_RECEIVED_MSG_ADDR_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_phase_next_recved_tile_size(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_NEXT_RECEIVED_MSG_SIZE_REG_INDEX);
}

inline __attribute__((always_inline)) uint32_t stream_phase_tiles_received(uint32_t stream_id, uint32_t msg_info_buf_start_addr) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX) - msg_info_buf_start_addr;
}


inline __attribute__((always_inline)) uint32_t stream_rec_endpoint_get_phase_tiles_count(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX) & 0xffff;  // used as scratch reg for receiver endpoint streams
}


inline __attribute__((always_inline)) void stream_rec_endpoint_set_phase_tiles_count(uint32_t stream_id, uint32_t val) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX) & ~0xffff;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX, (rmw | val));  // used as scratch reg for receiver endpoint streams
}


inline __attribute__((always_inline)) uint32_t stream_src_endpoint_get_phase_tiles_count(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX) & 0xffff;  // used as scratch reg for source endpoint streams
}


inline __attribute__((always_inline)) void stream_src_endpoint_set_phase_tiles_count(uint32_t stream_id, uint32_t val) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX) & ~0xffff;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX, (rmw | val));  // used as scratch reg for source endpoint streams
}


inline __attribute__((always_inline)) uint32_t stream_get_buf_space_available_words(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);
}

inline __attribute__((always_inline)) void stream_signal_flushed_tiles(uint32_t stream_id, uint32_t num_tiles, uint32_t num_words) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_INC_REG_INDEX, (num_words << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE) | num_tiles);
}

inline __attribute__((always_inline)) bool stream_is_dram_read_opt_enabled(uint32_t stream_id) {
  return !NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, NEXT_PHASE_SRC_CHANGE);
}

inline __attribute__((always_inline)) bool stream_next_phase_src_change(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, NEXT_PHASE_SRC_CHANGE) || !NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, PHASE_AUTO_CONFIG);
}

inline __attribute__((always_inline)) int stream_get_curr_phase_num_msgs(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, PHASE_NUM_INCR); // is actually CURR_PHASE_NUM_MSGS, due to bug in reads
}

inline __attribute__((always_inline)) void stream_set_curr_phase_num_msgs(uint32_t stream_id, uint32_t val) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX) & ~0xFFF;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, ((rmw << PHASE_NUM_INCR_WIDTH) | (val << CURR_PHASE_NUM_MSGS)));
}

// used by unpacker fracture
inline __attribute__((always_inline)) void stream_relay_tiles(uint32_t stream_id, uint32_t num_tiles, uint32_t num_words) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_INC_REG_INDEX, (num_words << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE) | num_tiles);
}

// used by packer
inline uint32_t stream_get_free_words(uint32_t stream_id) {
  uint32_t wait_status = NOC_STREAM_READ_REG(stream_id, STREAM_WAIT_STATUS_REG_INDEX);
  uint32_t tiles_left_in_phase = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX) & 0xffff; // used as scratch reg for source endpoint streams
  uint32_t buf_space_available = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);
  wait_status &= (0x1 << MSG_FWD_ONGOING);
  return (wait_status && tiles_left_in_phase) ? buf_space_available : 0;
}

inline uint32_t stream_should_packer_reset_pointers(uint32_t stream_id) {
  uint32_t should_packer_reset_pointers = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_REG_INDEX);
  if (should_packer_reset_pointers)
    NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_REG_INDEX, 0); // used as scratch reg for source endpoint streams
  return should_packer_reset_pointers;
}

inline uint32_t stream_dram_write_should_reset_pointers(uint32_t stream_id) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX);
  uint32_t should_reset_pointers = rmw >> 16;
  if (should_reset_pointers)
    NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_DEST_REG_INDEX, (rmw & 0xffff));  // used as scratch reg for receiver endpoint streams
  return should_reset_pointers;
}

inline uint32_t stream_dram_read_should_reset_pointers(uint32_t stream_id) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX);
  uint32_t should_reset_pointers = rmw >> 16;
  if (should_reset_pointers)
    NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX, (rmw & 0xffff));  // used as scratch reg for receiver endpoint streams
  return should_reset_pointers;
}

template <bool fracture = false, bool with_rd_ptr = false>
static __attribute__((unused)) __attribute__((noinline)) bool stream_get_push_flushed(uint32_t stream_id, uint32_t exp_rd_ptr=0) {
  uint32_t prev_phase = stream_get_curr_phase(stream_id);
  uint32_t wait_status = NOC_STREAM_READ_REG(stream_id, STREAM_WAIT_STATUS_REG_INDEX);
  wait_status &= (0x1 << MSG_FWD_ONGOING);

  if (wait_status) {
    uint32_t buf_size = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SIZE_REG_INDEX);
    uint32_t buf_space_available = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);
    uint32_t num_tiles;
    if constexpr (fracture)
      num_tiles = 0;
    else
      num_tiles = stream_get_curr_phase_num_msgs(stream_id);
    uint32_t rd_ptr;
    if constexpr (with_rd_ptr)
      rd_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_RD_PTR_REG_INDEX);
    uint32_t cur_phase = stream_get_curr_phase(stream_id);
    if (cur_phase == prev_phase) {
      if constexpr (with_rd_ptr)
        return (buf_space_available != 0 && rd_ptr == exp_rd_ptr) || (buf_size == buf_space_available && num_tiles > 0); // For this case we might be resending next phase so we need the num_tiles > 0 check
      else if constexpr (fracture) 
        return buf_size == buf_space_available; // We dont need num_tiles > 0 as there is no resend concept for fracture
      else
        return buf_size == buf_space_available && num_tiles > 0; // For this case we might be resending next phase so we need the num_tiles > 0 check
    }
  }

  return stream_phase_advance_wait(stream_id);
}

inline __attribute__((always_inline)) uint32_t stream_get_buf_space_available(uint32_t stream_id) {
  uint32_t buf_space_available = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);
  return buf_space_available;
}

// used by packer
inline __attribute__((always_inline)) void stream_push_tiles(uint32_t stream_id, uint32_t num_tiles, uint32_t num_words) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX);
  uint32_t tiles_left_in_phase = rmw & 0xffff;
  rmw = rmw & ~0xffff;
  tiles_left_in_phase -= num_tiles;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX, (rmw | tiles_left_in_phase)); // used as scratch reg for source endpoint streams
  NOC_STREAM_WRITE_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_INC_REG_INDEX, (num_words << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE) | num_tiles);
}

inline void stream_set_tiles_left_in_phase(uint32_t stream_id, uint32_t num_tiles) {
  uint32_t rmw = NOC_STREAM_READ_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX);
  uint32_t tiles_left_in_phase = rmw & 0xffff;
  rmw = rmw & ~0xffff;
  tiles_left_in_phase -= num_tiles;
  NOC_STREAM_WRITE_REG(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX, (rmw | tiles_left_in_phase)); // used as scratch reg for source endpoint streams
}

#define STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE(dest_num, words_free_inc) \
  (((dest_num) << REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM) | ((words_free_inc) << REMOTE_DEST_BUF_WORDS_FREE_INC))


inline __attribute__((always_inline)) bool stream_is_receiver_endpoint(uint32_t stream_id) {
  return NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, RECEIVER_ENDPOINT);
}

inline __attribute__((always_inline)) void stream_receiver_endpoint_single_clear_op(uint32_t stream_id, uint32_t num_tiles) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MSG_INFO_CLEAR_REG_INDEX, num_tiles);
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MSG_DATA_CLEAR_REG_INDEX, num_tiles);
}

inline __attribute__((always_inline)) uint32_t stream_tiles_outstanding(uint32_t stream_id) {
  return NOC_STREAM_READ_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_REG_INDEX);
}

inline void stream_receiver_endpoint_tiles_clear(uint32_t stream_id, uint32_t num_tiles) {
  while (num_tiles > 0) {
    uint32_t num_to_clear = (num_tiles == 1) ? 1 : 2;

    // Bug fix for streams. Flow ctrl messages are sent out of order, must clear one message at the end of phase.
    int32_t num_msgs_left_in_phase = stream_get_curr_phase_num_msgs(stream_id);
    if (num_msgs_left_in_phase <= 2)
      num_to_clear = 1;

    stream_receiver_endpoint_single_clear_op(stream_id, num_to_clear);
    num_tiles -= num_to_clear;
  }
}

inline __attribute__((always_inline)) void stream_reset(uint32_t stream_id) {
  uint32_t val = NOC_STREAM_READ_REG(stream_id, STREAM_MISC_CFG_REG_INDEX);
  val &= (~(1<<PHASE_AUTO_CONFIG));
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MISC_CFG_REG_INDEX, val); // disable auto-config
  NOC_STREAM_WRITE_REG(stream_id, STREAM_RESET_REG_INDEX, 0x1);
}

inline __attribute__((always_inline)) void stream_force_next_phase(uint32_t stream_id) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_RESET_REG_INDEX, 0x1);
}

inline bool assert_check(uint32_t stream_id, bool hang) {
  uint32_t debug_assert = NOC_STREAM_READ_REG(stream_id, STREAM_DEBUG_ASSERTIONS_REG_INDEX);
  if (debug_assert > 0 && hang) {
    while(true){};
  }
  return debug_assert > 0;
}

inline bool stream_done_hint() {
  return false;
}

inline bool should_stall_for_tile_header_buffer_reset(uint32_t stream_id, uint32_t msg_info_buf_addr, uint32_t buf_size_tiles, uint32_t &prev_ack_thresh) {
  uint32_t is_remote_src = NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, REMOTE_SOURCE);
  uint32_t msg_info_wr_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX);

  if (is_remote_src && (msg_info_wr_ptr - msg_info_buf_addr >= MAX_TILES_MSG_INFO_BUF_PER_PHASE - 2*buf_size_tiles)) {
    prev_ack_thresh = NOC_STREAM_READ_REG(stream_id, STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX);
    NOC_STREAM_WRITE_REG(stream_id, STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, 0);
    return true;
  }

  return false;
}

inline bool reset_tile_header_buffer(uint32_t stream_id, uint32_t msg_info_buf_addr, uint32_t buf_size_tiles, uint32_t &prev_phases_tiles_received_inc, uint32_t &prev_ack_thresh, uint32_t num_iter_tiles) {
  uint32_t msg_info_full = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_FULL_REG_INDEX);
  uint32_t num_msgs_recv = NOC_STREAM_READ_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_REG_INDEX);

  if (msg_info_full || (num_msgs_recv == buf_size_tiles)) {
    uint32_t buf_space_available = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);

    if (buf_space_available == 0) {
      uint32_t msg_info_rd_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_PTR_REG_INDEX);
      uint32_t msg_info_wr_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX);
      num_msgs_recv = NOC_STREAM_READ_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_REG_INDEX);
      uint32_t msg_info_num_tiles = msg_info_wr_ptr - msg_info_rd_ptr + num_msgs_recv;
      prev_phases_tiles_received_inc = msg_info_rd_ptr - num_msgs_recv - msg_info_buf_addr;
      NOC_STREAM_WRITE_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX, msg_info_buf_addr);
      NOC_STREAM_WRITE_REG(stream_id, STREAM_MSG_INFO_PTR_REG_INDEX, msg_info_buf_addr + num_msgs_recv);
      NOC_STREAM_WRITE_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX, msg_info_buf_addr + msg_info_num_tiles);
      NOC_STREAM_WRITE_REG(stream_id, STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, prev_ack_thresh);
      return true;
    }
  }

  if (num_iter_tiles <= buf_size_tiles) {
    prev_phases_tiles_received_inc = 0;
    NOC_STREAM_WRITE_REG(stream_id, STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, prev_ack_thresh);
    return true;
  }

  return false;
}

inline void check_dummy_phase(uint32_t stream_id) {
  if (stream_phase_is_active(stream_id)) {
    uint32_t cur_phase = stream_get_curr_phase(stream_id) >> EPOCH_SHIFT; 
    if (cur_phase == 0x1F) {
      if (NOC_STREAM_READ_REG_FIELD(stream_id, STREAM_MISC_CFG_REG_INDEX, SOURCE_ENDPOINT)) {
        uint32_t buf_size = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SIZE_REG_INDEX);
        uint32_t buf_space_available = NOC_STREAM_READ_REG(stream_id, STREAM_BUF_SPACE_AVAILABLE_REG_INDEX);
        uint32_t num_tiles = stream_get_curr_phase_num_msgs(stream_id);
    
        if (buf_space_available == buf_size && num_tiles > 0) {
          stream_relay_tiles(stream_id, 1, 1);
        }
      } 
    }
  }
}

inline bool is_dummy_phase(uint32_t stream_id) {
  uint32_t cur_phase = stream_get_curr_phase(stream_id) >> EPOCH_SHIFT; 
  return cur_phase == 0x1F;
}

inline void stream_dram_write_init(uint32_t stream_id) {
}

inline void stream_dram_write(uint32_t stream_id, uint32_t noc, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t len_tiles, uint32_t vc, uint32_t tile_header_buf_addr_word) {
  if (len_bytes > 0) {
    while(true){}; // Not supported in grayskull, so we should hang
  }
}

inline bool stream_dram_write_ok(uint32_t stream_id) {
  return true;
}

inline bool stream_dram_writes_sent(uint32_t stream_id) {
  return true;
}

inline uint32_t get_misc_cfg_with_auto_config(uint32_t auto_config) {
  uint32_t val =  ((0) << (INCOMING_DATA_NOC)) |
             ((0) << (OUTGOING_DATA_NOC)) |
             ((0) << (REMOTE_SRC_UPDATE_NOC)) |
             ((1) << (LOCAL_SOURCES_CONNECTED)) |
             ((0) << (SOURCE_ENDPOINT)) |
             ((0) << (REMOTE_SOURCE)) |
             ((0) << (RECEIVER_ENDPOINT)) |
             ((0) << (LOCAL_RECEIVER)) |
             ((0) << (REMOTE_RECEIVER)) |
             ((auto_config) << (PHASE_AUTO_CONFIG)) |
             ((1) << (PHASE_AUTO_ADVANCE)) |
             ((1) << (DATA_AUTO_SEND)) |
             ((1) << (NEXT_PHASE_SRC_CHANGE)) |
             ((1) << (NEXT_PHASE_DEST_CHANGE)) |
             ((0) << (DATA_BUF_NO_FLOW_CTRL)) |
             ((0) << (DEST_DATA_BUF_NO_FLOW_CTRL)) |
             ((0) << (REMOTE_SRC_IS_MCAST)) |
             ((0) << (NO_PREV_PHASE_OUTGOING_DATA_FLUSH)) |
             ((0) << (UNICAST_VC_REG)) |
             ((1) << (REG_UPDATE_VC_REG));
  return val;
}

inline void config_local_receiver (uint32_t stream_id) {
  NOC_STREAM_WRITE_REG(stream_id, STREAM_MISC_CFG_REG_INDEX, 
                       ((0) << (INCOMING_DATA_NOC)) | 
                       ((0) << (OUTGOING_DATA_NOC)) | 
                       ((0) << (REMOTE_SRC_UPDATE_NOC)) | 
                       ((1) << (LOCAL_SOURCES_CONNECTED)) | 
                       ((0) << (SOURCE_ENDPOINT)) | 
                       ((0) << (REMOTE_SOURCE)) | 
                       ((0) << (RECEIVER_ENDPOINT)) | 
                       ((0) << (LOCAL_RECEIVER)) | 
                       ((0) << (REMOTE_RECEIVER)) | 
                       ((0) << (PHASE_AUTO_CONFIG)) | 
                       ((1) << (PHASE_AUTO_ADVANCE)) | 
                       ((1) << (DATA_AUTO_SEND)) | 
                       ((1) << (NEXT_PHASE_SRC_CHANGE)) | 
                       ((1) << (NEXT_PHASE_DEST_CHANGE)) | 
                       ((0) << (DATA_BUF_NO_FLOW_CTRL)) |
                       ((0) << (DEST_DATA_BUF_NO_FLOW_CTRL)) |
                       ((0) << (REMOTE_SRC_IS_MCAST)) |
                       ((0) << (NO_PREV_PHASE_OUTGOING_DATA_FLUSH)) |
                       ((0) << (UNICAST_VC_REG)) |
                       ((1) << (REG_UPDATE_VC_REG))
                       );
}

inline void stream_local_receiver_start_phase(uint32_t op_stream_id, uint32_t receiver_stream_id, uint32_t tiles_to_clear, uint64_t local_src_mask) {
  for (int k = 0; k < 3; k++) {
    uint32_t val = local_src_mask & 0xFFFFFF;
    NOC_STREAM_WRITE_REG(receiver_stream_id, STREAM_LOCAL_SRC_MASK_REG_INDEX + k, val);
    local_src_mask = local_src_mask >> 24;
  }

  NOC_STREAM_WRITE_REG(receiver_stream_id, STREAM_CURR_PHASE_REG_INDEX, stream_get_curr_phase(op_stream_id));
  NOC_STREAM_WRITE_REG(receiver_stream_id, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, tiles_to_clear << CURR_PHASE_NUM_MSGS);
  NOC_STREAM_WRITE_REG(receiver_stream_id, STREAM_PHASE_ADVANCE_REG_INDEX, 0x1);
}

inline void stream_clear_all_tiles(uint32_t stream_id) {
  uint32_t msg_info_wr_ptr;
  uint32_t msg_info_rd_ptr;
  uint32_t num_msgs_recv;
  uint32_t num_msgs_recv_in_bufs_and_mem;
  do {
    msg_info_rd_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_PTR_REG_INDEX);
    num_msgs_recv = NOC_STREAM_READ_REG(stream_id, STREAM_NUM_MSGS_RECEIVED_REG_INDEX);
    msg_info_wr_ptr = NOC_STREAM_READ_REG(stream_id, STREAM_MSG_INFO_WR_PTR_REG_INDEX);
    num_msgs_recv_in_bufs_and_mem = msg_info_wr_ptr - msg_info_rd_ptr + num_msgs_recv;
    if (num_msgs_recv > 0) {
      stream_receiver_endpoint_single_clear_op(stream_id, 1);
    }
  } while (num_msgs_recv_in_bufs_and_mem > 0);
}

#endif //ndef _STREAM_INTERFACE_H_

