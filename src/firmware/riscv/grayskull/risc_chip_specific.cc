#include "risc_chip_specific.h"
#ifdef PERF_DUMP
#include "risc_perf.h"
#endif

////

void init_tile_clear() {
  // set up stream 3 to do tile clearing via local interface
  config_local_receiver(TILE_CLEAR_STREAM_ID);
}

void wait_till_tile_clear_done(uint32_t stream_id) {
  while (!stream_phase_advance_wait(TILE_CLEAR_STREAM_ID));
}

void kick_off_tile_clear(uint32_t blob_index) {
  if (blob_index) {
    blob_index--;
    uint32_t val = get_misc_cfg_with_auto_config(0);
    RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][5] = BLOB_CFG_DW(val , STREAM_MISC_CFG_REG_INDEX);
    //kick off tile clear blob.
    while (!stream_phase_advance_wait(TILE_CLEAR_STREAM_ID));
    NOC_STREAM_WRITE_REG(TILE_CLEAR_STREAM_ID, STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, (uint32_t)&RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[0]);
    NOC_STREAM_WRITE_REG(TILE_CLEAR_STREAM_ID, STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX, 5 << NEXT_PHASE_NUM_CFG_REG_WRITES);
    NOC_STREAM_WRITE_REG(TILE_CLEAR_STREAM_ID, STREAM_MISC_CFG_REG_INDEX, (0x1 << PHASE_AUTO_CONFIG) | (1 << NEXT_PHASE_SRC_CHANGE) | (1 << NEXT_PHASE_DEST_CHANGE));
  }
}

void process_tile_clearing(kernel_input_stream_state_t* input_stream_state, uint32_t streams_to_clear) {
  uint32_t blob_index = 0;
  kernel_input_stream_state_t* curr_input_state = input_stream_state;
  while (streams_to_clear) {
    if (curr_input_state == input_stream_state) {
      kick_off_tile_clear(blob_index);
      blob_index = 0;
      while (!stream_phase_advance_wait(TILE_CLEAR_STREAM_ID));
    }

    curr_input_state = curr_input_state->next;

    int32_t tiles_to_clear = curr_input_state->tiles_to_clear;
    if (!tiles_to_clear) {
      continue;
    }
        
    uint32_t stream_id = curr_input_state->stream_id;
    uint32_t phase_active = stream_phase_is_active(stream_id);
    int32_t tiles_to_clear_group = tiles_to_clear;
    int32_t num_msgs_left_in_phase = stream_get_curr_phase_num_msgs(stream_id);
    if (num_msgs_left_in_phase < tiles_to_clear_group)
      tiles_to_clear_group = num_msgs_left_in_phase;
    if (!phase_active || !tiles_to_clear_group) {
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

    // set up local receiver stream for clearing
        
    if (tiles_to_clear_group <= TILE_CLEAR_THRESHOLD) {
      while (!stream_phase_advance_wait(TILE_CLEAR_STREAM_ID));
      uint64_t local_src_mask = 0x1;
      local_src_mask = local_src_mask << stream_id;
      stream_local_receiver_start_phase(stream_id, TILE_CLEAR_STREAM_ID, tiles_to_clear_group, local_src_mask); 
    } else {
      // set up local receiver stream autoconfig blob for clearing tiles
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][0] = (5 << NEXT_PHASE_NUM_CFG_REG_WRITES) | (tiles_to_clear_group << CURR_PHASE_NUM_MSGS);
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][1] = BLOB_CFG_DW(stream_get_curr_phase(stream_id), STREAM_CURR_PHASE_REG_INDEX);

      uint64_t local_src_mask = 0x1;
      local_src_mask = local_src_mask << stream_id;
      uint32_t val = local_src_mask & 0xFFFFFF;
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][2] = BLOB_CFG_DW(val, STREAM_LOCAL_SRC_MASK_REG_INDEX);
      local_src_mask >>= 24;
      val = local_src_mask & 0xFFFFFF;
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][3] = BLOB_CFG_DW(val, STREAM_LOCAL_SRC_MASK_REG_INDEX + 1);
      local_src_mask >>= 24;
      val = local_src_mask & 0xFFFFFF;
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][4] = BLOB_CFG_DW(val, STREAM_LOCAL_SRC_MASK_REG_INDEX + 2);

      val = get_misc_cfg_with_auto_config(1);
      RISC_EPOCH_INFO_PTR->tile_clear_blob.blob_words[blob_index][5] = BLOB_CFG_DW(val , STREAM_MISC_CFG_REG_INDEX);
      blob_index++;
    }
    tiles_to_clear -= tiles_to_clear_group;
    curr_input_state->tiles_to_clear = tiles_to_clear;
    uint32_t tiles_to_clear_eq_0 = (tiles_to_clear == 0);
    streams_to_clear -= tiles_to_clear_eq_0;
  }

  kick_off_tile_clear(blob_index);
}

