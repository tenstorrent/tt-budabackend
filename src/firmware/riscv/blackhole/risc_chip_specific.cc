// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "risc_chip_specific.h"
#include "stream_interface.h"
#include "dram_address_map.h"
#include "risc.h"
#ifdef PERF_DUMP
#include "risc_perf.h"
#endif
////

void init_tile_clear() {
  // we dont need to do this in wh
}

void wait_till_tile_clear_done(uint32_t stream_id) {
  while(!stream_receiver_endpoint_tile_clearing_finished(stream_id));
}

void process_tile_clearing(kernel_input_stream_state_t* input_stream_state, uint32_t streams_to_clear) {
  kernel_input_stream_state_t* curr_input_state = input_stream_state;
  while (streams_to_clear) {
    curr_input_state = curr_input_state->next;

    int32_t tiles_to_clear = curr_input_state->tiles_to_clear;
    if (!tiles_to_clear) {
      continue;
    }
        
    uint32_t stream_id = curr_input_state->stream_id;
    uint32_t phase_active = stream_phase_is_active(stream_id);
    if (!phase_active) {
      continue;
    }

    while(!stream_receiver_endpoint_tile_clearing_finished(stream_id));

    int32_t tiles_to_clear_group = tiles_to_clear;
    int32_t num_msgs_left_in_phase = stream_get_curr_phase_num_msgs(stream_id);
    if (num_msgs_left_in_phase < tiles_to_clear_group)
      tiles_to_clear_group = num_msgs_left_in_phase;
    if (!tiles_to_clear_group) {
      continue;
    }

    uint32_t num_fork_streams = curr_input_state->num_fork_streams;
    if (num_fork_streams > 0) {
      bool fork_is_done = true;
      for (uint32_t k = 0; k < num_fork_streams; k++) {
        bool push_flushed = stream_get_push_flushed<true>(curr_input_state->fork_stream_ids[k]);
        if (!push_flushed) {
          fork_is_done = false;
          break;
        }
      }
      if (!fork_is_done)
        continue;
    }

    if (stream_id < MULTI_MSG_TILES_STREAM_THESH) {
      stream_receiver_endpoint_tiles_clear_b0(stream_id, tiles_to_clear_group);
      tiles_to_clear -= tiles_to_clear_group;
    } else {
      while (tiles_to_clear_group > 0) {
        int32_t tiles_available = stream_tiles_outstanding(stream_id);
        if (!tiles_available) {
          continue;
        }

        if (tiles_to_clear_group < tiles_available)
          tiles_available = tiles_to_clear_group;

        tiles_to_clear_group -= tiles_available;
        tiles_to_clear -= tiles_available;
        stream_receiver_endpoint_tiles_clear(stream_id, tiles_available);
      }
    }

    curr_input_state->tiles_to_clear = tiles_to_clear;
    uint32_t tiles_to_clear_eq_0 = (tiles_to_clear == 0);
    streams_to_clear -= tiles_to_clear_eq_0;
  }
}

int get_epoch_table_x(int my_x, int my_y) {
#if USE_EMULATOR_DRAM_LAYOUT == 1
  return 1;
#else
#if NO_DISTRIBUTED_EPOCH_TABLES == 1
  return 0;
#else
  int epoch_x;
#ifdef ERISC
  if (my_x <= 4) {
    epoch_x = 0;
  } else {
    epoch_x = 5;
  }
#else
  if (my_x <= 7) {
    epoch_x = 0;
  } else {
    epoch_x = 9;
  }
#endif
  return epoch_x;
#endif
#endif
}


int get_epoch_table_y(int my_x, int my_y) {
#if USE_EMULATOR_DRAM_LAYOUT == 1
  return 0;
#else
#if NO_DISTRIBUTED_EPOCH_TABLES == 1
  return 0;
#else
  return my_y;
#endif
#endif
}

int get_epoch_index_x(int my_x) {
  if (my_x >= 18 && my_x <= 21)
    my_x -= 17;
  else if (my_x > 21)
    my_x -= 16;

  return my_x;
}

int get_epoch_index_y(int my_y) {
  if (my_y == 16)
    my_y = 0;
  else if (my_y == 17)
    my_y = 6;
  else if (my_y <= 22 && my_y > 17)
    my_y = (my_y - 17);
  else if (my_y > 22)
    my_y = (my_y - 16);

  return my_y;
}

void risc_wait_for_cmd_buf(uint32_t noc, uint32_t cmd_buf)
{
#ifdef RISC_B0_HW
  while (!ncrisc_noc_fast_write_ok(noc, cmd_buf));
#endif
}

void risc_dram_write_init(uint32_t dram_stream)
{
#ifdef RISC_B0_HW
  stream_dram_write_init(dram_stream, dram_mem::address_map::DRAM_EACH_BANK_TILE_HEADER_BUFFER);
#endif
}

void risc_dram_write (uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t noc, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t len_tiles, uint32_t vc, uint32_t stream_msg_info_buf_addr, uint32_t transaction_id)
{
#ifdef RISC_B0_HW
  bool do_dram_writes_with_cmd_buf = dram_writes_with_cmd_buf;
#else
  bool do_dram_writes_with_cmd_buf = true;
#endif

  if (do_dram_writes_with_cmd_buf) {
    ncrisc_noc_fast_write_any_len(noc, NCRISC_WR_CMD_BUF, src_addr, dest_addr, len_bytes, vc, false, false, 1, transaction_id);
  } else {
    while (!stream_dram_write_ok(dram_stream));
    stream_dram_write(dram_stream, noc, src_addr, dest_addr, len_bytes, len_tiles, vc, stream_msg_info_buf_addr);
  }
}

bool risc_dram_write_ok(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t output_noc)
{
#ifdef RISC_B0_HW
  bool do_dram_writes_with_cmd_buf = dram_writes_with_cmd_buf;
#else
  bool do_dram_writes_with_cmd_buf = true;
#endif

  if (do_dram_writes_with_cmd_buf)
    return ncrisc_noc_fast_write_bufs_ok(output_noc);
  else
    return stream_dram_write_ok(dram_stream);
}

bool risc_dram_writes_sent(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream)
{
#ifdef RISC_B0_HW
  bool do_dram_writes_with_cmd_buf = dram_writes_with_cmd_buf;
#else
  bool do_dram_writes_with_cmd_buf = true;
#endif

  if (do_dram_writes_with_cmd_buf)
    return true;
  else
    return stream_dram_writes_sent(dram_stream);
}