int get_epoch_table_x(int my_x, int my_y) {
#if NO_DISTRIBUTED_EPOCH_TABLES == 1
  return 1;
#else
  int epoch_x;
  switch (my_x) {
    case 1:
    case 2:
    case 3: epoch_x = 1; break;
    case 4:
    case 5:
    case 6: epoch_x = 4; break;
    case 7:
    case 8:
    case 9: epoch_x = 7; break;
    case 10:
    case 11:
    case 12: epoch_x = 10; break;
    default: epoch_x = 1; break;
  }
  return epoch_x;
#endif
}


int get_epoch_table_y(int my_x, int my_y) {
#if NO_DISTRIBUTED_EPOCH_TABLES == 1
  return 0;
#else
  int epoch_y;
  if (my_y <= 5) {
    epoch_y = 0;
  } else {
    epoch_y = 6;
  }
  return epoch_y;
#endif
}

int get_epoch_index_x(int my_x) {
  return my_x;
}

int get_epoch_index_y(int my_y) {
  return my_y;
}

void risc_wait_for_cmd_buf(uint32_t noc, uint32_t cmd_buf)
{

}

void risc_dram_write_init(uint32_t dram_stream)
{
  
}

void risc_dram_write (uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t noc, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t len_tiles, uint32_t vc, uint32_t stream_msg_info_buf_addr, uint32_t transaction_id)
{
  ncrisc_noc_fast_write_any_len(noc, NCRISC_WR_CMD_BUF, src_addr, dest_addr, len_bytes, vc, false, false, 1, transaction_id);
}

bool risc_dram_write_ok(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream, uint32_t output_noc)
{
  return ncrisc_noc_fast_write_bufs_ok(output_noc);
}

bool risc_dram_writes_sent(uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream)
{
  return true;
}

void replicate(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id) {
  const uint32_t REPLICATE_VC = 0;
  for (uint32_t j = 0; j < times_to_replicate; j++) {
    while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                          src_addr,
                          dest_addr,
                          chunk_size_bytes,
                          REPLICATE_VC, false, false, 1, transaction_id);
    dest_addr += chunk_size_bytes;
  }
}