void replicate(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id) {
  const uint32_t REPLICATE_VC = 0;
#ifdef RISC_B0_HW
  uint32_t replicate_cmd_buf = NCRISC_SMALL_TXN_CMD_BUF;
#else
  uint32_t replicate_cmd_buf = NCRISC_WR_CMD_BUF;
#endif
  for (uint32_t j = 0; j < times_to_replicate; j++) {
    while (!ncrisc_noc_fast_write_ok(noc_id, replicate_cmd_buf));
    ncrisc_noc_fast_write(noc_id, replicate_cmd_buf,
                          src_addr,
                          dest_addr,
                          chunk_size_bytes,
                          REPLICATE_VC, false, false, 1, transaction_id);
    dest_addr += chunk_size_bytes;
  }
}

void replicate_l1(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id) {
  const uint32_t REPLICATE_VC = 0;
#ifdef RISC_B0_HW
  uint32_t replicate_cmd_buf = NCRISC_SMALL_TXN_CMD_BUF;
#else
  uint32_t replicate_cmd_buf = NCRISC_WR_CMD_BUF;
#endif
  for (uint32_t j = 0; j < times_to_replicate; j++) {
    while (!ncrisc_noc_fast_write_ok_l1(noc_id, replicate_cmd_buf));
    ncrisc_noc_fast_write_l1(noc_id, replicate_cmd_buf,
                          src_addr,
                          dest_addr,
                          chunk_size_bytes,
                          REPLICATE_VC, false, false, 1, transaction_id);
    dest_addr += chunk_size_bytes;
  }
}

bool has_pending_dram_write_ptrs(uint32_t dram_stream) {
#ifdef RISC_B0_HW
  uint32_t dram_output_stream_state_idx = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_DRAM_OUTPUT_STREAM_STATE);

  if ((dram_output_stream_state_idx & PTR_UPDATE_TYPE_DRAM_OUTPUT_STREAM_STATE) != 0) {
    return true;
  } else {
    return false;
  }
#else
  return false;
#endif
}

void write_pending_dram_write_ptrs(uint32_t dram_stream, dram_output_stream_state_t *dram_output_stream_state_base) {
#ifdef RISC_B0_HW
  uint32_t dram_output_stream_state_idx = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_DRAM_OUTPUT_STREAM_STATE);

  if ((dram_output_stream_state_idx & PTR_UPDATE_TYPE_DRAM_OUTPUT_STREAM_STATE) != 0) {
    dram_output_stream_state_t* curr_dram_output_stream_state = &(dram_output_stream_state_base[dram_output_stream_state_idx & ~PTR_UPDATE_TYPE_DRAM_OUTPUT_STREAM_STATE]);
    dram_q_state_t tt_l1_ptr * next_dram_q_issue = curr_dram_output_stream_state->next_dram_q_issue;
    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
    uint32_t stream_id = curr_dram_output_stream_state->stream_id;
    uint32_t output_noc = stream_get_output_noc(stream_id);
    uint32_t output_vc = stream_get_output_unicast_vc(stream_id);
    uint64_t dram_ptr_addr;
    if ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0) {
      dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
    } else {
      dram_ptr_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
    }
    bool is_ram = (next_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0;

    uint32_t dram_wrptr_q_slots = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_WR_PTR_UPDATE);
    if (dram_wrptr_q_slots) {
      dram_wrptr_q_slots = dram_wrptr_q_slots & ~PTR_UPDATE_TYPE_WR_PTR_UPDATE;
      l1_ptrs->l1_to_dram.wr_dram_wrptr = dram_wrptr_q_slots;
      if (!is_ram) {
        while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_dram_wrptr)), dram_ptr_addr+DRAM_BUF_WRPTR_OFFSET, 2,
                              output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
      }
      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_WR_PTR_UPDATE, 0);
    }

    uint32_t update_type = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_TYPE);
    if (update_type == PTR_UPDATE_TYPE_EPOCH_W_STRIDE || update_type == PTR_UPDATE_TYPE_EPOCH) {
  #ifdef EPOCH_CHECK_ENABLED
      if (l1_ptrs->rd_epoch_id_tag != DRAM_QUEUE_NO_EPOCH_CHECK)
        l1_ptrs->wr_epoch_id_tag = DRAM_QUEUE_EPOCH_CHECK_EN | (curr_dram_output_stream_state->epoch_id);
      else
        l1_ptrs->wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
  #else
        // No epoch check works around #928, however it will not support multiple epoch consumers that need to sync on the same queue
        l1_ptrs->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
  #endif
      if (update_type == PTR_UPDATE_TYPE_EPOCH_W_STRIDE) {
        uint32_t stride_wrap = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_STRIDE_WRAP);
        uint32_t stride = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_STRIDE);
        l1_ptrs->local.stride_wrap = stride_wrap;
        l1_ptrs->l1_to_dram.wr_stride = stride;
        l1_ptrs->dram_to_l1.rd_stride = stride;
        while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_epoch_id_tag)), dram_ptr_addr+DRAM_BUF_EPOCH_ID_TAG_OFFSET, 4,
                              output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
      } else {
        while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_epoch_id_tag)), dram_ptr_addr+DRAM_BUF_EPOCH_ID_TAG_OFFSET, 2,
                              output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
      }

      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_TYPE, 0);
    } else if (update_type == PTR_UPDATE_TYPE_STRIDE) {
      uint32_t stride_wrap = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_STRIDE_WRAP);
      uint32_t stride = stream_dram_writes_read_scratch(dram_stream, PTR_UPDATE_REG_STRIDE);
      l1_ptrs->local.stride_wrap = stride_wrap;
      l1_ptrs->l1_to_dram.wr_stride = stride;
      l1_ptrs->dram_to_l1.rd_stride = stride;
      while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_stride)), dram_ptr_addr+DRAM_BUF_STRIDE_OFFSET, 2,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);

      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_TYPE, 0);
    }

    stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_DRAM_OUTPUT_STREAM_STATE, 0);
  }
#else
#endif
}