void replicate_l1(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t chunk_size_bytes, uint32_t times_to_replicate, uint32_t transaction_id) {
  const uint32_t REPLICATE_VC = 0;
  for (uint32_t j = 0; j < times_to_replicate; j++) {
    while (!ncrisc_noc_fast_write_ok_l1(noc_id, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write_l1(noc_id, NCRISC_WR_CMD_BUF,
                          src_addr,
                          dest_addr,
                          chunk_size_bytes,
                          REPLICATE_VC, false, false, 1, transaction_id);
    dest_addr += chunk_size_bytes;
  }
}

void dram_input_stream_issue_scatter_read_init(uint32_t data_rec_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_dest_addr, uint32_t& transaction_id) {
  uint32_t full_packets_per_chunk = dram_io_scatter_chunk_size_bytes / NOC_MAX_BURST_SIZE;
  uint32_t remainder_chunk_size = dram_io_scatter_chunk_size_bytes % NOC_MAX_BURST_SIZE;
  uint32_t magic_num_addr = stream_dest_addr;
  for (uint32_t scatter_tile = 0; scatter_tile < data_rec_chunk_size_tiles; scatter_tile += dram_io_scatter_chunk_size_tiles) {
    uint32_t addr = magic_num_addr;
    for (uint32_t p = 0; p < full_packets_per_chunk; p++) {
      addr += NOC_MAX_BURST_SIZE;
      set_packet_end_marker(addr - 4); 
    }
    if (remainder_chunk_size > 0) {
      addr += remainder_chunk_size;
      set_packet_end_marker(addr - 4);
    }
    magic_num_addr += dram_io_scatter_chunk_size_bytes;
  }
}

bool dram_input_stream_check_next_chunk_flushed(uint32_t input_noc, uint32_t chunk_pending_start_addr, uint32_t chunk_size_bytes, uint32_t scatter_chunk_size_bytes, uint32_t& transaction_id) {

  if (ncrisc_noc_reads_flushed(input_noc, NCRISC_RD_DEF_TRID)) {
    // this handles rare stalls in case of false negatives
    // (when the last dword in the packet happens to be the same as the marker)
    return true;
  }

  bool marker_check_ok = true;
  uint32_t full_packets_per_chunk = scatter_chunk_size_bytes / NOC_MAX_BURST_SIZE;
  uint32_t remainder_chunk_size = scatter_chunk_size_bytes % NOC_MAX_BURST_SIZE;
  for (uint32_t scatter_bytes = 0; scatter_bytes < chunk_size_bytes; scatter_bytes += scatter_chunk_size_bytes) {
    uint32_t addr = chunk_pending_start_addr + scatter_bytes;
    
    for (uint32_t p = 0; p < full_packets_per_chunk; p++) {
      addr += NOC_MAX_BURST_SIZE;
      if (!check_packet_end_marker(addr - 4)) {
        marker_check_ok = false;
        break;
      }
    }
    if (remainder_chunk_size > 0) {
      addr += remainder_chunk_size;
      marker_check_ok &= check_packet_end_marker(addr - 4);
    }
  }

  return marker_check_ok;
}

void process_dram_write(
  uint32_t &num_dram_output_streams, dram_output_stream_state_t *dram_output_stream_state, uint32_t &dram_ptr_update_cnt, uint32_t &total_tiles_to_clear
) {
  bool previous_writes_sent = true;
  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    previous_writes_sent = previous_writes_sent && ncrisc_noc_nonposted_writes_sent(n, NCRISC_WR_DEF_TRID);
  }

  if (previous_writes_sent) {

    dram_output_stream_state_t* curr_dram_output_stream_state = dram_output_stream_state;
    for (uint32_t i = 0; i < num_dram_output_streams; i++, curr_dram_output_stream_state++) {

      uint32_t stream_id = curr_dram_output_stream_state->stream_id;
      uint32_t epoch_q_slots_remaining = curr_dram_output_stream_state->epoch_q_slots_remaining;
      uint32_t dram_output_no_push = curr_dram_output_stream_state->stream_info->dram_io_info->dram_output_no_push;
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

      uint32_t write_stride = curr_dram_output_stream_state->write_stride & ~DRAM_STRIDE_UPDATE_WAIT;
      bool is_strided_write = curr_dram_output_stream_state->total_write_strides > 1;
      if (is_strided_write && (curr_dram_output_stream_state->write_stride & DRAM_STRIDE_UPDATE_WAIT)) {

        RISC_POST_STATUS(0xF0300000);

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
      bool cmd_buf_free = ncrisc_noc_fast_write_bufs_ok(output_noc);

      uint32_t in_flight_tiles = curr_dram_output_stream_state->in_flight_tiles;
      uint32_t in_flight_q_slots = curr_dram_output_stream_state->in_flight_q_slots;

      if (curr_dram_output_stream_state->moves_raw_data || dram_output_no_push) {
        if (phase_active && !curr_phase_tiles_remaining && !in_flight_tiles) {
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
        curr_dram_output_stream_state->in_flight_tiles = 0;
        curr_dram_output_stream_state->in_flight_q_slots = 0;
        in_flight_q_slots = 0;
        in_flight_tiles = 0;
      }

      RISC_POST_DEBUG(0xF1300000 | phase_active);
      RISC_POST_DEBUG(0xF1400000 | curr_phase_tiles_remaining);
      RISC_POST_DEBUG(0xF1500000 | output_noc);
      RISC_POST_DEBUG(0xF1600000 | cmd_buf_free);
        
      if (!epoch_q_slots_remaining || !phase_active || !curr_phase_tiles_remaining || !cmd_buf_free) {
        continue;
      }

      RISC_POST_STATUS(0xF2000000);

      dram_q_state_t tt_l1_ptr * next_dram_q_issue = curr_dram_output_stream_state->next_dram_q_issue;
      volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
      uint16_t data_send_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
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

          uint32_t data_send_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
          uint32_t wr_ptr_autoinc = l1_ptrs->dram_to_l1.rd_gwr_ptr_autoinc;
          uint32_t stream_rd_ptr_byte = stream_dram_write_should_reset_pointers(stream_id) ? 0 : curr_dram_output_stream_state->stream_rd_ptr_byte;
          wr_ptr_autoinc = wr_ptr_autoinc ? wr_ptr_autoinc : 1;

          // Reg reads, flushed by immediate subsequent use
          uint32_t output_vc = stream_get_output_unicast_vc(stream_id); // FIXME imatosevic - make sure this field is set by blob gen even for DRAM output streams
          uint32_t dram_writes_with_cmd_buf = curr_dram_output_stream_state->stream_info->dram_io_info->dram_writes_with_cmd_buf;

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
                                                                stream_msg_info_buf_addr, dram_writes_with_cmd_buf, 3);
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
                  risc_dram_write (dram_writes_with_cmd_buf, 3, output_noc, stream_rd_addr, dram_dest_addr, data_send_chunk_size_bytes, data_send_chunk_size_tiles, output_vc, stream_msg_info_buf_addr, NCRISC_WR_DEF_TRID);
                  curr_dram_output_stream_state->dram_stream_write_start_time = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
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

              if (!is_strided_write || (header_reads_flushed(output_noc, NCRISC_HEADER_RD_TRID, l1_ptr_addr) && stride_iter_matches_expr && write_stride == rd_stride)) {
                //wait for dram writes via stream 3 to finish.
                //NOP on Grayskull, Wormhole A0.
                RISC_POST_STATUS(0xF4120000);
                while (!risc_dram_writes_sent(dram_writes_with_cmd_buf, 3));
                update_dram_write_ptrs(is_ram, is_strided_write, write_stride, curr_dram_output_stream_state->total_write_strides, temp_dram_q_issue->dram_ptr_issued_q_slots, output_noc, output_vc,
                                       dram_ptr_addr, curr_dram_output_stream_state, l1_ptrs, curr_stride_wrap, next_stride_wrap);
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

            if (!risc_dram_write_ok(dram_writes_with_cmd_buf, 3, output_noc)) {
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
          curr_dram_output_stream_state->in_flight_q_slots = in_flight_q_slots;
          curr_dram_output_stream_state->in_flight_tiles = in_flight_tiles;
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
        tiles_available = stream_tiles_outstanding(stream_id);   
      }     
      if (!phase_active || !tiles_available) {
        continue;
      }
      
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
        stream_receiver_endpoint_tiles_clear(stream_id, tiles_available);
      }
      curr_dram_output_stream_state_clear->tiles_to_clear = tiles_to_clear;  
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
      risc::record_timestamp(risc::perf_event::DRAM_WRITE_TILES_CLEARED | stream_id);
#endif
    }
  }
}