void set_pending_dram_write_ptrs(uint32_t dram_stream, uint32_t dram_writes_with_cmd_buf, bool is_ram, bool is_strided_write, uint32_t write_stride, uint32_t total_write_strides, uint32_t dram_wrptr_q_slots, uint32_t output_noc, uint32_t output_vc, 
                                 uint64_t dram_buf_addr, dram_output_stream_state_t* curr_dram_output_stream_state, uint32_t curr_dram_output_stream_state_idx, volatile dram_io_state_t tt_l1_ptr * l1_ptrs, uint32_t curr_stride_wrap, uint32_t next_stride_wrap) {
#ifdef RISC_B0_HW
  if (dram_writes_with_cmd_buf || curr_dram_output_stream_state->moves_raw_data) {
    update_dram_write_ptrs(is_ram, is_strided_write, write_stride, total_write_strides, dram_wrptr_q_slots, output_noc, output_vc,
                           dram_buf_addr, curr_dram_output_stream_state, l1_ptrs, curr_stride_wrap, next_stride_wrap);
  } else {
    if (!is_strided_write || write_stride == total_write_strides-1) {
      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_WR_PTR_UPDATE, PTR_UPDATE_TYPE_WR_PTR_UPDATE | dram_wrptr_q_slots);
      
      if (is_strided_write) {
        stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_TYPE, PTR_UPDATE_TYPE_EPOCH_W_STRIDE);
        stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_STRIDE_WRAP, next_stride_wrap);
        stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_STRIDE, next_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0);
      } else {
        stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_TYPE, PTR_UPDATE_TYPE_EPOCH);
      }

      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_DRAM_OUTPUT_STREAM_STATE, curr_dram_output_stream_state_idx | PTR_UPDATE_TYPE_DRAM_OUTPUT_STREAM_STATE);
    } else {
      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_TYPE, PTR_UPDATE_TYPE_STRIDE);
      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_STRIDE_WRAP, next_stride_wrap);
      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_STRIDE, curr_stride_wrap + write_stride + 1);

      stream_dram_writes_write_scratch(dram_stream, PTR_UPDATE_REG_DRAM_OUTPUT_STREAM_STATE, curr_dram_output_stream_state_idx | PTR_UPDATE_TYPE_DRAM_OUTPUT_STREAM_STATE);
    }
  }
#else
  update_dram_write_ptrs(is_ram, is_strided_write, write_stride, total_write_strides, dram_wrptr_q_slots, output_noc, output_vc,
                         dram_buf_addr, curr_dram_output_stream_state, l1_ptrs, curr_stride_wrap, next_stride_wrap);
#endif
}

void process_dram_write(
  uint32_t &num_dram_output_streams, dram_output_stream_state_t *dram_output_stream_state, uint32_t &dram_ptr_update_cnt, uint32_t &total_tiles_to_clear
) {
  dram_output_stream_state_t* curr_dram_output_stream_state = dram_output_stream_state;
  for (uint32_t i = 0; i < num_dram_output_streams; i++, curr_dram_output_stream_state++) {

    uint32_t stream_id = curr_dram_output_stream_state->stream_id;
    uint32_t epoch_q_slots_remaining = curr_dram_output_stream_state->epoch_q_slots_remaining;

    if (!epoch_q_slots_remaining) {
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
      if (curr_dram_output_stream_state->dram_q_available) {
        risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
        curr_dram_output_stream_state->dram_q_available = 0;
      }
#endif
      continue;
    }
        
    RISC_POST_STATUS(0xF0000000);
    RISC_POST_DEBUG(0xF0100000);
    RISC_POST_DEBUG(epoch_q_slots_remaining);
    RISC_POST_DEBUG(0xF0200000);

    dram_q_state_t tt_l1_ptr * next_dram_q_issue = curr_dram_output_stream_state->next_dram_q_issue;
    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
    uint16_t data_send_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
    uint32_t data_send_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
    uint32_t dram_writes_with_cmd_buf = curr_dram_output_stream_state->stream_info->dram_io_info->dram_writes_with_cmd_buf;
    uint32_t dram_output_no_push = curr_dram_output_stream_state->stream_info->dram_io_info->dram_output_no_push;
    uint32_t dram_stream = DRAM_STREAM_1;

    if (dram_writes_with_cmd_buf || curr_dram_output_stream_state->moves_raw_data) {
      bool previous_writes_sent = true;
      for (uint32_t n = 0; n < NUM_NOCS; n++) {
        previous_writes_sent = previous_writes_sent && ncrisc_noc_nonposted_writes_sent(n, NCRISC_WR_DEF_TRID);
      }
      if (!previous_writes_sent)
        continue;
    } else {
      if (risc_dram_writes_sent(dram_writes_with_cmd_buf, DRAM_STREAM_1))
        dram_stream = DRAM_STREAM_1;
#ifndef ERISC
      else if (risc_dram_writes_sent(dram_writes_with_cmd_buf, DRAM_STREAM_2))
        dram_stream = DRAM_STREAM_2;
#endif
      else
        dram_stream = 0;
      if (!dram_stream)
        continue;
    }

    uint32_t write_stride = curr_dram_output_stream_state->write_stride & ~DRAM_STRIDE_UPDATE_WAIT;
    bool is_strided_write = curr_dram_output_stream_state->total_write_strides > 1;
    if (is_strided_write && (curr_dram_output_stream_state->write_stride & DRAM_STRIDE_UPDATE_WAIT)) {

      RISC_POST_STATUS(0xF0300000);

      bool can_do_stride_update = true;
      if (!dram_writes_with_cmd_buf && !curr_dram_output_stream_state->moves_raw_data) {
        uint32_t sending_dram_stream_1 = curr_dram_output_stream_state->stream_sending_q.peek(0);
        if (sending_dram_stream_1 && !risc_dram_writes_sent(dram_writes_with_cmd_buf, sending_dram_stream_1))
          can_do_stride_update = false;

        uint32_t sending_dram_stream_2 = curr_dram_output_stream_state->stream_sending_q.peek(1);
        if (sending_dram_stream_2 && !risc_dram_writes_sent(dram_writes_with_cmd_buf, sending_dram_stream_2))
          can_do_stride_update = false;
      }
      if (!can_do_stride_update) {
        continue;
      }

      dram_q_state_t tt_l1_ptr * next_dram_q_issue = curr_dram_output_stream_state->next_dram_q_issue;
      volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
      volatile uint32_t tt_l1_ptr * l1_ptr_addr = next_dram_q_issue->l1_dram_ptrs;
      uint32_t output_noc = stream_get_output_noc(stream_id);

      if (!header_reads_flushed(output_noc, NCRISC_HEADER_RD_TRID, l1_ptr_addr)) {
        continue;
      }

      uint32_t rd_stride;
      uint32_t curr_stride_wrap = 0;
      uint32_t next_stride_wrap = 0;
      if (!stride_iter_matches(l1_ptrs, rd_stride, curr_stride_wrap, next_stride_wrap)) {
        if (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK)
          dram_ptr_update_cnt = 0;
        continue;
      }

      if (write_stride == rd_stride) {
        uint64_t dram_ptr_addr;
        if ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0) {
          dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
        } else {
          dram_ptr_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
        }
        uint32_t output_vc = stream_get_output_unicast_vc(stream_id);
        bool is_ram = (next_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0; 
        update_dram_write_ptrs(is_ram, is_strided_write, write_stride, curr_dram_output_stream_state->total_write_strides, next_dram_q_issue->dram_ptr_issued_q_slots, output_noc, output_vc,
                               dram_ptr_addr, curr_dram_output_stream_state, l1_ptrs, curr_stride_wrap, next_stride_wrap);

        next_dram_q_issue = next_dram_q_issue->next;
        curr_dram_output_stream_state->next_dram_q_issue = next_dram_q_issue;
        curr_dram_output_stream_state->write_stride = write_stride;
      } else {
        if (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK)
          dram_ptr_update_cnt = 0;
        continue;
      }
    }

    RISC_POST_STATUS(0xF1000000 | stream_id);

    // Reg reads, flushed by immediate subsequent use
    uint32_t phase_active = stream_phase_is_active(stream_id);
    uint32_t curr_phase_tiles_remaining = stream_rec_endpoint_get_phase_tiles_count(stream_id);
    uint32_t output_noc = stream_get_output_noc(stream_id);
    bool cmd_buf_free;
    bool past_sending_threshold;

    bool stream_sending_q_full = false;
    uint32_t in_flight_tiles = 0;
    uint32_t in_flight_q_slots = 0;
    uint32_t total_in_flight_tiles = get_total_in_flight_tiles(curr_dram_output_stream_state);
    if (dram_writes_with_cmd_buf || curr_dram_output_stream_state->moves_raw_data) {
      in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles;
      in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots;
      curr_dram_output_stream_state->in_flight_tiles = 0;
      curr_dram_output_stream_state->in_flight_q_slots = 0;
      cmd_buf_free = ncrisc_noc_fast_write_bufs_ok(output_noc);
      past_sending_threshold = true;
    } else {
      uint32_t sending_dram_stream = curr_dram_output_stream_state->stream_sending_q.peek();
      if (sending_dram_stream && risc_dram_writes_sent(dram_writes_with_cmd_buf, sending_dram_stream)) {
        write_pending_dram_write_ptrs(sending_dram_stream, dram_output_stream_state);
        if (sending_dram_stream == DRAM_STREAM_1) {
          in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles;
          in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots;
          curr_dram_output_stream_state->in_flight_tiles = 0;
          curr_dram_output_stream_state->in_flight_q_slots = 0;
        } else {
          in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles_2;
          in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots_2;
          curr_dram_output_stream_state->in_flight_tiles_2 = 0;
          curr_dram_output_stream_state->in_flight_q_slots_2 = 0;
        }
        curr_dram_output_stream_state->stream_sending_q.pop();
      }
      stream_sending_q_full = curr_dram_output_stream_state->stream_sending_q.full();
      cmd_buf_free = !has_pending_dram_write_ptrs(dram_stream);

      uint32_t cycles_since_last_write = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L) - dram_output_stream_state[0].dram_stream_write_start_time;
      uint32_t min_cycles_for_trans = data_send_chunk_size_bytes / NOC_WORD_BYTES;
      uint32_t cycles_thresh = min_cycles_for_trans > CYCLES_SINCE_LAST_STREAM_DRAM_WRITE_THRESH ? min_cycles_for_trans - CYCLES_SINCE_LAST_STREAM_DRAM_WRITE_THRESH : 0;
      past_sending_threshold = cycles_since_last_write > cycles_thresh;
    }

    if (curr_dram_output_stream_state->moves_raw_data || dram_output_no_push) {
      if (phase_active && !curr_phase_tiles_remaining && !total_in_flight_tiles) {
        uint32_t flags = curr_dram_output_stream_state->stream_info->flags;
        if ((flags & STREAM_BRISC_PACK) != 0)
          stream_set_curr_phase_num_msgs(stream_id, 0);
        stream_force_next_phase(stream_id);
        phase_active = false;
      }
    }

    if (in_flight_tiles) {
      epoch_q_slots_remaining -= in_flight_q_slots;
      curr_dram_output_stream_state->tiles_to_clear = in_flight_tiles;
      total_tiles_to_clear += in_flight_tiles;
      curr_dram_output_stream_state->epoch_q_slots_remaining = epoch_q_slots_remaining;
    }

    if (dram_stream == DRAM_STREAM_1) {
      in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots;
      in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles;
    } else {
      in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots_2;
      in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles_2;
    }

    RISC_POST_DEBUG(0xF1300000 | phase_active);
    RISC_POST_DEBUG(0xF1400000 | curr_phase_tiles_remaining);
    RISC_POST_DEBUG(0xF1500000 | output_noc);
    RISC_POST_DEBUG(0xF1600000 | cmd_buf_free);
        
    if (!epoch_q_slots_remaining || !phase_active || !curr_phase_tiles_remaining || !cmd_buf_free || stream_sending_q_full || !past_sending_threshold) {
      continue;
    }

    RISC_POST_STATUS(0xF2000000);

#ifdef DRAM_DECOUPLE
    uint32_t dram_decoupled = l1_ptrs->local.dram_decoupled;
#else
    uint32_t dram_decoupled = 0;
#endif
    uint16_t curr_phase_tiles_sent = curr_dram_output_stream_state->curr_phase_tiles_sent;
    uint16_t curr_phase_tiles_received;
    uint16_t tiles_avail_to_send;
    if (curr_dram_output_stream_state->moves_raw_data || dram_output_no_push) {
      // Reg read, flushed by immediate subsequent use
      curr_phase_tiles_received = *get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
      tiles_avail_to_send = op_pack_tiles_ptr_sub(curr_phase_tiles_received, curr_phase_tiles_sent);
    } else {
      // Reg read, flushed by immediate subsequent use
      uint32_t stream_msg_info_buf_addr_word = curr_dram_output_stream_state->stream_info->msg_info_buf_start;
      curr_phase_tiles_received = stream_phase_tiles_received(stream_id, stream_msg_info_buf_addr_word);
      tiles_avail_to_send = curr_phase_tiles_received - curr_phase_tiles_sent;
    }
        
    bool next_data_chunk_available = tiles_avail_to_send >= data_send_chunk_size_tiles;
        
    RISC_POST_DEBUG(0xF2100000);
    RISC_POST_DEBUG((uint32_t)next_dram_q_issue);
    RISC_POST_DEBUG(curr_phase_tiles_sent);
    RISC_POST_DEBUG(data_send_chunk_size_tiles);
    RISC_POST_DEBUG(curr_phase_tiles_received);
    RISC_POST_DEBUG(0xF2200000);

    if (next_data_chunk_available) {


      RISC_POST_STATUS(0xF3000000);
      dram_q_state_t tt_l1_ptr * temp_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue;
      uint32_t dram_rdptr_q_slots = temp_dram_q_issue->dram_ptr_incoming_q_slots & ~DRAM_PTR_UPDATE_PENDING_MASK;
      bool dram_q_space_available =  dram_decoupled ? true : (temp_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0 ||
                                    !dram_io_full(dram_rdptr_q_slots, temp_dram_q_issue->dram_ptr_issued_q_slots, temp_dram_q_issue->dram_buf_size_q_slots);

      RISC_POST_DEBUG(0xF3100000 | temp_dram_q_issue->dram_ptr_issued_q_slots);
      RISC_POST_DEBUG(temp_dram_q_issue->dram_buf_size_q_slots);
      RISC_POST_DEBUG(0xF3200000 | dram_rdptr_q_slots);

      uint32_t this_dram_io_entries = dram_io_entries(dram_rdptr_q_slots, temp_dram_q_issue->dram_ptr_issued_q_slots, temp_dram_q_issue->dram_buf_size_q_slots);
      if (this_dram_io_entries >= (temp_dram_q_issue->dram_buf_size_q_slots-DRAM_MIN_ENTIRES_POLL) && (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK))
        dram_ptr_update_cnt = 0;

      if (dram_q_space_available) {

#ifndef PERF_DUMP
        RISC_POST_STATUS(0xF4000000);
#endif

#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
        if (!curr_dram_output_stream_state->dram_q_available) {
          risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
          curr_dram_output_stream_state->dram_q_available = 1;
        }
#endif

        //set_dont_poll_immediately(); // Maybe needed in the future
        dram_ptr_update_cnt = dram_ptr_update_cnt | (DRAM_PTR_UPDATE_MASK + 1);

        uint32_t wr_ptr_autoinc = l1_ptrs->dram_to_l1.rd_gwr_ptr_autoinc;
        uint32_t stream_rd_ptr_byte = stream_dram_write_should_reset_pointers(stream_id) ? 0 : curr_dram_output_stream_state->stream_rd_ptr_byte;
        wr_ptr_autoinc = wr_ptr_autoinc ? wr_ptr_autoinc : 1;

        // Reg reads, flushed by immediate subsequent use
        uint32_t output_vc = stream_get_output_unicast_vc(stream_id); // FIXME imatosevic - make sure this field is set by blob gen even for DRAM output streams

        RISC_POST_DEBUG(0xF4100000 | output_noc);
        RISC_POST_DEBUG(0xF4200000 | output_vc);

        dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state = l1_ptrs->local.dram_io_scatter_state;
        epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info = curr_dram_output_stream_state->stream_info->dram_io_info;
        volatile tt_uint64_t tt_l1_ptr * scatter_offsets;
        bool has_scatter_offsets;
        if (dram_io_scatter_state != ((dram_io_scatter_state_t*)0)) {
          scatter_offsets = dram_io_scatter_state->scatter_offsets;
          has_scatter_offsets = get_scatter_offsets(dram_io_scatter_state, scatter_offsets, dram_io_info->scatter_list_stream_id);
        } else {
          has_scatter_offsets = true;
        }

        while (has_scatter_offsets) {

#ifndef PERF_DUMP
          RISC_POST_STATUS(0xF5000000);
#endif

          uint32_t dram_buf_size_bytes = l1_ptrs->local.dram_buf_size_bytes;
          uint64_t dram_buf_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
          bool full_q_slot_sent = false;

          if (curr_dram_output_stream_state->moves_raw_data) {
            process_dram_write_moves_raw_data_l1(curr_dram_output_stream_state, next_dram_q_issue, stream_id,
                                              data_send_chunk_size_tiles, output_vc, data_send_chunk_size_bytes, dram_buf_addr,
                                              stream_rd_ptr_byte, dram_buf_size_bytes, full_q_slot_sent);
          } else {
            uint32_t stream_rd_addr = (stream_get_data_buf_addr(stream_id) * MEM_WORD_WIDTH) + stream_rd_ptr_byte;
            uint32_t stream_msg_info_buf_addr = curr_dram_output_stream_state->stream_info->msg_info_buf_start;

            if (dram_io_scatter_state != ((dram_io_scatter_state_t*)0)) {
              uint32_t dram_io_scatter_chunk_size_bytes = dram_io_scatter_state->scatter_chunk_size_bytes;
              uint32_t dram_io_scatter_size = dram_io_scatter_state->scatter_offsets_size;
              uint32_t dram_embeddings_row_shift = dram_io_info->dram_embeddings_row_shift;
              uint32_t dram_io_scatter_chunk_size_tiles = dram_io_scatter_state->scatter_chunk_size_tiles;
              uint32_t scatter_idx = next_dram_q_issue->scatter_offsets_index;
                
              uint32_t c_dim_count = curr_dram_output_stream_state->c_dim_count;
              uint32_t c_dim_size = curr_dram_output_stream_state->c_dim_size;
              uint32_t col_offset_bytes = curr_dram_output_stream_state->col_offset_bytes;
              dram_output_stream_issue_scatter_write_indicies(output_noc, output_vc, next_dram_q_issue->dram_ptr_issued_byte, data_send_chunk_size_tiles, dram_io_scatter_chunk_size_tiles,
                                                              dram_io_scatter_chunk_size_bytes, stream_rd_addr, scatter_offsets, scatter_idx,
                                                              dram_buf_addr, dram_embeddings_row_shift, c_dim_count, c_dim_size, dram_io_info->c_dim_loop_num_rows, col_offset_bytes, NCRISC_WR_DEF_TRID,
                                                              stream_msg_info_buf_addr, dram_writes_with_cmd_buf, dram_stream);
              curr_dram_output_stream_state->c_dim_count = c_dim_count;
              curr_dram_output_stream_state->col_offset_bytes = col_offset_bytes;

              clear_scatter_offsets(curr_dram_output_stream_state->scatter_list_input_index, next_dram_q_issue, dram_io_scatter_state, dram_io_info, dram_io_scatter_size, scatter_idx, dram_io_info->scatter_list_stream_id);
            } else {
              uint32_t offset = next_dram_q_issue->dram_ptr_issued_byte;
              if ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) == 0) {
                offset += DRAM_BUF_DATA_OFFSET;
              }
              uint64_t dram_dest_addr = dram_buf_addr + offset;

              if (dram_decoupled == 0) {
                RISC_POST_STATUS(0xF4110000);
                risc_dram_write (dram_writes_with_cmd_buf, dram_stream, output_noc, stream_rd_addr, dram_dest_addr, data_send_chunk_size_bytes, data_send_chunk_size_tiles, output_vc, stream_msg_info_buf_addr, NCRISC_WR_DEF_TRID);
                dram_output_stream_state[0].dram_stream_write_start_time = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
              }
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
              risc::record_timestamp(risc::perf_event::DRAM_WRITE_SENT | (stream_id << 16) | data_send_chunk_size_bytes);
  #endif
              RISC_POST_DEBUG(0xF5000000);
              RISC_POST_DEBUG(dram_dest_addr >> 32);
              RISC_POST_DEBUG(dram_dest_addr & 0xFFFFFFFF);
              RISC_POST_DEBUG(stream_rd_addr);
              RISC_POST_DEBUG((uint32_t)next_dram_q_issue);
              RISC_POST_DEBUG(0xF6000000);
            }

            stream_rd_ptr_byte = buf_ptr_inc_wrap(stream_rd_ptr_byte, data_send_chunk_size_bytes, curr_dram_output_stream_state->stream_buf_size_bytes);

            if (!dram_writes_with_cmd_buf)
              curr_dram_output_stream_state->stream_sending_q.push(dram_stream);

            uint32_t dram_num_partial_q_slot_issued_tiles = next_dram_q_issue->dram_num_partial_q_slot_issued_tiles;
            dram_num_partial_q_slot_issued_tiles += data_send_chunk_size_tiles;
            next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = dram_num_partial_q_slot_issued_tiles;
            full_q_slot_sent = (dram_num_partial_q_slot_issued_tiles == curr_dram_output_stream_state->q_slot_size_tiles);

            next_dram_q_issue->dram_ptr_issued_byte = buf_ptr_inc_wrap(next_dram_q_issue->dram_ptr_issued_byte, data_send_chunk_size_bytes, dram_buf_size_bytes);

          }

          bool update_dram_q = true;
          if (full_q_slot_sent) {
            volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
            volatile uint32_t tt_l1_ptr * l1_ptr_addr = next_dram_q_issue->l1_dram_ptrs;

            uint64_t dram_ptr_addr;
            if ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0) {
              dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
            } else {
              dram_ptr_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
            }
            bool is_ram = (next_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0;

            if (wr_ptr_autoinc > 1) {
              uint32_t q_slot_size_bytes = curr_dram_output_stream_state->stream_info->dram_io_info->dram_q_slot_size_bytes;
              next_dram_q_issue->dram_ptr_issued_byte = buf_ptr_inc_wrap(next_dram_q_issue->dram_ptr_issued_byte, mulsi3(wr_ptr_autoinc-1,q_slot_size_bytes), dram_buf_size_bytes);
            }

            in_flight_q_slots++;
            next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = 0;
            next_dram_q_issue->dram_ptr_issued_q_slots = dram_io_incr_ptr(temp_dram_q_issue->dram_ptr_issued_q_slots, wr_ptr_autoinc, temp_dram_q_issue->dram_buf_size_q_slots);

            uint32_t rd_stride;
            uint32_t curr_stride_wrap = 0;
            uint32_t next_stride_wrap = 0;
            bool stride_iter_matches_expr = is_strided_write && stride_iter_matches(l1_ptrs, rd_stride, curr_stride_wrap, next_stride_wrap);

#ifdef RISC_B0_HW
            bool do_ptr_update = !is_strided_write || 
                                                      ((dram_writes_with_cmd_buf || curr_dram_output_stream_state->moves_raw_data) &&
                                                       (header_reads_flushed(output_noc, NCRISC_HEADER_RD_TRID, l1_ptr_addr) && stride_iter_matches_expr && write_stride == rd_stride));
#else
            bool do_ptr_update = !is_strided_write || (header_reads_flushed(output_noc, NCRISC_HEADER_RD_TRID, l1_ptr_addr) && stride_iter_matches_expr && write_stride == rd_stride);
#endif
            if (do_ptr_update) {
              set_pending_dram_write_ptrs(dram_stream, dram_writes_with_cmd_buf, is_ram, is_strided_write, write_stride, curr_dram_output_stream_state->total_write_strides, temp_dram_q_issue->dram_ptr_issued_q_slots, output_noc, output_vc,
                                          dram_ptr_addr, curr_dram_output_stream_state, i, l1_ptrs, curr_stride_wrap, next_stride_wrap);
            } else {
              update_dram_q = false;
              curr_dram_output_stream_state->write_stride = write_stride | DRAM_STRIDE_UPDATE_WAIT;
              if (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK)
                dram_ptr_update_cnt = 0;
            }

            if (update_dram_q) {
              next_dram_q_issue = next_dram_q_issue->next;
              l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
            }
          }

          in_flight_tiles += data_send_chunk_size_tiles;
          curr_phase_tiles_remaining -= data_send_chunk_size_tiles;
          if (curr_dram_output_stream_state->moves_raw_data || dram_output_no_push) {
            curr_phase_tiles_sent = op_pack_tiles_ptr_add(curr_phase_tiles_sent, data_send_chunk_size_tiles);
          } else {
            curr_phase_tiles_sent += data_send_chunk_size_tiles;
          }

          if (dram_stream == DRAM_STREAM_1) {
            curr_dram_output_stream_state->in_flight_q_slots = in_flight_q_slots;
            curr_dram_output_stream_state->in_flight_tiles = in_flight_tiles;
          } else {
            curr_dram_output_stream_state->in_flight_q_slots_2 = in_flight_q_slots;
            curr_dram_output_stream_state->in_flight_tiles_2 = in_flight_tiles;
          }

          RISC_POST_DEBUG(0xF7000000 | curr_phase_tiles_remaining);
          RISC_POST_DEBUG(0xF7100000 | curr_phase_tiles_sent);
          RISC_POST_DEBUG(0xF7200000 | in_flight_q_slots);
          RISC_POST_DEBUG(0xF7300000 | in_flight_tiles);
              
          if (!curr_phase_tiles_remaining) {
            if (!curr_dram_output_stream_state->moves_raw_data && !dram_output_no_push)
              curr_phase_tiles_sent = 0;
            break;
          }

          if (is_strided_write && !update_dram_q)
            break;

          if (!curr_dram_output_stream_state->moves_raw_data && !dram_writes_with_cmd_buf) {
            break;
          }

          if (!ncrisc_noc_fast_write_bufs_ok(output_noc)) {
            break;
          }

          if (curr_dram_output_stream_state->moves_raw_data || dram_output_no_push) {
            tiles_avail_to_send = op_pack_tiles_ptr_sub(curr_phase_tiles_received, curr_phase_tiles_sent);
          } else {
            tiles_avail_to_send = curr_phase_tiles_received - curr_phase_tiles_sent;
          }
          next_data_chunk_available = tiles_avail_to_send >= data_send_chunk_size_tiles;

          if (!next_data_chunk_available) {
            break;
          }
          dram_q_state_t tt_l1_ptr * temp_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue;
          dram_rdptr_q_slots = temp_dram_q_issue->dram_ptr_incoming_q_slots & ~DRAM_PTR_UPDATE_PENDING_MASK;
          dram_q_space_available = dram_decoupled ? true : (temp_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0 ||
                                   !dram_io_full(dram_rdptr_q_slots, temp_dram_q_issue->dram_ptr_issued_q_slots, temp_dram_q_issue->dram_buf_size_q_slots);

          RISC_POST_DEBUG(0xF8000000 | temp_dram_q_issue->dram_ptr_issued_q_slots);
          RISC_POST_DEBUG(temp_dram_q_issue->dram_buf_size_q_slots);
          RISC_POST_DEBUG(0xF8100000 | dram_rdptr_q_slots);
          if (!dram_q_space_available) {
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
            if (curr_dram_output_stream_state->dram_q_available) {
              risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
              curr_dram_output_stream_state->dram_q_available = 0;
            }
#endif
            break;
          }

          if (dram_io_scatter_state != ((dram_io_scatter_state_t*)0)) {
            has_scatter_offsets = get_scatter_offsets(dram_io_scatter_state, scatter_offsets, dram_io_info->scatter_list_stream_id);
          } else {
            has_scatter_offsets = true;
          }
        }
            
        curr_dram_output_stream_state->stream_rd_ptr_byte = stream_rd_ptr_byte; 
        curr_dram_output_stream_state->next_dram_q_issue = next_dram_q_issue;
        curr_dram_output_stream_state->curr_phase_tiles_sent = curr_phase_tiles_sent;              
        stream_rec_endpoint_set_phase_tiles_count(stream_id, curr_phase_tiles_remaining);
      }
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
      else {
        if (curr_dram_output_stream_state->dram_q_available) {
          risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
          curr_dram_output_stream_state->dram_q_available = 0;
        }
      }
#endif
    }
  }

}

void process_dram_write_clear(uint32_t &num_dram_output_streams, dram_output_stream_state_t *dram_output_stream_state, uint32_t& total_tiles_to_clear) {
  while (total_tiles_to_clear) {
      
    RISC_POST_STATUS(0x20000000 | total_tiles_to_clear);

    dram_output_stream_state_t* curr_dram_output_stream_state_clear = dram_output_stream_state;
    for (uint32_t i = 0;
         i < num_dram_output_streams;
         i++, curr_dram_output_stream_state_clear++) {

      uint32_t tiles_to_clear = curr_dram_output_stream_state_clear->tiles_to_clear;
      RISC_POST_DEBUG(0x21000000 | i);
      RISC_POST_DEBUG(0x22000000 | tiles_to_clear);
      if (!tiles_to_clear) {
        continue;
      }
      
      // Reg read, flushed by immediate subsequent use
      uint32_t moves_raw_data = curr_dram_output_stream_state_clear->moves_raw_data;
      uint32_t stream_id = curr_dram_output_stream_state_clear->stream_id;
      uint32_t dram_output_no_push = curr_dram_output_stream_state_clear->stream_info->dram_io_info->dram_output_no_push;
      uint32_t phase_active = stream_phase_is_active(stream_id); 
      uint16_t tiles_available;   
      if (moves_raw_data || dram_output_no_push) {
        uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
        uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
        tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
      } else {
        if (stream_id < MULTI_MSG_TILES_STREAM_THESH) {
          tiles_available = tiles_to_clear;
        } else {
          tiles_available = stream_tiles_outstanding(stream_id);   
        }
      }     
      if (!phase_active || !tiles_available) {
        continue;
      }
      
      while(!stream_receiver_endpoint_tile_clearing_finished(stream_id));

      uint32_t num_msgs_left_in_phase = stream_get_curr_phase_num_msgs(stream_id);
      if (num_msgs_left_in_phase < tiles_available)
        tiles_available = num_msgs_left_in_phase;
      if (tiles_to_clear < tiles_available)
        tiles_available = tiles_to_clear;
      if (!tiles_available) {
        continue;
      }

      tiles_to_clear -= tiles_available;
      total_tiles_to_clear -= tiles_available;
      if (moves_raw_data || dram_output_no_push) {

        volatile uint32_t tt_reg_ptr * tiles_acked_ptr = get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
        uint16_t operand_tiles_acked = (uint16_t)tiles_acked_ptr[0];
        uint16_t new_epoch_tiles_acked = op_pack_tiles_ptr_add(operand_tiles_acked, tiles_available);
        tiles_acked_ptr[0] = new_epoch_tiles_acked;
        uint32_t curr_phase_tiles_remaining = stream_rec_endpoint_get_phase_tiles_count(stream_id);
        uint32_t total_in_flight_tiles = get_total_in_flight_tiles(curr_dram_output_stream_state_clear);
        if (!curr_phase_tiles_remaining && !total_in_flight_tiles) {
          uint32_t flags = curr_dram_output_stream_state_clear->stream_info->flags;
          if ((flags & STREAM_BRISC_PACK) != 0)
            stream_set_curr_phase_num_msgs(stream_id, 0);
          stream_force_next_phase(stream_id);
        }
      } else {
        if (stream_id < MULTI_MSG_TILES_STREAM_THESH) {
          stream_receiver_endpoint_tiles_clear_b0(stream_id, tiles_available);
        } else {
          stream_receiver_endpoint_tiles_clear(stream_id, tiles_available);
        }
      }
      curr_dram_output_stream_state_clear->tiles_to_clear = tiles_to_clear;  
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
      risc::record_timestamp(risc::perf_event::DRAM_WRITE_TILES_CLEARED | stream_id);
#endif
    }
  }
}

void process_dram_write_moves_raw_data_l1(dram_output_stream_state_t* curr_dram_output_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_issue, uint32_t stream_id,
                                       uint16_t data_send_chunk_size_tiles, uint32_t output_vc, uint32_t data_send_chunk_size_bytes, uint64_t dram_buf_addr,
                                       uint32_t& stream_rd_ptr_byte, uint32_t dram_buf_size_bytes, bool& full_q_slot_sent) {
  uint32_t untilize_copy_iters = curr_dram_output_stream_state->stream_info->untilize_copy_iters;  
  bool single_row_copy = untilize_copy_iters == 0;
  if (single_row_copy)
    untilize_copy_iters = 1;
  uint32_t num_times_to_write = mulsi3(data_send_chunk_size_tiles,untilize_copy_iters);
  risc_wait_for_cmd_buf(0, NCRISC_SMALL_TXN_CMD_BUF); //NOP on Grayskull, Worhmole A0.
                                                      //Make sure there are no long transactions on common rd/wr command buffer on Wormhole B0.
                                                      //We cannot have two NOC command buffers active simultaneously.
  ncrisc_noc_blitz_write_setup(0, NCRISC_WR_CMD_BUF_0, dram_buf_addr, data_send_chunk_size_bytes, output_vc, num_times_to_write, NCRISC_WR_DEF_TRID);
  if (!single_row_copy)
    ncrisc_noc_blitz_write_setup(0, NCRISC_WR_CMD_BUF_1, dram_buf_addr, data_send_chunk_size_bytes, output_vc, num_times_to_write, NCRISC_WR_DEF_TRID);
  for (uint32_t untilize_tiles = 0; untilize_tiles < data_send_chunk_size_tiles; untilize_tiles++) {
    uint32_t dram_wrptr_byte;
    untilize_copy<0>(next_dram_q_issue, dram_buf_addr, curr_dram_output_stream_state->stream_buf_addr_byte, stream_rd_ptr_byte,
                     data_send_chunk_size_bytes, curr_dram_output_stream_state, &dram_wrptr_byte, untilize_copy_iters, single_row_copy);
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
    risc::record_timestamp(risc::perf_event::DRAM_WRITE_SENT | (stream_id << 16) | data_send_chunk_size_bytes);
#endif

    uint32_t log_2x_untilize_copy_iters = curr_dram_output_stream_state->stream_info->log_2x_untilize_copy_iters;  
    stream_rd_ptr_byte = buf_ptr_inc_wrap(stream_rd_ptr_byte, data_send_chunk_size_bytes << log_2x_untilize_copy_iters, curr_dram_output_stream_state->stream_buf_size_bytes);

    enum move_ptr_along_t {COLUMN, ROW, ZCOLUMN, ZROW, BATCH} move_ptr_along; 

    move_ptr_along = COLUMN;
    uint32_t c_dim_count = curr_dram_output_stream_state->c_dim_count;
    c_dim_count++;
    curr_dram_output_stream_state->c_dim_count = c_dim_count;
    if (c_dim_count == curr_dram_output_stream_state->c_dim_size) {
      curr_dram_output_stream_state->c_dim_count = 0;

      move_ptr_along = ROW;
      dram_wrptr_byte = curr_dram_output_stream_state->prev_row_ptr_bytes;
      uint32_t r_dim_count = curr_dram_output_stream_state->r_dim_count;
      r_dim_count++;
      curr_dram_output_stream_state->r_dim_count = r_dim_count;
      if (r_dim_count == curr_dram_output_stream_state->r_dim_size) {
        curr_dram_output_stream_state->r_dim_count = 0;

        move_ptr_along = ZCOLUMN;
        dram_wrptr_byte = curr_dram_output_stream_state->prev_zc_ptr_bytes;
        uint32_t zc_dim_count = curr_dram_output_stream_state->zc_dim_count;
        zc_dim_count++;
        curr_dram_output_stream_state->zc_dim_count = zc_dim_count;
        if (zc_dim_count == curr_dram_output_stream_state->zc_dim_size) {
          curr_dram_output_stream_state->zc_dim_count = 0;

          move_ptr_along = ZROW;
          dram_wrptr_byte = curr_dram_output_stream_state->prev_zr_ptr_bytes;
          uint32_t zr_dim_count = curr_dram_output_stream_state->zr_dim_count;
          zr_dim_count++;
          curr_dram_output_stream_state->zr_dim_count = zr_dim_count;
          if (zr_dim_count == curr_dram_output_stream_state->zr_dim_size) {
            curr_dram_output_stream_state->zr_dim_count = 0;

            move_ptr_along = BATCH;
            dram_wrptr_byte = curr_dram_output_stream_state->prev_batch_ptr_bytes;
            uint32_t batch_dim_count = next_dram_q_issue->dram_num_partial_q_slot_issued_tiles;
            batch_dim_count++;
            next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = batch_dim_count;
            full_q_slot_sent = (batch_dim_count == curr_dram_output_stream_state->q_slot_size_tiles);
          }
        }
      }
    }

    if (move_ptr_along == COLUMN) {
      dram_wrptr_byte = buf_ptr_inc_wrap(dram_wrptr_byte, data_send_chunk_size_bytes, dram_buf_size_bytes);
    } else if (move_ptr_along == ROW) {
      dram_wrptr_byte = buf_ptr_inc_wrap(dram_wrptr_byte, curr_dram_output_stream_state->skip_col_tile_row_bytes, dram_buf_size_bytes);
      curr_dram_output_stream_state->prev_row_ptr_bytes = dram_wrptr_byte;
    } else if (move_ptr_along == ZCOLUMN) {
      dram_wrptr_byte = buf_ptr_inc_wrap(dram_wrptr_byte, curr_dram_output_stream_state->skip_zcol_bytes, dram_buf_size_bytes);
      curr_dram_output_stream_state->prev_row_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_zc_ptr_bytes = dram_wrptr_byte;
    } else if (move_ptr_along == ZROW) {
      dram_wrptr_byte = buf_ptr_inc_wrap(dram_wrptr_byte, curr_dram_output_stream_state->skip_col_zrow_bytes, dram_buf_size_bytes);
      curr_dram_output_stream_state->prev_row_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_zc_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_zr_ptr_bytes = dram_wrptr_byte;
    } else if (move_ptr_along == BATCH) {
      dram_wrptr_byte = buf_ptr_inc_wrap(dram_wrptr_byte, curr_dram_output_stream_state->skip_col_row_bytes, dram_buf_size_bytes);
      curr_dram_output_stream_state->prev_row_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_zc_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_zr_ptr_bytes = dram_wrptr_byte;
      curr_dram_output_stream_state->prev_batch_ptr_bytes = dram_wrptr_byte;
    }
    next_dram_q_issue->dram_ptr_issued_byte = dram_wrptr_byte;
  }
}
