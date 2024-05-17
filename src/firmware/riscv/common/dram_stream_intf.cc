// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "dram_stream_intf.h"
#include "risc_common.h"
#include "risc.h"
#include "epoch.h"
#include "noc_nonblocking_api.h"
#include "risc_chip_specific.h"

#ifndef ERISC
#include "context.h"
#else
#include "unpack_pack_stream_intf.h"
#include "eth_init.h"
#endif

#ifdef PERF_DUMP
#include "risc_perf.h"
#endif

using namespace std;

////
#ifndef NUM_EXEC_LOOP_ITERATIONS
#define NUM_EXEC_LOOP_ITERATIONS 1
#endif
constexpr bool HACK_FW_LOOPING_ENABLED = NUM_EXEC_LOOP_ITERATIONS > 1;

#define ERISC_LOOP_ITER_COUNT 50

////
#ifdef DRAM_DECOUPLE
extern uint32_t noc_read_scratch_buf[32];
#endif


void read_phase_active(uint32_t stream_id, dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t& phase_active, uint32_t& curr_phase_tiles_remaining_to_issue) {
  // reg read, flushed by subsequent use
  phase_active = stream_phase_is_active(stream_id) && !is_dummy_phase(stream_id);
  curr_phase_tiles_remaining_to_issue = stream_src_endpoint_get_phase_tiles_count(stream_id);
  uint32_t fork_index = 0;
  while (phase_active && curr_phase_tiles_remaining_to_issue) {
    uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
    if (fork_stream_id == NULL_STREAM) {
      break;
    }
    // reg read, flushed by use in loop condition
    phase_active &= stream_phase_is_active(fork_stream_id) && !is_dummy_phase(fork_stream_id);
    uint32_t fork_phase_tiles_remaining_to_issue = stream_src_endpoint_get_phase_tiles_count(fork_stream_id);
    if (fork_phase_tiles_remaining_to_issue < curr_phase_tiles_remaining_to_issue)
      curr_phase_tiles_remaining_to_issue = fork_phase_tiles_remaining_to_issue;
    fork_index++;
  }
}


void update_dram_write_ptrs(bool is_ram, bool is_strided_write, uint32_t write_stride, uint32_t total_write_strides, uint32_t dram_wrptr_q_slots, uint32_t output_noc, uint32_t output_vc, 
                            uint64_t dram_buf_addr, dram_output_stream_state_t* curr_dram_output_stream_state, volatile dram_io_state_t tt_l1_ptr * l1_ptrs, uint32_t curr_stride_wrap, uint32_t next_stride_wrap) {
  if (!is_strided_write || write_stride == total_write_strides-1) {
    l1_ptrs->l1_to_dram.wr_dram_wrptr = dram_wrptr_q_slots;
    if (!is_ram) {
      while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_dram_wrptr)), dram_buf_addr+DRAM_BUF_WRPTR_OFFSET, 2,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
    }
#ifdef EPOCH_CHECK_ENABLED
    if (l1_ptrs->dram_to_l1.rd_epoch_id_tag != DRAM_QUEUE_NO_EPOCH_CHECK)
      l1_ptrs->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_EPOCH_CHECK_EN | (curr_dram_output_stream_state->epoch_id);
    else
      l1_ptrs->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
#else
      // No epoch check works around #928, however it will not support multiple epoch consumers that need to sync on the same queue
      l1_ptrs->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
#endif
    if (is_strided_write) {
      l1_ptrs->local.stride_wrap = next_stride_wrap;
      l1_ptrs->l1_to_dram.wr_stride = next_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0;
      l1_ptrs->dram_to_l1.rd_stride = next_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0;
      while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_epoch_id_tag)), dram_buf_addr+DRAM_BUF_EPOCH_ID_TAG_OFFSET, 4,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
    } else {
      while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_epoch_id_tag)), dram_buf_addr+DRAM_BUF_EPOCH_ID_TAG_OFFSET, 2,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
    }
  } else {
    l1_ptrs->local.stride_wrap = next_stride_wrap;
    l1_ptrs->l1_to_dram.wr_stride = curr_stride_wrap + write_stride + 1;
    l1_ptrs->dram_to_l1.rd_stride = curr_stride_wrap + write_stride + 1;
    while (!ncrisc_noc_fast_write_ok(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
    ncrisc_noc_fast_write(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_stride)), dram_buf_addr+DRAM_BUF_STRIDE_OFFSET, 2,
                          output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
  }
}


void push_readdata_to_stream(dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t stream_id, bool moves_raw_data,
                             uint32_t prev_data_rec_chunk_size_tiles, uint32_t prev_data_rec_chunk_size_bytes) {
  if (moves_raw_data) {
    epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info = curr_dram_input_stream_state->stream_info->dram_io_info;
    volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
    uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
    uint32_t scatter_list_stream_id = dram_io_info->scatter_list_stream_id;
    uint32_t hw_tilize = dram_io_info->hw_tilize;

    if (scatter_list_stream_id || hw_tilize) {
      prev_data_rec_chunk_size_tiles = dram_io_info->dram_embeddings_row_tiles;
    }

    uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, prev_data_rec_chunk_size_tiles);
    tiles_received_ptr[0] = new_epoch_tiles_received;
  } else {
    stream_signal_flushed_tiles(stream_id, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes/MEM_WORD_WIDTH);
  }
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
  risc::record_timestamp(risc::perf_event::DRAM_READ_TILE_FLUSHED | (stream_id << 16) | prev_data_rec_chunk_size_bytes);
#endif

  uint32_t fork_index = 0;
  while (true) {
    uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
    if (fork_stream_id == NULL_STREAM) {
      break;
    }
    if (moves_raw_data) {
      volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(fork_stream_id));
      uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
      uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, prev_data_rec_chunk_size_tiles);
      tiles_received_ptr[0] = new_epoch_tiles_received;
    } else {
      stream_signal_flushed_tiles(fork_stream_id, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes/MEM_WORD_WIDTH);
    }
    fork_index++;
  }
}

void push_readdata_to_stream_l1(dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t stream_id, bool moves_raw_data,
                                uint32_t prev_data_rec_chunk_size_tiles, uint32_t prev_data_rec_chunk_size_bytes) {
  if (moves_raw_data) {
    epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info = curr_dram_input_stream_state->stream_info->dram_io_info;
    volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
    uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
    uint32_t scatter_list_stream_id = dram_io_info->scatter_list_stream_id;
    uint32_t hw_tilize = dram_io_info->hw_tilize;

    if (scatter_list_stream_id || hw_tilize) {
      prev_data_rec_chunk_size_tiles = dram_io_info->dram_embeddings_row_tiles;
    }

    uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, prev_data_rec_chunk_size_tiles);
    tiles_received_ptr[0] = new_epoch_tiles_received;
  } else {
    stream_signal_flushed_tiles(stream_id, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes/MEM_WORD_WIDTH);
  }
#if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
  risc::record_timestamp_l1(risc::perf_event::DRAM_READ_TILE_FLUSHED | (stream_id << 16) | prev_data_rec_chunk_size_bytes);
#endif

  uint32_t fork_index = 0;
  while (true) {
    uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
    if (fork_stream_id == NULL_STREAM) {
      break;
    }
    if (moves_raw_data) {
      volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(fork_stream_id));
      uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
      uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, prev_data_rec_chunk_size_tiles);
      tiles_received_ptr[0] = new_epoch_tiles_received;
    } else {
      stream_signal_flushed_tiles(fork_stream_id, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes/MEM_WORD_WIDTH);
    }
    fork_index++;
  }
}

void get_buf_space_available_words(dram_input_stream_state_t* curr_dram_input_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_issue, uint32_t stream_id, bool moves_raw_data, 
                                   uint32_t &stream_buf_bytes_free, bool &stream_has_free_space) {
  volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
  if (moves_raw_data) {
    epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info = curr_dram_input_stream_state->stream_info->dram_io_info;
    uint32_t scatter_list_stream_id = dram_io_info->scatter_list_stream_id;
    uint32_t hw_tilize = dram_io_info->hw_tilize;
    uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
    uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
    uint16_t tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
    // This is done because operand tiles is in tile format, whereas ncrisc uses row based tiles. We dont care for perf because these reads are very small
    if ((scatter_list_stream_id || hw_tilize) && tiles_available != 0) {
      stream_buf_bytes_free = 0;
      stream_has_free_space = false;
      return;
    }
    uint32_t data_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
    uint32_t stream_buf_size_tiles = curr_dram_input_stream_state->stream_info->buf_size_tiles;
    uint32_t stream_buf_free_tiles = stream_buf_size_tiles - tiles_available;
    uint32_t in_flight_data_rec_chunks = curr_dram_input_stream_state->in_flight_data_rec_chunks;
    uint32_t in_flight_tiles = mulsi3(in_flight_data_rec_chunks,data_chunk_size_tiles);
    uint32_t stream_buf_tiles_free_thr = (data_chunk_size_tiles + in_flight_tiles);
    uint32_t stream_msg_info_buf_ptr = (curr_dram_input_stream_state->stream_info->msg_info_buf_start)*MEM_WORD_WIDTH;
    uint32_t tile_size_words = *((int *)(stream_msg_info_buf_ptr));
    stream_buf_bytes_free = mulsi3(stream_buf_free_tiles, tile_size_words) * MEM_WORD_WIDTH;
    stream_has_free_space = (stream_buf_free_tiles >= stream_buf_tiles_free_thr);
  } else {
    uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
    uint32_t in_flight_bytes = curr_dram_input_stream_state->in_flight_bytes;
    uint32_t stream_buf_bytes_free_thr = (data_rec_chunk_size_bytes + in_flight_bytes);
    stream_buf_bytes_free = (stream_get_buf_space_available_words(stream_id) * MEM_WORD_WIDTH);
    stream_has_free_space = (stream_buf_bytes_free >= stream_buf_bytes_free_thr);
    uint32_t fork_index = 0;
    while (stream_has_free_space) {
      uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
      if (fork_stream_id == NULL_STREAM) {
        break;
      }
      // reg read, flushed by use in loop condition
      uint32_t fork_stream_buf_bytes_free = (stream_get_buf_space_available_words(fork_stream_id) * MEM_WORD_WIDTH);
      stream_has_free_space &= (fork_stream_buf_bytes_free >= stream_buf_bytes_free_thr);
      if (fork_stream_buf_bytes_free < stream_buf_bytes_free)
        stream_buf_bytes_free = fork_stream_buf_bytes_free;
      fork_index++;
    }
  }
}

bool get_stream_push_flushed_l1(dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t stream_id, uint32_t data_rec_chunk_size_bytes) {
  bool all_idle = stream_phase_advance_wait(stream_id);
  uint32_t stream_buf_bytes_free = (stream_get_buf_space_available_words(stream_id) * MEM_WORD_WIDTH);
  uint32_t fork_index = 0;
  while (true) {
    uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
    if (fork_stream_id == NULL_STREAM) {
      break;
    }
    // reg read, flushed by use in loop condition
    all_idle = all_idle && stream_phase_advance_wait(fork_stream_id);
    uint32_t fork_stream_buf_bytes_free = (stream_get_buf_space_available_words(fork_stream_id) * MEM_WORD_WIDTH);
    if (fork_stream_buf_bytes_free < stream_buf_bytes_free)
      stream_buf_bytes_free = fork_stream_buf_bytes_free;
    fork_index++;
  }

  uint32_t prev_stream_buf_bytes_free = curr_dram_input_stream_state->stream_flushed_ptr_byte;
  prev_stream_buf_bytes_free += data_rec_chunk_size_bytes;

  if (stream_buf_bytes_free >= prev_stream_buf_bytes_free || all_idle) {
    curr_dram_input_stream_state->stream_flushed_ptr_byte = prev_stream_buf_bytes_free;
    return true;
  } else {
    return false;
  }
}

void clear_scatter_offsets(uint32_t& scatter_list_input_index_saved, dram_q_state_t tt_l1_ptr * next_dram_q_issue, dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state, 
                           epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info,
                           uint32_t dram_io_scatter_size, uint32_t& scatter_idx, uint32_t scatter_list_stream_id) {
  if (scatter_list_stream_id) {
    uint32_t scatter_list_input_index = scatter_list_input_index_saved;
    scatter_list_input_index += scatter_idx;
    uint32_t scatter_list_indicies_per_tile = dram_io_info->scatter_list_indicies_per_tile;
    uint32_t scatter_list_indicies_per_input = dram_io_info->scatter_list_indicies_per_input;
    if (scatter_idx == scatter_list_indicies_per_tile || scatter_list_input_index == scatter_list_indicies_per_input) {
      stream_receiver_endpoint_single_clear_op(scatter_list_stream_id, 1);
      scatter_idx = 0;
      if (scatter_list_input_index == scatter_list_indicies_per_input) {
        scatter_list_input_index = 0;
      }
      scatter_list_input_index_saved = scatter_list_input_index;
    }
    next_dram_q_issue->scatter_offsets_index = scatter_idx;
  } else {
    if (scatter_idx >= dram_io_scatter_size)
      scatter_idx -= dram_io_scatter_size;
    next_dram_q_issue->scatter_offsets_index = scatter_idx;
  }
}


inline __attribute__((always_inline)) void decompress_scatter_offsets(volatile tt_uint64_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx, uint32_t& scatter_loop_index, uint32_t& scatter_loop_inc, volatile dram_scatter_loop_t tt_l1_ptr * dram_scatter_loop) {
  uint32_t num_iter = dram_scatter_loop->num_iter;
  uint32_t magic_number = num_iter & DRAM_IO_IS_SCATTER_LOOP;
  if (magic_number) {
    num_iter = num_iter & ~DRAM_IO_IS_SCATTER_LOOP;
    if (scatter_loop_index < num_iter) {
      uint32_t inc = dram_scatter_loop->inc;
      uint32_t num_index_back = dram_scatter_loop->num_index_back;
      scatter_loop_index++;
      scatter_loop_inc += inc;
      scatter_idx -= num_index_back;
    } else {
      scatter_loop_index = 0;
      scatter_loop_inc = 0;
      scatter_idx += DRAM_IO_IS_SCATTER_LOOP_SIZE;
    }
  }
}


inline __attribute__((always_inline)) bool simpler_decompress_scatter_offsets(volatile uint32_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx, uint32_t& scatter_loop_index) {
  uint32_t num_iter = scatter_offsets[scatter_idx];
  uint32_t magic_number = (num_iter & DRAM_IO_IS_SCATTER_LOOP) && (num_iter != DRAM_SCATTER_LIST_INVALID);
  if (magic_number) {
    num_iter = num_iter & ~DRAM_IO_IS_SCATTER_LOOP;
    if (scatter_loop_index < num_iter) {
      scatter_loop_index++;
      scatter_idx -= 1;
    } else {
      scatter_loop_index = 0;
      scatter_idx += 1;
    }
    return true;
  }
  return false;
}

// dram_input_stream_issue_scatter_read implements Scatter Read for DRAM input streams 
#ifdef RISC_B0_HW
void __attribute__((section("code_l1"))) __attribute__ ((noinline)) dram_input_stream_issue_scatter_read(uint32_t input_noc, uint32_t dram_data_buf_fetch_rdptr_byte, uint32_t data_rec_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles,
#else
void __attribute__ ((noinline)) dram_input_stream_issue_scatter_read(uint32_t input_noc, uint32_t dram_data_buf_fetch_rdptr_byte, uint32_t data_rec_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles,
#endif
                                                                      uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_dest_addr, volatile tt_uint64_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx,
                                                                      uint32_t& scatter_loop_index, uint32_t& scatter_loop_inc, uint32_t transaction_id
                                                                      ) {
  uint32_t addr_with_offset = dram_data_buf_fetch_rdptr_byte + DRAM_BUF_DATA_OFFSET;
  for (uint32_t scatter_tile = 0; scatter_tile < data_rec_chunk_size_tiles; scatter_tile += dram_io_scatter_chunk_size_tiles) {
    // Note: keep () around the 32 bit addition to add before promotion to 64 bit
    uint64_t dram_src_addr_w_offset = tt_l1_load(&scatter_offsets[scatter_idx]);
    bool is_padding_set = ((dram_src_addr_w_offset >> 32) & IS_DRAM_PADDING_SET) != 0;
    dram_src_addr_w_offset = dram_src_addr_w_offset & ~(((uint64_t)IS_DRAM_PADDING_SET) << 32);
    if (!is_padding_set)
      dram_src_addr_w_offset += (addr_with_offset + scatter_loop_inc);
    scatter_idx++;
    volatile dram_scatter_loop_t tt_l1_ptr * dram_scatter_loop = (volatile dram_scatter_loop_t tt_l1_ptr *)&scatter_offsets[scatter_idx];
    ncrisc_noc_fast_read_any_len_scatter(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_src_addr_w_offset, stream_dest_addr, dram_io_scatter_chunk_size_bytes, transaction_id);

    stream_dest_addr += dram_io_scatter_chunk_size_bytes;

    decompress_scatter_offsets(scatter_offsets, scatter_idx, scatter_loop_index, scatter_loop_inc, dram_scatter_loop);
  }
}

// dram_input_stream_issue_scatter_read_indicies implements Tilize 
void __attribute__ ((noinline)) dram_input_stream_issue_scatter_read_indicies(bool hw_tilize, uint32_t input_noc, uint32_t dram_data_buf_fetch_rdptr_byte, uint32_t data_rec_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles,
                                                                              uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_dest_addr, volatile tt_uint64_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx, uint32_t& scatter_loop_index,
                                                                              uint64_t dram_addr, uint32_t dram_embeddings_row_shift, uint32_t tilize_row_col_offset, uint32_t& c_dim_count, uint32_t c_dim_size, uint32_t c_dim_loop_num_rows, uint32_t& col_offset_bytes, uint32_t transaction_id) {
  bool wait_for_zeros = false;
  uint32_t addr_with_offset = dram_data_buf_fetch_rdptr_byte + DRAM_BUF_DATA_OFFSET;
  for (uint32_t scatter_tile = 0; scatter_tile < data_rec_chunk_size_tiles; scatter_tile += dram_io_scatter_chunk_size_tiles) {
    uint64_t dram_src_addr_w_offset;
    uint32_t scatter_offset_val_inc = 1;

    volatile uint32_t tt_l1_ptr * new_scatter_offsets = (volatile uint32_t tt_l1_ptr *)(scatter_offsets);
    uint32_t scatter_offset_val = new_scatter_offsets[scatter_idx];
    scatter_idx++;
    if (scatter_offset_val == DRAM_SCATTER_LIST_INVALID) {
      uint64_t replicate_dest_addr = NOC_XY_ADDR(my_x[input_noc], my_y[input_noc], stream_dest_addr);
      uint32_t bytes_left = dram_io_scatter_chunk_size_bytes;
      const uint32_t REPLICATE_VC = 0;
      while (bytes_left) {
        uint32_t trans_size_bytes = bytes_left >= ZERO_GRAD_CHUNK_SIZE_BYTES ? ZERO_GRAD_CHUNK_SIZE_BYTES : bytes_left;
        while (!ncrisc_noc_fast_write_ok(input_noc, NCRISC_WR_CMD_BUF));
        ncrisc_noc_fast_write(input_noc, NCRISC_WR_CMD_BUF,
                              l1_mem::address_map::ZEROS_BASE,
                              replicate_dest_addr,
                              trans_size_bytes,
                              REPLICATE_VC, false, false, 1, NCRISC_WR_LOCAL_TRID);
        replicate_dest_addr += trans_size_bytes;
        bytes_left -= trans_size_bytes;
      }
      wait_for_zeros = true;
    } else {
      if (hw_tilize && scatter_loop_index > 0) {
        uint32_t prev_scatter_offset_val = new_scatter_offsets[scatter_idx-2]; // -2 due to the increment above
        scatter_offset_val_inc = scatter_offset_val - prev_scatter_offset_val;
        scatter_offset_val += mulsi3(scatter_loop_index, scatter_offset_val_inc);
      }
      dram_src_addr_w_offset = dram_addr + (mulsi3(scatter_offset_val,dram_embeddings_row_shift) + addr_with_offset + col_offset_bytes + tilize_row_col_offset);
      ncrisc_noc_fast_read_any_len(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_src_addr_w_offset, stream_dest_addr, dram_io_scatter_chunk_size_bytes, transaction_id);
    }

    stream_dest_addr += dram_io_scatter_chunk_size_bytes;

    uint32_t row_idx = hw_tilize ? scatter_offset_val + scatter_offset_val_inc : scatter_idx;
    uint32_t prev_scatter_loop_index = scatter_loop_index;

    bool done_simpler_decompress_scatter_offsets = false;
    if (hw_tilize)
      done_simpler_decompress_scatter_offsets = simpler_decompress_scatter_offsets(new_scatter_offsets, scatter_idx, scatter_loop_index);

    // c_dim_loop_num_rows is always power of 2 so rather than doing a mod, we can just check if the lower bits are 0
    if ((row_idx & (c_dim_loop_num_rows-1)) == 0) {
      c_dim_count++;
      if (c_dim_count >= c_dim_size) {
        col_offset_bytes = 0;
        c_dim_count = 0;
      } else {
        col_offset_bytes += dram_io_scatter_chunk_size_bytes;
        if (done_simpler_decompress_scatter_offsets)
          scatter_idx -= (c_dim_loop_num_rows-prev_scatter_loop_index+1);
        else
          scatter_idx -= c_dim_loop_num_rows;
      }
    }

  }

  if (wait_for_zeros)
    while (!ncrisc_noc_nonposted_writes_flushed(input_noc, NCRISC_WR_LOCAL_TRID));
}

void dram_output_stream_issue_scatter_write_indicies(uint32_t output_noc, uint32_t output_vc, uint32_t dram_data_buf_wr_offset_byte, uint32_t data_send_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles,
                                                     uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_src_addr, volatile tt_uint64_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx,
                                                     uint64_t dram_addr, uint32_t dram_embeddings_row_shift, uint32_t& c_dim_count, uint32_t c_dim_size, uint32_t c_dim_loop_num_rows, uint32_t& col_offset_bytes, uint32_t transaction_id,
                                                     uint32_t stream_msg_info_buf_addr, uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream) {
  uint32_t addr_with_offset = dram_data_buf_wr_offset_byte + DRAM_BUF_DATA_OFFSET;
  for (uint32_t scatter_tile = 0; scatter_tile < data_send_chunk_size_tiles; scatter_tile += dram_io_scatter_chunk_size_tiles) {
    uint64_t dram_dst_addr_w_offset;

    volatile uint32_t tt_l1_ptr * new_scatter_offsets = (volatile uint32_t tt_l1_ptr *)(scatter_offsets);
    uint32_t scatter_offset_val = new_scatter_offsets[scatter_idx];
    scatter_idx++;
    
    dram_dst_addr_w_offset = dram_addr + (mulsi3(scatter_offset_val,dram_embeddings_row_shift) + addr_with_offset + col_offset_bytes);
    risc_dram_write (dram_writes_with_cmd_buf, dram_stream, output_noc, stream_src_addr, dram_dst_addr_w_offset, dram_io_scatter_chunk_size_bytes, dram_io_scatter_chunk_size_tiles, output_vc, stream_msg_info_buf_addr, NCRISC_WR_DEF_TRID);

    stream_src_addr += dram_io_scatter_chunk_size_bytes;

    // c_dim_loop_num_rows is always power of 2 so rather than doing a mod, we can just check if the lower bits are 0
    if ((scatter_idx & (c_dim_loop_num_rows-1)) == 0) {
      c_dim_count++;
      if (c_dim_count >= c_dim_size) {
        col_offset_bytes = 0;
        c_dim_count = 0;
      } else {
        col_offset_bytes += dram_io_scatter_chunk_size_bytes;
        scatter_idx -= c_dim_loop_num_rows;
      }
    }
  }
}

void dram_input_stream_scatter_read(uint32_t stream_id, uint32_t curr_phase_tiles_remaining_to_issue, uint32_t stream_buf_bytes_free, dram_input_stream_state_t* curr_dram_input_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_issue,
                                    uint32_t &dram_ptr_update_cnt) {
  dram_q_state_t tt_l1_ptr * first_dram_q_issue;
  uint32_t prev_in_flight_data_rec_chunks = curr_dram_input_stream_state->in_flight_data_rec_chunks;
  uint32_t in_flight_bytes = curr_dram_input_stream_state->in_flight_bytes;
  bool is_dram_read_opt_enabled = curr_dram_input_stream_state->dram_read_opt_enabled || stream_is_dram_read_opt_enabled(stream_id);
  volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
  uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
  dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state = l1_ptrs->local.dram_io_scatter_state;

  uint32_t min_dram_io_entries = curr_dram_input_stream_state->min_dram_io_entries;
  bool all_dram_q_slot_available = true;
  if (min_dram_io_entries == 0) {
    min_dram_io_entries = ~0;
    first_dram_q_issue = next_dram_q_issue;
    while(true) {
      if (!l1_ptrs->l1_to_dram.dram_padding) {
        uint32_t rdptr_val_issue = next_dram_q_issue->dram_ptr_issued_q_slots;
        uint32_t wrptr_val_issue = next_dram_q_issue->dram_ptr_incoming_q_slots & ~DRAM_PTR_UPDATE_PENDING_MASK;
        uint32_t dram_buf_size_q_slots = next_dram_q_issue->dram_buf_size_q_slots;
        uint32_t this_dram_io_entries = dram_io_entries(rdptr_val_issue, wrptr_val_issue, dram_buf_size_q_slots);
        uint16_t rd_epoch_id_tag = l1_ptrs->dram_to_l1.rd_epoch_id_tag;

        if (this_dram_io_entries < min_dram_io_entries)
          min_dram_io_entries = this_dram_io_entries;

        bool dram_q_slot_available = ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0 || !dram_io_local_empty(rdptr_val_issue, l1_ptrs->dram_to_l1.rd_dram_rdptr, wrptr_val_issue)) &&
            (rd_epoch_id_tag == DRAM_QUEUE_NO_EPOCH_CHECK || rd_epoch_id_tag == curr_dram_input_stream_state->stream_info->producer_epoch_id);
        all_dram_q_slot_available = all_dram_q_slot_available && dram_q_slot_available;
      }

      next_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue->next;
      l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
      if (next_dram_q_issue == first_dram_q_issue || !all_dram_q_slot_available) {
        break;
      }
    }   
    next_dram_q_issue = first_dram_q_issue;
  }

  if (min_dram_io_entries <= DRAM_MIN_ENTIRES_POLL && (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK))
    dram_ptr_update_cnt = 0;

  if (all_dram_q_slot_available) {
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
    if (curr_dram_input_stream_state->io_q_empty) {
      risc::record_timestamp(risc::perf_event::DRAM_IO_Q_STATUS | stream_id);
      curr_dram_input_stream_state->io_q_empty = 0;
    }
  #endif

    RISC_POST_STATUS(0xD3000000);
    uint32_t stream_buf_size_bytes = stream_get_data_buf_size(stream_id) * MEM_WORD_WIDTH;
    uint32_t stream_flushed_ptr_byte = curr_dram_input_stream_state->stream_flushed_ptr_byte;
    uint32_t stream_wr_ptr_byte = buf_ptr_inc_wrap(stream_flushed_ptr_byte, in_flight_bytes, stream_buf_size_bytes);
    uint32_t in_flight_data_rec_chunks = prev_in_flight_data_rec_chunks;
    epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info = curr_dram_input_stream_state->stream_info->dram_io_info;
    uint64_t dram_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
    uint32_t data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
    uint32_t q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;
    uint32_t dram_data_buf_size_bytes = l1_ptrs->local.dram_buf_size_bytes;
#ifdef DRAM_DECOUPLE
    uint32_t dram_decoupled = l1_ptrs->dram_to_l1.dram_decoupled;
#else
    uint32_t dram_decoupled = 0;
#endif
    uint32_t dram_io_scatter_chunk_size_bytes = dram_io_scatter_state->scatter_chunk_size_bytes;
    uint32_t dram_io_scatter_size = dram_io_scatter_state->scatter_offsets_size;
    uint32_t rd_ptr_autoinc = (next_dram_q_issue->dram_q_state_flags & DRAM_Q_RAM) != 0 ? l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc : l1_ptrs->dram_to_l1.rd_lrd_ptr_autoinc;

    volatile tt_uint64_t tt_l1_ptr * scatter_offsets = dram_io_scatter_state->scatter_offsets;
    bool has_scatter_offsets = get_scatter_offsets(dram_io_scatter_state, scatter_offsets, dram_io_info->scatter_list_stream_id);

    RISC_POST_DEBUG(in_flight_bytes);
    RISC_POST_DEBUG(0xD4100000 | in_flight_data_rec_chunks);
    RISC_POST_DEBUG(stream_flushed_ptr_byte);
    RISC_POST_DEBUG(stream_wr_ptr_byte);
    RISC_POST_DEBUG(data_rec_chunk_size_tiles);
    RISC_POST_DEBUG(0xD7000000);

    uint32_t dram_embeddings_row_shift = dram_io_info->dram_embeddings_row_shift;
    uint32_t dram_io_scatter_chunk_size_tiles = dram_io_scatter_state->scatter_chunk_size_tiles;
    if (has_scatter_offsets) {

#ifndef PERF_DUMP
      RISC_POST_STATUS(0xD4000000);
#endif

      //set_dont_poll_immediately(); // Maybe needed in the future
      dram_ptr_update_cnt = dram_ptr_update_cnt | (DRAM_PTR_UPDATE_MASK + 1);

      uint32_t stream_dest_addr = curr_dram_input_stream_state->stream_buf_addr_byte + stream_wr_ptr_byte;
      uint32_t scatter_idx = next_dram_q_issue->scatter_offsets_index;
      if (dram_decoupled == 0) {
        uint32_t transaction_id = curr_dram_input_stream_state->transaction_id_issued;
        dram_input_stream_issue_scatter_read_init(data_rec_chunk_size_tiles, dram_io_scatter_chunk_size_tiles, dram_io_scatter_chunk_size_bytes, stream_dest_addr, transaction_id);
        curr_dram_input_stream_state->transaction_id_issued = transaction_id;
        curr_dram_input_stream_state->prev_rd_data_word_recv = ncrisc_rd_data_word_recv(curr_dram_input_stream_state->input_noc);
        if (dram_io_info->scatter_list_stream_id || dram_io_info->hw_tilize) {
          uint32_t c_dim_count = curr_dram_input_stream_state->c_dim_count;
          uint32_t c_dim_size = curr_dram_input_stream_state->c_dim_size;
          uint32_t col_offset_bytes = curr_dram_input_stream_state->col_offset_bytes;
          uint32_t scatter_loop_index = curr_dram_input_stream_state->scatter_loop_index;
          // Embeddings and Tilize
          dram_input_stream_issue_scatter_read_indicies(dram_io_info->hw_tilize, curr_dram_input_stream_state->input_noc, next_dram_q_issue->dram_ptr_issued_byte, data_rec_chunk_size_tiles, dram_io_scatter_chunk_size_tiles,
                                                        dram_io_scatter_chunk_size_bytes,  stream_dest_addr, scatter_offsets, scatter_idx, scatter_loop_index, dram_addr, dram_embeddings_row_shift, dram_io_info->hw_tilize ? dram_io_info->tilize_row_col_offset : 0,
                                                        c_dim_count, c_dim_size, dram_io_info->c_dim_loop_num_rows, col_offset_bytes, transaction_id);
          curr_dram_input_stream_state->scatter_loop_index = scatter_loop_index;
          curr_dram_input_stream_state->c_dim_count = c_dim_count;
          curr_dram_input_stream_state->col_offset_bytes = col_offset_bytes;
        } else {
          uint32_t scatter_loop_index = curr_dram_input_stream_state->scatter_loop_index;
          uint32_t scatter_loop_inc = curr_dram_input_stream_state->scatter_loop_inc;
          // Scatter read [and regular]
          dram_input_stream_issue_scatter_read(curr_dram_input_stream_state->input_noc, next_dram_q_issue->dram_ptr_issued_byte, data_rec_chunk_size_tiles, dram_io_scatter_chunk_size_tiles,
                                               dram_io_scatter_chunk_size_bytes,  stream_dest_addr, scatter_offsets, scatter_idx, scatter_loop_index, scatter_loop_inc, transaction_id);
          curr_dram_input_stream_state->scatter_loop_index = scatter_loop_index;
          curr_dram_input_stream_state->scatter_loop_inc = scatter_loop_inc;
        }
      }
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
      risc::record_timestamp(risc::perf_event::DRAM_READ_ISSUED | (stream_id << 16) | data_rec_chunk_size_bytes);
  #endif

      uint32_t dram_num_partial_q_slot_issued_tiles = next_dram_q_issue->dram_num_partial_q_slot_issued_tiles;
      dram_num_partial_q_slot_issued_tiles += data_rec_chunk_size_tiles;
      next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = dram_num_partial_q_slot_issued_tiles;

      clear_scatter_offsets(curr_dram_input_stream_state->scatter_list_input_index, next_dram_q_issue, dram_io_scatter_state, dram_io_info, dram_io_scatter_size, scatter_idx, dram_io_info->scatter_list_stream_id);

      stream_wr_ptr_byte = buf_ptr_inc_wrap(stream_wr_ptr_byte, data_rec_chunk_size_bytes, stream_buf_size_bytes);
      in_flight_data_rec_chunks++;
      in_flight_bytes += data_rec_chunk_size_bytes;
      if (!is_dram_read_opt_enabled) {
        curr_phase_tiles_remaining_to_issue -= data_rec_chunk_size_tiles;
      }

      bool full_q_slot_recv = dram_num_partial_q_slot_issued_tiles == q_slot_size_tiles;
      if (full_q_slot_recv) {
        uint32_t q_slot_size_bytes = dram_io_scatter_state->q_slot_size_bytes;
        uint32_t rdptr_val_issue = next_dram_q_issue->dram_ptr_issued_q_slots;
        first_dram_q_issue = next_dram_q_issue;
        min_dram_io_entries--;

        next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = 0;

        uint32_t dram_data_buf_fetch_rdptr_byte = next_dram_q_issue->dram_ptr_issued_byte;
        dram_data_buf_fetch_rdptr_byte = buf_ptr_inc_wrap(dram_data_buf_fetch_rdptr_byte, mulsi3(rd_ptr_autoinc,q_slot_size_bytes), dram_data_buf_size_bytes);

        rdptr_val_issue = dram_io_incr_ptr(rdptr_val_issue, rd_ptr_autoinc, next_dram_q_issue->dram_buf_size_q_slots);

        while(true) {
          next_dram_q_issue->dram_ptr_issued_byte = dram_data_buf_fetch_rdptr_byte;
          next_dram_q_issue->dram_ptr_issued_q_slots = rdptr_val_issue;
          next_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue->next;
          if (next_dram_q_issue == first_dram_q_issue) {
            break;
          }
        }
      }
    }

    curr_dram_input_stream_state->min_dram_io_entries = min_dram_io_entries;
    curr_dram_input_stream_state->in_flight_data_rec_chunks = in_flight_data_rec_chunks;
    curr_dram_input_stream_state->in_flight_bytes = in_flight_bytes;
    if (!is_dram_read_opt_enabled) {
      stream_src_endpoint_set_phase_tiles_count(stream_id, curr_phase_tiles_remaining_to_issue);

      uint32_t fork_index = 0;
      while (true) {
        uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
        if (fork_stream_id == NULL_STREAM) {
          break;
        }
        stream_src_endpoint_set_phase_tiles_count(fork_stream_id, curr_phase_tiles_remaining_to_issue);
        fork_index++;
      }
    }
  }
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
  else {
    if (!curr_dram_input_stream_state->io_q_empty) {
      risc::record_timestamp(risc::perf_event::DRAM_IO_Q_STATUS | stream_id);
      curr_dram_input_stream_state->io_q_empty = 1;
    }
  }
  #endif
}


bool dram_input_stream_scatter_queue_update(uint32_t stream_id, uint32_t &epoch_q_slots_remaining, uint32_t moves_raw_data, uint32_t input_noc, uint32_t prev_data_rec_chunk_size_tiles, uint32_t prev_data_rec_chunk_size_bytes, 
                                            dram_input_stream_state_t* curr_dram_input_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_in_flight, uint32_t &dram_ptr_update_cnt, uint32_t &flushed_partial_q_slot_tiles) {
  dram_q_state_t tt_l1_ptr * first_dram_q_in_flight;
  volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_in_flight->l1_dram_ptrs;
  uint32_t dram_buf_size_q_slots = next_dram_q_in_flight->dram_buf_size_q_slots;
  uint32_t rdptr_flushed_val = next_dram_q_in_flight->dram_ptr_flushed_q_slots;  
  uint32_t rd_grd_ptr_autoinc = l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc;
  uint32_t rd_ptr_autoinc = (next_dram_q_in_flight->dram_q_state_flags & DRAM_Q_RAM) != 0 ? l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc : l1_ptrs->dram_to_l1.rd_lrd_ptr_autoinc;
  rdptr_flushed_val = dram_io_incr_ptr(rdptr_flushed_val, rd_ptr_autoinc, dram_buf_size_q_slots);
  next_dram_q_in_flight->dram_ptr_flushed_q_slots = rdptr_flushed_val;  

  uint32_t total_readers;
  bool has_multi_readers = curr_dram_input_stream_state->stream_info->dram_io_info->has_multi_readers && rd_grd_ptr_autoinc;

  if (has_multi_readers) {
    first_dram_q_in_flight = next_dram_q_in_flight;
    while(true) {
      if (!l1_ptrs->l1_to_dram.dram_padding) {
        total_readers = l1_ptrs->local.total_readers;
        has_multi_readers = total_readers > 1 && rd_grd_ptr_autoinc;
        rd_grd_ptr_autoinc = l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc;
        uint32_t reader_index = l1_ptrs->local.reader_index;
        uint32_t rd_stride;
        uint32_t curr_stride_wrap = 0;
        uint32_t next_stride_wrap = 0;

        if (has_multi_readers) {
          volatile uint32_t tt_l1_ptr * l1_ptr_addr = next_dram_q_in_flight->l1_dram_ptrs;
          if (!header_reads_flushed(input_noc, NCRISC_HEADER_RD_TRID, l1_ptr_addr)) {
            if (!curr_dram_input_stream_state->q_ptr_update_pending) {
              push_readdata_to_stream(curr_dram_input_stream_state, stream_id, moves_raw_data, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes);
              curr_dram_input_stream_state->q_ptr_update_pending = true;
            }
            return false;
          }
        }

        if (has_multi_readers && (!stride_iter_matches(l1_ptrs, rd_stride, curr_stride_wrap, next_stride_wrap) || reader_index != rd_stride)) {
          if (!curr_dram_input_stream_state->q_ptr_update_pending) {
            push_readdata_to_stream(curr_dram_input_stream_state, stream_id, moves_raw_data, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes);
            curr_dram_input_stream_state->q_ptr_update_pending = true;
          }
          if (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK)
            dram_ptr_update_cnt = 0;
          return false;
        }
      }

      next_dram_q_in_flight = (dram_q_state_t tt_l1_ptr *)next_dram_q_in_flight->next;
      l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_in_flight->l1_dram_ptrs;
      if (next_dram_q_in_flight == first_dram_q_in_flight) {
        break;
      }
    }
  }

  if (has_multi_readers || rd_grd_ptr_autoinc) {
    first_dram_q_in_flight = next_dram_q_in_flight;
    while(true) {
      if (!l1_ptrs->l1_to_dram.dram_padding) {
        uint32_t rd_grd_ptr_autoinc = l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc;
        uint64_t dram_buf_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
        uint32_t rd_stride;
        uint32_t curr_stride_wrap = 0;
        uint32_t next_stride_wrap = 0;
        uint32_t total_readers_minus_one = l1_ptrs->local.total_readers - 1;
        bool has_multi_readers = total_readers_minus_one > 0 && rd_grd_ptr_autoinc;

        stride_iter_matches(l1_ptrs, rd_stride, curr_stride_wrap, next_stride_wrap);
                          
        if ((next_dram_q_in_flight->dram_q_state_flags & DRAM_Q_RAM) == 0) {
          if (!has_multi_readers || l1_ptrs->local.reader_index == total_readers_minus_one) {
            if (rd_grd_ptr_autoinc) {
              l1_ptrs->l1_to_dram.wr_dram_rdptr = dram_io_incr_ptr(l1_ptrs->l1_to_dram.wr_dram_rdptr, rd_grd_ptr_autoinc, dram_buf_size_q_slots);
              // Reg poll loop, flushed immediately
              while (!ncrisc_noc_fast_write_ok(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
              ncrisc_noc_fast_write(input_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_dram_rdptr)), dram_buf_addr+DRAM_BUF_RDPTR_OFFSET, 2,
                                  DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
              if constexpr (HACK_FW_LOOPING_ENABLED) {
                l1_ptrs->l1_to_dram.wr_dram_local_rdptr = l1_ptrs->l1_to_dram.wr_dram_rdptr;
                // Reg poll loop, flushed immediately
                while (!ncrisc_noc_fast_write_ok(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
                ncrisc_noc_fast_write(input_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_dram_local_rdptr)), dram_buf_addr+DRAM_BUF_LOCAL_RDPTR_OFFSET, 2,
                                      DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
              }
            }
          }
        }

        if (has_multi_readers) {
          uint32_t reader_index = l1_ptrs->local.reader_index;
          if (reader_index == total_readers_minus_one) {
            l1_ptrs->local.stride_wrap = next_stride_wrap;
            l1_ptrs->l1_to_dram.wr_stride = next_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0;
            l1_ptrs->dram_to_l1.rd_stride = next_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0;
          } else {
            l1_ptrs->local.stride_wrap = next_stride_wrap;
            l1_ptrs->l1_to_dram.wr_stride = curr_stride_wrap + reader_index + 1;
            l1_ptrs->dram_to_l1.rd_stride = curr_stride_wrap + reader_index + 1;
          }
          // Reg poll loop, flushed immediately
          while (!ncrisc_noc_fast_write_ok(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
          ncrisc_noc_fast_write(input_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_stride)), dram_buf_addr+DRAM_BUF_STRIDE_OFFSET, 2,
                                DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        }

        next_dram_q_in_flight->dram_ptr_flushed_q_slots = rdptr_flushed_val;  
      }

      next_dram_q_in_flight = (dram_q_state_t tt_l1_ptr *)next_dram_q_in_flight->next;
      l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_in_flight->l1_dram_ptrs;  
      if (next_dram_q_in_flight == first_dram_q_in_flight) {
        break;
      }
    }
  }
 
  flushed_partial_q_slot_tiles = 0;
  next_dram_q_in_flight->flushed_partial_q_slot_tiles = 0;
  epoch_q_slots_remaining--;
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
  risc::record_timestamp(risc::perf_event::EPOCH_Q_SLOT_COMPLETE | stream_id);
  #endif
  return true;
}

void process_dram_streaming_read_l1(void * pFunction, uint32_t& stream_id, uint32_t& phase_active, uint32_t& curr_phase_tiles_remaining_to_issue, dram_input_stream_state_t* curr_dram_input_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_issue,
                                    uint32_t &epoch_q_slots_remaining) {
#ifndef ERISC
  uint32_t in_flight_data_rec_chunks = curr_dram_input_stream_state->in_flight_data_rec_chunks;

  if (phase_active && curr_phase_tiles_remaining_to_issue) {

    RISC_POST_STATUS(0xDD000000 | stream_id);

    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;

    uint32_t wrptr_val_issue = l1_ptrs->dram_to_l1.rd_dram_wrptr;
    uint32_t dram_q_slot_available = !dram_io_empty(next_dram_q_issue->dram_ptr_issued_q_slots, wrptr_val_issue);

    if (dram_q_slot_available) {
      uint32_t q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;
      uint32_t data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
      uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;

      while (dram_q_slot_available) {
        uint32_t dram_num_partial_q_slot_issued_tiles = next_dram_q_issue->dram_num_partial_q_slot_issued_tiles;

        curr_phase_tiles_remaining_to_issue -= data_rec_chunk_size_tiles;
        stream_src_endpoint_set_phase_tiles_count(stream_id, curr_phase_tiles_remaining_to_issue);
        uint32_t fork_index = 0;
        while (true) {
          uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
          if (fork_stream_id == NULL_STREAM) {
            break;
          }
          stream_src_endpoint_set_phase_tiles_count(fork_stream_id, curr_phase_tiles_remaining_to_issue);
          fork_index++;
        }

        push_readdata_to_stream_l1(curr_dram_input_stream_state, stream_id, curr_dram_input_stream_state->moves_raw_data, data_rec_chunk_size_tiles, data_rec_chunk_size_bytes);
    #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
        risc::record_timestamp_l1(risc::perf_event::DRAM_READ_ISSUED | (stream_id << 16) | data_rec_chunk_size_bytes);
    #endif

        curr_dram_input_stream_state->stream_flushed_ptr_byte -= data_rec_chunk_size_bytes;
        in_flight_data_rec_chunks++;

        dram_num_partial_q_slot_issued_tiles += data_rec_chunk_size_tiles;
        bool full_q_slot_recv = dram_num_partial_q_slot_issued_tiles == q_slot_size_tiles;

        if (full_q_slot_recv) {
          uint32_t dram_buf_size_q_slots = next_dram_q_issue->dram_buf_size_q_slots;
          next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = 0;
          next_dram_q_issue->dram_ptr_issued_q_slots = dram_io_incr_ptr_l1(next_dram_q_issue->dram_ptr_issued_q_slots, 1, dram_buf_size_q_slots);
          next_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue->next;
          l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
          data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
          data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
          q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;
        } else {
          next_dram_q_issue->dram_num_partial_q_slot_issued_tiles = dram_num_partial_q_slot_issued_tiles;
        }
                  
        if (!curr_phase_tiles_remaining_to_issue) {
          break;
        }

        wrptr_val_issue = l1_ptrs->dram_to_l1.rd_dram_wrptr;
        dram_q_slot_available = !dram_io_empty(next_dram_q_issue->dram_ptr_issued_q_slots, wrptr_val_issue);
      }
      curr_dram_input_stream_state->in_flight_data_rec_chunks = in_flight_data_rec_chunks;
    }
  }

  if (in_flight_data_rec_chunks > 0) {

    dram_q_state_t tt_l1_ptr * next_dram_q_in_flight = curr_dram_input_stream_state->next_dram_q_in_flight;
    volatile dram_io_state_t tt_l1_ptr* l1_ptrs = (volatile dram_io_state_t tt_l1_ptr*)next_dram_q_in_flight->l1_dram_ptrs;
    uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;

    RISC_POST_STATUS(0xEE000000 | stream_id);

    bool push_flushed = get_stream_push_flushed_l1(curr_dram_input_stream_state, stream_id, data_rec_chunk_size_bytes);

    if (push_flushed) {
            
      RISC_POST_STATUS(0xEE100000);

      uint32_t flushed_partial_q_slot_tiles = next_dram_q_in_flight->flushed_partial_q_slot_tiles;
      uint32_t data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
      uint32_t q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;

      while (push_flushed) {

        flushed_partial_q_slot_tiles += data_rec_chunk_size_tiles;
        bool full_q_slot_flushed = flushed_partial_q_slot_tiles == q_slot_size_tiles;

        if (full_q_slot_flushed) {
          uint32_t dram_buf_size_q_slots = next_dram_q_in_flight->dram_buf_size_q_slots;
          uint32_t rdptr_flushed_val = next_dram_q_in_flight->dram_ptr_flushed_q_slots;  
          rdptr_flushed_val = dram_io_incr_ptr_l1(rdptr_flushed_val, 1, dram_buf_size_q_slots);

          l1_ptrs->dram_to_l1.rd_dram_rdptr = rdptr_flushed_val;

          flushed_partial_q_slot_tiles = 0;
          next_dram_q_in_flight->flushed_partial_q_slot_tiles = flushed_partial_q_slot_tiles;
          next_dram_q_in_flight->dram_ptr_flushed_q_slots = rdptr_flushed_val;     
          next_dram_q_in_flight = next_dram_q_in_flight->next;
          l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_in_flight->l1_dram_ptrs;
          flushed_partial_q_slot_tiles = next_dram_q_in_flight->flushed_partial_q_slot_tiles;
          data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
          data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
          q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;
          epoch_q_slots_remaining--;
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 1
          risc::record_timestamp_l1(risc::perf_event::EPOCH_Q_SLOT_COMPLETE | stream_id);
  #endif
        } else {
          next_dram_q_in_flight->flushed_partial_q_slot_tiles = flushed_partial_q_slot_tiles;
        }

        in_flight_data_rec_chunks--;

        if (!in_flight_data_rec_chunks) {
          break;
        }
        push_flushed = get_stream_push_flushed_l1(curr_dram_input_stream_state, stream_id, data_rec_chunk_size_bytes);
      }
      curr_dram_input_stream_state->next_dram_q_in_flight = next_dram_q_in_flight;
      curr_dram_input_stream_state->epoch_q_slots_remaining = epoch_q_slots_remaining;
      curr_dram_input_stream_state->in_flight_data_rec_chunks = in_flight_data_rec_chunks;
    }
  }
#endif
}

void check_whether_poll_immediately(uint32_t& dram_ptr_update_cnt) {
  // Maybe needed in the future
  //if (RISC_DRAM_POLLING_CTRL_PTR->poll_req) {
  //  RISC_DRAM_POLLING_CTRL_PTR->poll_req_ack = 1;
  //  dram_ptr_update_cnt = 0;
  //}

  uint32_t poll_freq = RISC_EPOCH_RUNTIME_CONFIG_PTR->dram_header_polling_freq;
  if (poll_freq && (dram_ptr_update_cnt & DRAM_PTR_UPDATE_MASK) < poll_freq) {
    dram_ptr_update_cnt = (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK) | (poll_freq & DRAM_PTR_UPDATE_MASK);
  }
}

void set_dont_poll_immediately() {
  // Maybe needed in the future
  //if (RISC_DRAM_POLLING_CTRL_PTR->poll_req_ack) {
  //  RISC_DRAM_POLLING_CTRL_PTR->poll_req_ack = 0;
  //  RISC_DRAM_POLLING_CTRL_PTR->poll_req = 0;
  //}
}

void poll_dram_ptrs(void * pFunction, uint32_t &num_active_dram_queues, dram_q_state_t tt_l1_ptr *dram_q_state, bool& check_ptrs_flushed, bool& all_ptrs_flushed) {
  all_ptrs_flushed = check_ptrs_flushed;

  bool reads_flushed = true;
  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    reads_flushed = reads_flushed && 
  #if defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT)
        ncrisc_noc_reads_flushed_l1(n, NCRISC_HEADER_RD_TRID);
  #else
        ncrisc_noc_reads_flushed(n, NCRISC_HEADER_RD_TRID);
  #endif
  }

  dram_q_state_t tt_l1_ptr * curr_dram_q_state = dram_q_state;
  for (uint32_t i = 0, old_i = 0; i < num_active_dram_queues; i++, curr_dram_q_state = old_i == MAX_L0_DRAM_Q_STATE_T-1 ? (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr : curr_dram_q_state + 1) {
    old_i = i; // Not sure if i++ happens before or after the other incremrent, this statement makes it clear what the order should be
    curr_dram_q_state->dram_q_state_flags &= ~DRAM_Q_PTRS_LOADED_FLAG;
    volatile uint32_t tt_l1_ptr * l1_ptr_addr = curr_dram_q_state->l1_dram_ptrs;
    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)l1_ptr_addr;
    uint32_t ptr_read_pending = curr_dram_q_state->dram_ptr_incoming_q_slots & DRAM_PTR_UPDATE_PENDING_MASK;
    if (ptr_read_pending && (reads_flushed || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST]))))) {
      uint32_t l1_dram_incoming_ptr_index = curr_dram_q_state->l1_dram_incoming_ptr_index;
      uint32_t dram_ptr_incoming_q_slots = l1_dram_incoming_ptr_index ? l1_ptrs->dram_to_l1.rd_dram_wrptr : l1_ptrs->dram_to_l1.rd_dram_rdptr;
      RISC_POST_DEBUG(0xC1000000 | i);
      RISC_POST_DEBUG((uint32_t)l1_ptr_addr);
      RISC_POST_DEBUG(0xC2000000 | l1_dram_incoming_ptr_index);
      RISC_POST_DEBUG(0xC3000000 | dram_ptr_incoming_q_slots);
      bool is_dram_streaming_write = !l1_dram_incoming_ptr_index && (curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0;
      curr_dram_q_state->dram_ptr_incoming_q_slots = dram_ptr_incoming_q_slots;
      if (is_dram_streaming_write) {
        l1_ptrs->dram_to_l1.dram_streaming_tag = 0;
      }
      if (check_ptrs_flushed) {
        bool updater_stride = l1_ptrs->l1_to_dram.wr_stride == 0;
        if ((l1_dram_incoming_ptr_index && updater_stride && l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc && l1_ptrs->l1_to_dram.wr_dram_rdptr != l1_ptrs->dram_to_l1.rd_dram_rdptr) || // read
            (!l1_dram_incoming_ptr_index && updater_stride && l1_ptrs->l1_to_dram.wr_dram_wrptr != l1_ptrs->dram_to_l1.rd_dram_wrptr)) {
          all_ptrs_flushed = false;
        } else {
          curr_dram_q_state->dram_q_state_flags |= DRAM_Q_PTRS_LOADED_FLAG;
        }
      } else {
        curr_dram_q_state->dram_q_state_flags |= DRAM_Q_PTRS_LOADED_FLAG;
      }
    }
  }

  curr_dram_q_state = dram_q_state;
  for (uint32_t i = 0, old_i = 0; i < num_active_dram_queues; i++, curr_dram_q_state = old_i == MAX_L0_DRAM_Q_STATE_T-1 ? (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr : curr_dram_q_state + 1) {
    old_i = i; // Not sure if i++ happens before or after the other incremrent, this statement makes it clear what the order should be
    volatile uint32_t tt_l1_ptr * l1_ptr_addr = curr_dram_q_state->l1_dram_ptrs;
    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)l1_ptr_addr;
    uint32_t rd_grd_ptr_autoinc = l1_ptrs->dram_to_l1.rd_grd_ptr_autoinc;
    uint32_t total_readers = l1_ptrs->local.total_readers;
    bool has_multi_readers = total_readers > 1 && rd_grd_ptr_autoinc;
    bool is_ram_non_multi_read = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_RAM) != 0 && !has_multi_readers;
    bool is_dram_streaming_read = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0 && curr_dram_q_state->l1_dram_incoming_ptr_index;
    bool is_dram_padding = l1_ptrs->l1_to_dram.dram_padding;
    if (!is_dram_streaming_read && !is_ram_non_multi_read && !is_dram_padding) {
      uint32_t ptr_remote = curr_dram_q_state->dram_ptr_incoming_q_slots;
      uint32_t ptr_read_pending = ptr_remote & DRAM_PTR_UPDATE_PENDING_MASK;
      uint32_t input_noc = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_INPUT_NOC_FLAG) >> 1;
      RISC_POST_DEBUG(0xC4000000 | i);
      RISC_POST_DEBUG(0xC5000000 | ptr_remote);
      if (!ptr_read_pending && (curr_dram_q_state->dram_q_state_flags & DRAM_Q_PTRS_LOADED_FLAG) == 0 && (reads_flushed || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST]))))) {
        uint64_t dram_ptr_addr;
        if ((curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0) {
          dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
        } else {
          dram_ptr_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
        }
        if (check_ptrs_flushed && !addr_is_pcie(dram_ptr_addr))
          continue;
        curr_dram_q_state->dram_ptr_incoming_q_slots = ptr_remote | DRAM_PTR_UPDATE_PENDING_MASK;
        set_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST])));
        RISC_POST_DEBUG(0xC7000000);
        RISC_POST_DEBUG((uint32_t)l1_ptr_addr);
        RISC_POST_DEBUG(dram_ptr_addr>>32);
        RISC_POST_DEBUG(dram_ptr_addr & 0xFFFFFFFF);
        RISC_POST_DEBUG(0xC8000000);          
        // Reg poll loop, flushed immediately
  #if defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT)
        while (!ncrisc_noc_fast_read_ok_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_read_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_ptr_addr, (uint32_t)l1_ptr_addr, DRAM_HEADER_SIZE, NCRISC_HEADER_RD_TRID);
  #else
        while (!ncrisc_noc_fast_read_ok(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_read(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_ptr_addr, (uint32_t)l1_ptr_addr, DRAM_HEADER_SIZE, NCRISC_HEADER_RD_TRID);
  #endif
      }
    }
  }
}

#ifdef DRAM_DECOUPLE
void risc_decoupled_stream_buffer_init_l1(uint32_t stream_id)
{
  uint32_t stream_buf_addr_byte = stream_get_data_buf_addr(stream_id) * MEM_WORD_WIDTH;
  uint32_t stream_buf_size_bytes = stream_get_data_buf_size(stream_id) * MEM_WORD_WIDTH;
  uint32_t *stream_header_buf_addr = (uint32_t *) (stream_get_msg_info_rd_ptr(stream_id) * MEM_WORD_WIDTH);
  uint32_t msg_size = *stream_header_buf_addr;
  uint32_t msg_size_bytes = msg_size * MEM_WORD_WIDTH;

  for (uint32_t i = 0; i < stream_buf_size_bytes; i = i + msg_size_bytes) {
    uint32_t *ptr = (uint32_t *)stream_buf_addr_byte;
    *ptr = msg_size;
    stream_buf_addr_byte += msg_size_bytes;
  }
}

void risc_decoupled_dram_buffer_init_l1(volatile dram_io_state_t tt_l1_ptr * dram_io_state, uint32_t noc, uint32_t msg_info_buf_start)
{
  uint64_t dram_buf_addr = tt_l1_load(&dram_io_state->dram_buf_addr) + DRAM_BUF_DATA_OFFSET;
  uint32_t dram_buf_size_q_slots = dram_io_state->dram_buf_size_q_slots;
  uint32_t dram_q_slot_size_tiles = dram_io_state->dram_q_slot_size_tiles;


  uint32_t stream_msg_info_buf_ptr = msg_info_buf_start * MEM_WORD_WIDTH;
  uint32_t tile_size_words = *((int *)(stream_msg_info_buf_ptr));
  uint32_t tile_size_bytes = tile_size_words * MEM_WORD_WIDTH;

  noc_read_scratch_buf[0] = tile_size_words;
  noc_read_scratch_buf[1] = 0;
  noc_read_scratch_buf[2] = 0;
  noc_read_scratch_buf[3] = 0;
  noc_read_scratch_buf[6] = 0xdeadbead;
  noc_read_scratch_buf[7] = 0xdeadbead;

  for (uint32_t i = 0; i < dram_buf_size_q_slots; i++) {
    for (volatile uint32_t j = 0; j < dram_q_slot_size_tiles; j++) { // We need this volatile as otherwise it produces a software divide
      //write tile header to dram buffer.
      noc_read_scratch_buf[4] = (dram_buf_size_q_slots << 16) | i;
      noc_read_scratch_buf[5] = (dram_q_slot_size_tiles << 16) | j;
      while (!ncrisc_noc_fast_write_ok_l1(noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write_l1(noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(noc_read_scratch_buf[0])), dram_buf_addr, 32,
                                                     DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);

      dram_buf_addr += tile_size_bytes;
    }
  }
}
#endif

// initilizing stream related structures
void risc_dram_stream_handler_init_l1(
  void * pFunction,
#ifdef RISC_GSYNC_ENABLED
  volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
#ifdef ERISC
  volatile uint32_t &epoch_id_to_send, volatile uint32_t &other_chip_epoch_id,
#endif
  uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
  dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t tt_l1_ptr *active_stream_info,
  epoch_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info
) {

  RISC_POST_STATUS((uint32_t)pFunction);
#if defined(PERF_DUMP)
  risc::record_timestamp_at_offset_l1(risc::perf_event::STREAM_HANDLER_INIT, risc::PROLOGUE_START_OFFSET);
#endif
#ifndef PERF_DUMP
  RISC_POST_STATUS(0xA0000000);
#endif
  // Initialized header buffers
  tile_header_buffer_init();
  
#ifndef PERF_DUMP
  RISC_POST_STATUS(0xB0000000);
#endif
  uint32_t num_kernel_inputs = RISC_EPOCH_INFO_PTR->num_inputs;
  uint32_t num_kernel_outputs =RISC_EPOCH_INFO_PTR->num_outputs;
  
  num_dram_input_streams = 0;
  num_dram_output_streams = 0;
  num_active_dram_queues = 0;


#ifdef RISC_GSYNC_ENABLED
#ifndef PERF_DUMP
  RISC_POST_STATUS(0xB0010000);
#endif
  global_sync(gsync_epoch, epochs_in_progress);
#endif

#ifdef ERISC
  uint32_t epoch_id = RISC_EPOCH_INFO_PTR->epoch_id;
  bool has_eth_stream_trans = RISC_EPOCH_INFO_PTR->has_eth_stream_trans;
  eth_epoch_sync(epoch_id, has_eth_stream_trans, epoch_id_to_send, other_chip_epoch_id, risc_context_switch);
#endif

  active_stream_info_t tt_l1_ptr * curr_active_stream_info = active_stream_info;
  
  //initialize streams to write to dram on WHB0.
  //does nothing on GS, WHA0.
  risc_dram_write_init(DRAM_STREAM_1);
#ifndef ERISC
  risc_dram_write_init(DRAM_STREAM_2);
#endif

  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {

    volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
    uint32_t stream_id = l1_stream_info->stream_id;
    uint32_t blob_start_addr = l1_stream_info->blob_start_addr;
    uint32_t start_phase_num_cfg_regs = l1_stream_info->start_phase_num_cfg_regs;
    uint32_t flags = l1_stream_info->flags;

    bool is_dram_prefetch_streaming = ((flags & STREAM_DRAM_INPUT) != 0) && ((flags & STREAM_DRAM_IO) == 0) && ((flags & STREAM_DRAM_STREAMING) == 0) &&
                                      ((flags & STREAM_INTERMEDIATE) == 0) && ((flags & STREAM_MOVES_RAW_DATA) == 0);

    if ((flags & STREAM_BRISC_PACK) == 0 && !is_dram_prefetch_streaming) {
#ifndef PERF_DUMP
      RISC_POST_STATUS(0xA1000000 | stream_id);
#endif
      stream_phase_blob_run(stream_id, blob_start_addr, start_phase_num_cfg_regs);
    } else {
      stream_set_auto_cfg_header(stream_id, 0); // Reset this back to zero since thats default value for successfully finished streams
    }

    uint32_t operand = stream_id_to_operand(stream_id);
    if ((flags & STREAM_MOVES_RAW_DATA) != 0 || ((flags & STREAM_DRAM_NO_PUSH) != 0)) {
      volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(operand);
      volatile uint32_t tt_reg_ptr * tiles_acked_ptr = get_operand_tiles_acked_ptr(operand);
      tiles_received_ptr[0] = 0;
      tiles_acked_ptr[0] = 0;
    }
  }

  // We cannot easily do this in brisc because we have a race condition between the epoch being loaded and usage of these variables in risc
  // If we move to brisc wed need syncs between risc/brisc, and moving this doesnt have much space (about 256 bytes)
  risc_initialize_tile_counters(num_kernel_inputs, num_kernel_outputs);

  dram_input_stream_state_t* curr_dram_input_stream_state_init = dram_input_stream_state;
  dram_output_stream_state_t* curr_dram_output_stream_state_init = dram_output_stream_state;
  dram_q_state_t tt_l1_ptr * curr_dram_q_state = dram_q_state;

  // Other dram stream setup
  curr_active_stream_info = active_stream_info;
  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {


    uint32_t stream_id = curr_active_stream_info->stream_id;
    uint32_t flags = curr_active_stream_info->flags;

    RISC_POST_STATUS(0xA3000000 | stream_id);
    RISC_POST_DEBUG(0xA3100000 | flags);

    // Load epoch data into local memory
    if ((flags & STREAM_DRAM_INPUT) != 0) {
      
      if ((flags & STREAM_DRAM_IO) != 0 || (flags & STREAM_DRAM_STREAMING) != 0) {

        RISC_POST_STATUS(0xA4000000 | stream_id);

        // Reg poll, flushed immediately
        while (!stream_phase_is_active(stream_id)); 

        volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
        uint8_t* fork_stream_ids_ptr = curr_dram_input_stream_state_init->fork_stream_ids;
        uint32_t num_fork_streams = l1_stream_info->num_fork_streams;
        for (uint32_t k = 0; k < EPOCH_MAX_OUTPUT_FORKS; k++) {
          fork_stream_ids_ptr[k] = RISC_EPOCH_INFO_PTR->active_streams[l1_stream_info->fork_idxs[k]]->stream_id;
          if (k == num_fork_streams) {
            fork_stream_ids_ptr[k] = NULL_STREAM;
            break;
          }
        }
          
        epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
        volatile dram_io_state_t tt_l1_ptr * l1_dram_io_state = l1_dram_io_info->dram_io_state;
        uint32_t num_dram_io_qs = l1_stream_info->num_dram_io_bufs;
        uint32_t c_dim_size = l1_stream_info->c_dim_size;
        uint32_t q_slot_size_bytes = l1_dram_io_info->dram_q_slot_size_bytes;
        uint32_t epoch_q_slots_remaining = l1_dram_io_info->epoch_q_slots_remaining;
        uint32_t stream_buf_addr_byte = stream_get_data_buf_addr(stream_id) * MEM_WORD_WIDTH;
        uint32_t stream_buf_size_bytes = stream_get_data_buf_size(stream_id) * MEM_WORD_WIDTH;
        uint32_t input_noc = stream_get_input_noc(stream_id);

        uint32_t transaction_id = NCRISC_RD_START_TRID + num_dram_input_streams;
        while (transaction_id > NCRISC_RD_END_TRID)
          transaction_id -= NCRISC_RD_END_TRID-NCRISC_RD_START_TRID+1;

        curr_dram_input_stream_state_init->moves_raw_data = (flags & STREAM_MOVES_RAW_DATA) != 0 || (flags & STREAM_DRAM_NO_PUSH) != 0;
        curr_dram_input_stream_state_init->stream_info = l1_stream_info;
        
        curr_dram_input_stream_state_init->in_flight_data_rec_chunks = 0;
        curr_dram_input_stream_state_init->min_dram_io_entries = 0;
        if ((flags & STREAM_DRAM_STREAMING) != 0)
          curr_dram_input_stream_state_init->stream_flushed_ptr_byte = stream_buf_size_bytes;
        else
          curr_dram_input_stream_state_init->stream_flushed_ptr_byte = 0;
        curr_dram_input_stream_state_init->in_flight_bytes = 0;
        curr_dram_input_stream_state_init->scatter_loop_index = 0;
        curr_dram_input_stream_state_init->scatter_loop_inc = 0;
        curr_dram_input_stream_state_init->active_stream_id = i;
        curr_dram_input_stream_state_init->io_q_empty = 1;
        curr_dram_input_stream_state_init->stream_buf_available = 0;
        curr_dram_input_stream_state_init->q_ptr_update_pending = false;
        curr_dram_input_stream_state_init->dram_read_opt_enabled = false;
        curr_dram_input_stream_state_init->scatter_list_input_index = 0;

        curr_dram_input_stream_state_init->transaction_id_issued = transaction_id;
        curr_dram_input_stream_state_init->transaction_id_flushed = transaction_id;

        curr_dram_input_stream_state_init->c_dim_count = 0;
        curr_dram_input_stream_state_init->c_dim_size = c_dim_size;
        curr_dram_input_stream_state_init->col_offset_bytes = 0;

        curr_dram_input_stream_state_init->epoch_q_slots_remaining = epoch_q_slots_remaining;

        curr_dram_input_stream_state_init->stream_id = stream_id;
        curr_dram_input_stream_state_init->input_noc = input_noc;
        curr_dram_input_stream_state_init->stream_buf_addr_byte = stream_buf_addr_byte;
        curr_dram_input_stream_state_init->prev_rd_data_word_recv = 0;

        curr_dram_input_stream_state_init->next_dram_q_issue = curr_dram_q_state;
        curr_dram_input_stream_state_init->next_dram_q_in_flight = curr_dram_q_state;

        dram_q_state_t tt_l1_ptr * start_dram_q_state = curr_dram_q_state;
        dram_q_state_t tt_l1_ptr * prev_dram_q_state = curr_dram_q_state;
        for (uint32_t q = 0;  q < num_dram_io_qs; q++) {
          uint32_t dram_buf_size_q_slots = l1_dram_io_state->local.dram_buf_size_q_slots;

          dram_q_state_t tt_l1_ptr * next_dram_q_state;
          if (num_active_dram_queues == MAX_L0_DRAM_Q_STATE_T-1) {
            next_dram_q_state = (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr;
          } else {
            next_dram_q_state = curr_dram_q_state + 1;
          }

          uint16_t reader_index = l1_dram_io_state->local.reader_index;
          curr_dram_q_state->temp0 = reader_index;
          curr_dram_q_state->dram_q_stream_id = stream_id;
          curr_dram_q_state->dram_ptr_issued_q_slots = 0; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_flushed_q_slots = 0; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_incoming_q_slots = 0; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_issued_byte = q_slot_size_bytes; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_num_partial_q_slot_issued_tiles = 0; // temp, changed when loading dram pointers
          curr_dram_q_state->flushed_partial_q_slot_tiles = 0;
          curr_dram_q_state->scatter_offsets_index = 0;
          curr_dram_q_state->dram_q_state_flags = ((flags & STREAM_DRAM_RAM) != 0 ? 1 : 0) | (input_noc << 1) | ((flags & STREAM_DRAM_STREAMING) != 0 ? 1 << 3 : 0);
          curr_dram_q_state->l1_dram_incoming_ptr_index = 1;
          curr_dram_q_state->dram_buf_size_q_slots = dram_buf_size_q_slots;
          curr_dram_q_state->next = next_dram_q_state;
          curr_dram_q_state->l1_dram_ptrs = (volatile uint32_t tt_l1_ptr *)l1_dram_io_state;
#ifdef DRAM_DECOUPLE
          l1_dram_io_state->dram_decoupled = flags & (STREAM_DRAM_INPUT | STREAM_DRAM_IO);
#endif
          prev_dram_q_state = curr_dram_q_state;
          curr_dram_q_state = next_dram_q_state;
          l1_dram_io_state = l1_dram_io_state->local.next;
          num_active_dram_queues++;
        }
        prev_dram_q_state->next = start_dram_q_state;
        num_dram_input_streams++;
        curr_dram_input_stream_state_init++;
      }
    }
    else if ((flags & STREAM_DRAM_OUTPUT) != 0) {

      if ((flags & STREAM_DRAM_IO) != 0 || (flags & STREAM_DRAM_STREAMING) != 0) {

        // We dont do this for writes as the stream may not be started by ncrisc but packer instead
        //while (!stream_phase_is_active(stream_id));

        RISC_POST_STATUS(0xA6000000 | stream_id);

        volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
        volatile epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
        volatile dram_io_state_t tt_l1_ptr* l1_dram_io_state = l1_dram_io_info->dram_io_state;
        uint32_t q_slot_size_tiles = l1_dram_io_state->local.dram_q_slot_size_tiles;
        uint32_t q_slot_size_bytes = l1_dram_io_info->dram_q_slot_size_bytes;
        uint32_t epoch_q_slots_remaining = l1_dram_io_info->epoch_q_slots_remaining;
        uint32_t num_dram_io_qs = l1_stream_info->num_dram_io_bufs;
        uint32_t epoch_id = RISC_EPOCH_INFO_PTR->epoch_id;
        uint16_t write_stride = l1_stream_info->stride;
        uint16_t total_write_strides = l1_stream_info->total_strides;
        uint32_t stride_offset_size_bytes = l1_stream_info->stride_offset_size_bytes;
        uint32_t skip_col_bytes = l1_stream_info->skip_col_bytes;
        uint32_t skip_col_tile_row_bytes = l1_stream_info->skip_col_tile_row_bytes;
        uint32_t skip_col_row_bytes = l1_stream_info->skip_col_row_bytes;
        uint32_t skip_zcol_bytes = l1_stream_info->skip_zcol_bytes;
        uint32_t skip_col_zrow_bytes = l1_stream_info->skip_col_zrow_bytes;
        uint32_t c_dim_size = l1_stream_info->c_dim_size;
        uint32_t r_dim_size = l1_stream_info->r_dim_size;
        uint32_t zc_dim_size = l1_stream_info->zc_dim_size;
        uint32_t zr_dim_size = l1_stream_info->zr_dim_size;
        uint32_t stream_buf_addr_byte = l1_stream_info->buf_base_addr;
        uint32_t stream_buf_size_bytes = l1_stream_info->buf_full_size_bytes;
        uint32_t output_noc = l1_dram_io_info->output_noc;
        
        curr_dram_output_stream_state_init->stream_info = l1_stream_info;

        curr_dram_output_stream_state_init->stream_buf_addr_byte = stream_buf_addr_byte;
        curr_dram_output_stream_state_init->stream_buf_size_bytes = stream_buf_size_bytes;
        curr_dram_output_stream_state_init->moves_raw_data = (flags & STREAM_MOVES_RAW_DATA) != 0;
        curr_dram_output_stream_state_init->epoch_id = epoch_id & 0x7FFF;
        curr_dram_output_stream_state_init->write_stride = write_stride;
        curr_dram_output_stream_state_init->total_write_strides = total_write_strides;

        curr_dram_output_stream_state_init->skip_col_bytes = skip_col_bytes;
        curr_dram_output_stream_state_init->skip_col_tile_row_bytes = skip_col_tile_row_bytes;
        curr_dram_output_stream_state_init->skip_col_row_bytes = skip_col_row_bytes;
        curr_dram_output_stream_state_init->skip_zcol_bytes = skip_zcol_bytes;
        curr_dram_output_stream_state_init->skip_col_zrow_bytes = skip_col_zrow_bytes;
        curr_dram_output_stream_state_init->c_dim_size = c_dim_size;
        curr_dram_output_stream_state_init->r_dim_size = r_dim_size;
        curr_dram_output_stream_state_init->zc_dim_size = zc_dim_size;
        curr_dram_output_stream_state_init->zr_dim_size = zr_dim_size;

        curr_dram_output_stream_state_init->c_dim_count = 0;
        curr_dram_output_stream_state_init->r_dim_count = 0;
        curr_dram_output_stream_state_init->zc_dim_count = 0;
        curr_dram_output_stream_state_init->zr_dim_count = 0;

        curr_dram_output_stream_state_init->in_flight_q_slots = 0;
        curr_dram_output_stream_state_init->tiles_to_clear = 0;
        curr_dram_output_stream_state_init->in_flight_tiles = 0;
        curr_dram_output_stream_state_init->stream_rd_ptr_byte = 0;
        curr_dram_output_stream_state_init->curr_phase_tiles_sent = 0;

        curr_dram_output_stream_state_init->q_slot_size_tiles = q_slot_size_tiles;
        curr_dram_output_stream_state_init->epoch_q_slots_remaining = epoch_q_slots_remaining;

        curr_dram_output_stream_state_init->stream_id = stream_id;
        curr_dram_output_stream_state_init->dram_q_available = 0;

        curr_dram_output_stream_state_init->next_dram_q_issue = curr_dram_q_state;
          
        dram_q_state_t tt_l1_ptr * start_dram_q_state = curr_dram_q_state;
        dram_q_state_t tt_l1_ptr * prev_dram_q_state = curr_dram_q_state;
        for (uint32_t q = 0;  q < num_dram_io_qs; q++) {
        
          uint32_t dram_buf_size_q_slots = l1_dram_io_state->local.dram_buf_size_q_slots;
        
          dram_q_state_t tt_l1_ptr * next_dram_q_state;
          if (num_active_dram_queues == MAX_L0_DRAM_Q_STATE_T-1) {
            next_dram_q_state = (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr;
          } else {
            next_dram_q_state = curr_dram_q_state + 1;
          }

          curr_dram_q_state->temp0 = write_stride;
          curr_dram_q_state->dram_q_stream_id = stream_id;
          curr_dram_q_state->dram_ptr_issued_q_slots = q_slot_size_bytes & 0xffff; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_flushed_q_slots = total_write_strides; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_incoming_q_slots = (q_slot_size_bytes >> 16) & 0xffff; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_ptr_issued_byte = total_write_strides > 1 ? stride_offset_size_bytes : q_slot_size_bytes; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_num_partial_q_slot_issued_tiles = epoch_id; // temp, changed when loading dram pointers
          curr_dram_q_state->dram_q_state_flags = ((flags & STREAM_DRAM_RAM) != 0 ? 1 : 0) | (output_noc << 1) | ((flags & STREAM_DRAM_STREAMING) != 0 ? 1 << 3 : 0);
          curr_dram_q_state->l1_dram_incoming_ptr_index = 0;
          curr_dram_q_state->dram_buf_size_q_slots = dram_buf_size_q_slots;
          curr_dram_q_state->next = next_dram_q_state;
          curr_dram_q_state->l1_dram_ptrs = (volatile uint32_t tt_l1_ptr *)l1_dram_io_state;
#ifdef DRAM_DECOUPLE
          l1_dram_io_state->dram_decoupled = flags & (STREAM_DRAM_OUTPUT | STREAM_DRAM_IO);
#endif
          prev_dram_q_state = curr_dram_q_state;
          curr_dram_q_state = next_dram_q_state;
          l1_dram_io_state = l1_dram_io_state->local.next;
          num_active_dram_queues++;
        }
        prev_dram_q_state->next = start_dram_q_state;        
        num_dram_output_streams++;
        curr_dram_output_stream_state_init++;
        
      }
    }
    else if ((flags & STREAM_INPUT_PARK) != 0) {
      // DV-only
      // Reg poll, flushed immediately
      while (!stream_phase_is_active(stream_id));
#ifndef PERF_DUMP
      RISC_POST_STATUS(0xA7000000 | stream_id);
#endif
      uint32_t stream_buf_size_words = stream_get_data_buf_size(stream_id);
      uint32_t stream_buf_size_tiles = stream_get_curr_phase_num_msgs(stream_id);
      if ((flags & STREAM_INTERMEDIATE) != 0 || (flags & STREAM_MOVES_RAW_DATA) != 0) {
        volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
        tiles_received_ptr[0] += stream_buf_size_tiles; // ok to be like this because its a park buffer and wont wrap
      } else {
        stream_push_tiles(stream_id, stream_buf_size_tiles, stream_buf_size_words);
      }
    }
  }

  // Dram prefetch - init
  // Checks prolog and dram io buffers, ensure they are valid to use
  num_dram_prefetch_streams = 0;
  curr_active_stream_info = active_stream_info;
  { // Control stack allocation
    bool dram_prefetch_has_data[MAX_PREFETCH_STREAMS];
    for (uint32_t i = 0;
        i < num_active_streams;
        i++, curr_active_stream_info++) {
      uint32_t flags = curr_active_stream_info->flags;
      if ((((flags & STREAM_DRAM_INPUT) != 0) && ((flags & STREAM_DRAM_IO) == 0) && ((flags & STREAM_DRAM_STREAMING) == 0) && ((flags & STREAM_IS_FORK) == 0)) ||
          (((flags & STREAM_DRAM_OUTPUT) != 0) && ((flags & STREAM_DRAM_IO) == 0) && ((flags & STREAM_DRAM_STREAMING) == 0))) {
        dram_prefetch_has_data[num_dram_prefetch_streams] = false;
        dram_prefetch_epoch_stream_info[num_dram_prefetch_streams] = RISC_EPOCH_INFO_PTR->active_streams[i];
        dram_prefetch_active_stream_info[num_dram_prefetch_streams] = curr_active_stream_info;
        num_dram_prefetch_streams++;
      }
    }

    // Dram load pointers
    bool all_dram_io_ptrs_loaded = false;
    bool all_dram_prefetch_have_data = false;
    bool reads_flushed = true;
    while (!all_dram_prefetch_have_data || !all_dram_io_ptrs_loaded) {

      dram_q_state_t tt_l1_ptr * curr_dram_q_state = dram_q_state;
      for (uint32_t i = 0, old_i = 0; i < num_active_dram_queues; i++, curr_dram_q_state = old_i == MAX_L0_DRAM_Q_STATE_T-1 ? (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr : curr_dram_q_state + 1) {
        old_i = i; // Not sure if i++ happens before or after the other incremrent, this statement makes it clear what the order should be
        if ((curr_dram_q_state->dram_q_state_flags & DRAM_Q_PTRS_LOADED_FLAG) == 0) {
  #ifndef PERF_DUMP
          RISC_POST_STATUS(0xA8100000 | i);
  #endif
          volatile uint32_t tt_l1_ptr * l1_ptr_addr = curr_dram_q_state->l1_dram_ptrs;
          volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)l1_ptr_addr;
          RISC_POST_DEBUG((uint32_t)l1_ptr_addr);
          bool is_dram_streaming_read = curr_dram_q_state->l1_dram_incoming_ptr_index && (curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0;
          bool is_dram_padding = l1_ptrs->l1_to_dram.dram_padding;
          if (is_dram_streaming_read || is_dram_padding) {
            uint32_t dram_ptr_issued_q_slots = l1_ptrs->dram_to_l1.rd_dram_rdptr;
            uint32_t dram_ptr_incoming_q_slots = l1_ptrs->dram_to_l1.rd_dram_wrptr;
            curr_dram_q_state->dram_ptr_issued_q_slots = dram_ptr_issued_q_slots;
            curr_dram_q_state->dram_ptr_incoming_q_slots = dram_ptr_incoming_q_slots;
            curr_dram_q_state->dram_ptr_flushed_q_slots = dram_ptr_issued_q_slots;
            l1_ptrs->l1_to_dram.wr_dram_rdptr = l1_ptrs->dram_to_l1.rd_dram_rdptr;
            if (is_dram_streaming_read)
              l1_ptrs->dram_to_l1.dram_streaming_tag = DRAM_STREAMING_SYNC_NUMBER;
            curr_dram_q_state->dram_q_state_flags |= DRAM_Q_PTRS_LOADED_FLAG;
          } else if (reads_flushed) {
            uint32_t input_noc = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_INPUT_NOC_FLAG) >> 1;
            uint64_t dram_ptr_addr;
            if ((curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0) {
              dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
            } else {
              dram_ptr_addr = tt_l1_load(&l1_ptrs->local.dram_buf_addr);
            }
            RISC_POST_DEBUG(dram_ptr_addr>>32);
            RISC_POST_DEBUG(dram_ptr_addr & 0xFFFFFFFF); 
            set_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST])));
            // Reg poll loop, flushed immediately
            while (!ncrisc_noc_fast_read_ok_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
            ncrisc_noc_fast_read_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_ptr_addr, (uint32_t)l1_ptr_addr, DRAM_HEADER_SIZE, NCRISC_HEADER_RD_TRID);
          }
        }
      }

      for (uint32_t i = 0; i < num_dram_prefetch_streams; i++) {
        curr_active_stream_info = dram_prefetch_active_stream_info[i];
        uint32_t stream_id = curr_active_stream_info->stream_id;
        if (!dram_prefetch_has_data[i]) {
  #ifndef PERF_DUMP
          RISC_POST_STATUS(0xA8200000 | stream_id);
  #endif

          if (reads_flushed) {
            epoch_stream_info_t tt_l1_ptr * l1_stream_info = dram_prefetch_epoch_stream_info[i];
            epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
            uint32_t num_dram_io_qs = l1_stream_info->num_dram_io_bufs;
            volatile dram_io_state_t tt_l1_ptr * l1_dram_io_state = l1_dram_io_info->dram_io_state;
            uint32_t input_noc = l1_dram_io_info->input_noc;
            for (uint32_t q = 0;  q < num_dram_io_qs; q++) {
              if (!l1_dram_io_state->l1_to_dram.dram_padding) {
                uint32_t tt_l1_ptr* l1_ptr_addr = (uint32_t tt_l1_ptr *)(l1_dram_io_state);
                uint64_t dram_src_addr = tt_l1_load(&l1_dram_io_state->local.dram_buf_addr);
                set_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST])));
                while (!ncrisc_noc_fast_read_ok_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
                ncrisc_noc_fast_read_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_src_addr, (uint32_t)l1_ptr_addr, DRAM_HEADER_SIZE, NCRISC_HEADER_RD_TRID);
              }
              l1_dram_io_state = l1_dram_io_state->local.next;
            }
          }
        }
      }

      reads_flushed = true;
      for (uint32_t n = 0; n < NUM_NOCS; n++) {
        reads_flushed = reads_flushed && ncrisc_noc_reads_flushed_l1(n, NCRISC_HEADER_RD_TRID);
      }

      all_dram_prefetch_have_data = true;

      for (uint32_t i = 0; i < num_dram_prefetch_streams; i++) {
        curr_active_stream_info = dram_prefetch_active_stream_info[i];
        uint32_t stream_id = curr_active_stream_info->stream_id;
        uint32_t flags = curr_active_stream_info->flags;
        if ((flags & STREAM_DRAM_INPUT) != 0 || (flags & STREAM_DRAM_OUTPUT) != 0) {
          if (!dram_prefetch_has_data[i]) {
  #ifndef PERF_DUMP
            RISC_POST_STATUS(0xA8300000 | stream_id);
  #endif

            epoch_stream_info_t tt_l1_ptr * l1_stream_info = dram_prefetch_epoch_stream_info[i];
            epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
            uint32_t num_dram_io_qs = l1_stream_info->num_dram_io_bufs;
            volatile dram_io_state_t tt_l1_ptr * l1_dram_io_state = l1_dram_io_info->dram_io_state;
            for (uint32_t q = 0;  q < num_dram_io_qs; q++) {
              uint32_t tt_l1_ptr * l1_ptr_addr = (uint32_t tt_l1_ptr *)(l1_dram_io_state);
              uint16_t rd_epoch_id_tag = l1_dram_io_state->dram_to_l1.rd_epoch_id_tag;
              bool is_dram_padding = l1_dram_io_state->l1_to_dram.dram_padding;
              if ((flags & STREAM_DRAM_INPUT) != 0 &&
                  (reads_flushed || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST])))) &&
                  (is_dram_padding ||
                   ((flags & STREAM_DRAM_RAM) != 0) || 
                   (l1_dram_io_state->dram_to_l1.rd_flags & DRAM_Q_HEADER_ZERO_FLAG) != 0 ||
                   !dram_io_local_empty(l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr, l1_dram_io_state->dram_to_l1.rd_dram_rdptr, l1_dram_io_state->dram_to_l1.rd_dram_wrptr)) &&
                  (is_dram_padding || rd_epoch_id_tag == DRAM_QUEUE_NO_EPOCH_CHECK || rd_epoch_id_tag == l1_stream_info->producer_epoch_id)) {
                if (q == num_dram_io_qs-1)
                  dram_prefetch_has_data[i] = true;
              } else if ((flags & STREAM_DRAM_OUTPUT) != 0 &&
                        (reads_flushed || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST]))))) {
                if (q == num_dram_io_qs-1)
                  dram_prefetch_has_data[i] = true;
              } else {
                all_dram_prefetch_have_data = false;
                break;
              }
              l1_dram_io_state = l1_dram_io_state->local.next;
            }
          }
        }
      }

      all_dram_io_ptrs_loaded = true;

      curr_dram_q_state = dram_q_state;
      for (uint32_t i = 0, old_i = 0; i < num_active_dram_queues; i++, curr_dram_q_state = old_i == MAX_L0_DRAM_Q_STATE_T-1 ? (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr : curr_dram_q_state + 1) {
        old_i = i; // Not sure if i++ happens before or after the other incremrent, this statement makes it clear what the order should be
        if ((curr_dram_q_state->dram_q_state_flags & DRAM_Q_PTRS_LOADED_FLAG) == 0) {
  #ifndef PERF_DUMP
          RISC_POST_STATUS(0xA8400000 | i);
  #endif
          volatile uint32_t tt_l1_ptr * l1_ptr_addr = curr_dram_q_state->l1_dram_ptrs;
          volatile dram_io_state_t tt_l1_ptr* l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)l1_ptr_addr;

          if (!(reads_flushed || check_packet_end_marker((uint32_t)(&(l1_ptr_addr[DRAM_HEADER_LAST]))))) {
            all_dram_io_ptrs_loaded = false;
            continue;
          }

          bool dram_streaming_not_ready = false;
          bool is_dram_streaming_write = !curr_dram_q_state->l1_dram_incoming_ptr_index && (curr_dram_q_state->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0;
          if (is_dram_streaming_write) {
            uint32_t dest_epoch_id = l1_ptrs->dram_to_l1.dram_streaming_epoch_id;
            bool tag_match = l1_ptrs->dram_to_l1.dram_streaming_tag == DRAM_STREAMING_SYNC_NUMBER;
            uint32_t current_epoch_id = curr_dram_q_state->dram_num_partial_q_slot_issued_tiles;
            dram_streaming_not_ready = !tag_match || current_epoch_id != dest_epoch_id;
          }

          if (dram_streaming_not_ready || l1_ptrs->dram_to_l1.rd_stride > curr_dram_q_state->temp0) {
            all_dram_io_ptrs_loaded = false;
            continue;
          }

          uint32_t l1_dram_incoming_ptr_index = curr_dram_q_state->l1_dram_incoming_ptr_index;

          uint32_t dram_ptr_issued_q_slots;
          uint32_t dram_ptr_incoming_q_slots;
          if (l1_dram_incoming_ptr_index) { // Read
            if ((curr_dram_q_state->dram_q_state_flags & DRAM_Q_RAM) != 0) {
              dram_ptr_issued_q_slots = l1_ptrs->dram_to_l1.rd_dram_rdptr;
            } else {
              dram_ptr_issued_q_slots = l1_ptrs->dram_to_l1.rd_dram_local_rdptr;
            }
            dram_ptr_incoming_q_slots = l1_ptrs->dram_to_l1.rd_dram_wrptr;
          } else { // write
            dram_ptr_issued_q_slots = l1_ptrs->dram_to_l1.rd_dram_wrptr;
            dram_ptr_incoming_q_slots = l1_ptrs->dram_to_l1.rd_dram_rdptr;
          }

          uint32_t total_strides = curr_dram_q_state->dram_ptr_flushed_q_slots;
          bool is_strided = total_strides > 1;
          uint32_t start_bytes;
          uint32_t inc_size_bytes;
          if (is_strided) {
            start_bytes = curr_dram_q_state->dram_ptr_issued_byte;
            inc_size_bytes = ((uint32_t)curr_dram_q_state->dram_ptr_incoming_q_slots << 16) | curr_dram_q_state->dram_ptr_issued_q_slots;
          } else {
            start_bytes = 0;
            inc_size_bytes = curr_dram_q_state->dram_ptr_issued_byte;
          }

          // we dont need dram_ptr_issued_q_slots % curr_dram_q_state->dram_buf_size_q_slots because buf_ptr_inc_wrap ensures start_bytes is within dram_data_buf_size_bytes
          uint32_t dram_data_buf_size_bytes = l1_ptrs->local.dram_buf_size_bytes;
          start_bytes = buf_ptr_inc_wrap(start_bytes, mulsi3(dram_ptr_issued_q_slots,inc_size_bytes), dram_data_buf_size_bytes);

          curr_dram_q_state->dram_ptr_issued_q_slots = dram_ptr_issued_q_slots;
          curr_dram_q_state->dram_ptr_incoming_q_slots = dram_ptr_incoming_q_slots;
          curr_dram_q_state->dram_ptr_flushed_q_slots = dram_ptr_issued_q_slots;
          curr_dram_q_state->dram_ptr_issued_byte = start_bytes;
          curr_dram_q_state->dram_num_partial_q_slot_issued_tiles = 0;
          l1_ptrs->l1_to_dram.wr_dram_rdptr = l1_ptrs->dram_to_l1.rd_dram_rdptr;

          if (is_dram_streaming_write) {
            l1_ptrs->dram_to_l1.dram_streaming_tag = 0;
            uint32_t output_noc = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_INPUT_NOC_FLAG) >> 1;
            uint64_t dram_ptr_addr = tt_l1_load(&l1_ptrs->l1_to_dram.dram_streaming_header_addr);
            // L1 and register reads, flushed by immediate use below
            while (!ncrisc_noc_fast_write_ok_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
            ncrisc_noc_fast_write_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF, l1_mem::address_map::ZEROS_BASE, dram_ptr_addr+DRAM_BUF_STREAMING_TAG_OFFSET, 4,
                                  DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
          }
#ifdef DRAM_DECOUPLE
          //Check whether its a decoupled stream.
          if ((l1_ptrs->dram_to_l1.dram_decoupled & STREAM_DRAM_IO) != 0) {
            uint16_t stream_id = curr_dram_q_state->dram_q_stream_id;

            if ((l1_ptrs->dram_to_l1.dram_decoupled & STREAM_DRAM_INPUT) != 0) {
              if ((l1_ptrs->dram_to_l1.rd_flags & DRAM_Q_DECOUPLED_FLAG) != 0) {
                risc_decoupled_stream_buffer_init_l1(stream_id);
              } else {
                l1_ptrs->dram_to_l1.dram_decoupled = 0;
              }
            } else if ((l1_ptrs->dram_to_l1.dram_decoupled & STREAM_DRAM_OUTPUT) != 0) {
              if ((l1_ptrs->dram_to_l1.rd_flags & DRAM_Q_DECOUPLED_FLAG) != 0) {
                uint32_t output_noc = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_INPUT_NOC_FLAG) >> 1;
                for (uint32_t i = 0; i < num_active_streams; i++) {
                  volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
                  if (stream_id == l1_stream_info->stream_id) {
                    risc_decoupled_dram_buffer_init_l1(l1_ptrs, output_noc, l1_stream_info->msg_info_buf_start);
                    break;
                  }
                }
              } else {
                l1_ptrs->dram_to_l1.dram_decoupled = 0;
              }
            } else {
              l1_ptrs->dram_to_l1.dram_decoupled = 0;
            }
          }
#endif
          curr_dram_q_state->dram_q_state_flags |= DRAM_Q_PTRS_LOADED_FLAG;
        }
      }

    }
  }

  // Dram prefetch - read data
  // Goes though all of the Prologue buffers and loads them from DRAM to L1
  for (uint32_t i = 0; i < num_dram_prefetch_streams; i++) {
    curr_active_stream_info = dram_prefetch_active_stream_info[i];
    uint32_t stream_id = curr_active_stream_info->stream_id;
    uint32_t flags = curr_active_stream_info->flags;

    if ((flags & STREAM_DRAM_INPUT) != 0) {
#ifndef PERF_DUMP
      RISC_POST_STATUS(0xA8500000 | stream_id);
#endif

      epoch_stream_info_t tt_l1_ptr * l1_stream_info = dram_prefetch_epoch_stream_info[i];
      epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
      volatile dram_io_state_t tt_l1_ptr * l1_dram_io_state = l1_dram_io_info->dram_io_state;
      uint32_t input_noc = l1_dram_io_info->input_noc;
      uint32_t stream_buf_size_bytes = l1_stream_info->buf_full_size_bytes;
      uint32_t stream_buf_addr = l1_stream_info->buf_base_addr;

      if ((l1_dram_io_state->dram_to_l1.rd_flags & DRAM_Q_HEADER_ZERO_FLAG) != 0) {
        uint64_t replicate_dest_addr = NOC_XY_ADDR(my_x[input_noc], my_y[input_noc], stream_buf_addr);
        replicate_l1(input_noc, l1_mem::address_map::ZEROS_BASE, replicate_dest_addr, ZERO_GRAD_CHUNK_SIZE_BYTES, stream_buf_size_bytes >> LOG_ZERO_GRAD_CHUNK_SIZE_BYTES, NCRISC_WR_LOCAL_TRID);
        uint32_t last_offset = ((stream_buf_size_bytes >> LOG_ZERO_GRAD_CHUNK_SIZE_BYTES) << LOG_ZERO_GRAD_CHUNK_SIZE_BYTES);
        uint32_t bytes_left = stream_buf_size_bytes - last_offset;
        if (bytes_left > 0) {
          replicate_l1(input_noc, l1_mem::address_map::ZEROS_BASE, replicate_dest_addr + last_offset, bytes_left, 1, NCRISC_WR_LOCAL_TRID);
        }
        uint32_t stream_msg_info_buf_ptr = (l1_stream_info->msg_info_buf_start)*MEM_WORD_WIDTH;
        uint32_t tile_size_words = *(volatile uint32_t tt_l1_ptr *)(stream_msg_info_buf_ptr);
        uint32_t tile_size_bytes = tile_size_words*MEM_WORD_WIDTH;
        while (!ncrisc_noc_nonposted_writes_flushed_l1(input_noc, NCRISC_WR_LOCAL_TRID));
        for (uint32_t tile_header_ptr = stream_buf_addr; tile_header_ptr < stream_buf_addr + stream_buf_size_bytes; tile_header_ptr += tile_size_bytes) {
          *((uint32_t *)(tile_header_ptr)) = tile_size_words;
        }
      } else {
        dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state = l1_dram_io_state->local.dram_io_scatter_state;
        bool is_dram_io_scatter = dram_io_scatter_state != ((dram_io_scatter_state_t*)0);
        if (is_dram_io_scatter) {
          uint32_t dram_ptr_issued_q_slots = ((flags & STREAM_DRAM_RAM) != 0) ? l1_dram_io_state->dram_to_l1.rd_dram_rdptr : l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr;
          uint32_t inc_size_bytes = l1_dram_io_info->dram_q_slot_size_bytes;
          uint32_t dram_data_buf_size_bytes = l1_dram_io_state->local.dram_buf_size_bytes;
          uint32_t buf_offset = buf_ptr_inc_wrap(0, mulsi3(dram_ptr_issued_q_slots,inc_size_bytes), dram_data_buf_size_bytes);

          uint32_t dram_io_scatter_size = dram_io_scatter_state->scatter_offsets_size;
          volatile tt_uint64_t tt_l1_ptr * scatter_offsets = dram_io_scatter_state->scatter_offsets; // Since this is prolog we cannot use an external index table as dram io code is not proccessed during prolog.
          uint32_t dram_io_scatter_chunk_size_bytes = dram_io_scatter_state->scatter_chunk_size_bytes;
          uint32_t next_stream_buf_addr = stream_buf_addr;
          for (uint32_t scatter_idx = 0; scatter_idx < dram_io_scatter_size; scatter_idx++) {
            uint64_t dram_src_addr_w_offset = tt_l1_load(&scatter_offsets[scatter_idx]);
            bool is_padding_set = ((dram_src_addr_w_offset >> 32) & IS_DRAM_PADDING_SET) != 0;
            dram_src_addr_w_offset = dram_src_addr_w_offset & ~(((uint64_t)IS_DRAM_PADDING_SET) << 32);
            if (!is_padding_set)
              dram_src_addr_w_offset += (buf_offset + DRAM_BUF_DATA_OFFSET);
            ncrisc_noc_fast_read_any_len_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_src_addr_w_offset, next_stream_buf_addr, dram_io_scatter_chunk_size_bytes, NCRISC_RD_START_TRID);
            next_stream_buf_addr += dram_io_scatter_chunk_size_bytes;
          }
        } else {
          bool is_dram_padding = l1_dram_io_state->l1_to_dram.dram_padding;
          uint64_t dram_src_addr_w_offset = tt_l1_load(&l1_dram_io_state->local.dram_buf_addr);
          if (!is_dram_padding) {
            uint32_t dram_ptr_issued_q_slots = ((flags & STREAM_DRAM_RAM) != 0) ? l1_dram_io_state->dram_to_l1.rd_dram_rdptr : l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr;
            uint32_t inc_size_bytes = l1_dram_io_info->dram_q_slot_size_bytes;
            uint32_t dram_data_buf_size_bytes = l1_dram_io_state->local.dram_buf_size_bytes;
            uint32_t buf_offset = buf_ptr_inc_wrap(0, mulsi3(dram_ptr_issued_q_slots,inc_size_bytes), dram_data_buf_size_bytes);
            dram_src_addr_w_offset += (DRAM_BUF_DATA_OFFSET+buf_offset);
          }
          ncrisc_noc_fast_read_any_len_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_src_addr_w_offset, stream_buf_addr, stream_buf_size_bytes, NCRISC_RD_START_TRID);
        }
      }
    }
  }

  curr_dram_output_stream_state_init = dram_output_stream_state;
  for (uint32_t i = 0; i < num_dram_output_streams; i++, curr_dram_output_stream_state_init++) {
    dram_q_state_t tt_l1_ptr * next_dram_q_issue = curr_dram_output_stream_state_init->next_dram_q_issue;
    uint32_t dram_ptr_issued_byte = next_dram_q_issue->dram_ptr_issued_byte;
    if (curr_dram_output_stream_state_init->moves_raw_data) {
      curr_dram_output_stream_state_init->prev_row_ptr_bytes   = dram_ptr_issued_byte;
      curr_dram_output_stream_state_init->prev_zc_ptr_bytes    = dram_ptr_issued_byte;
      curr_dram_output_stream_state_init->prev_zr_ptr_bytes    = dram_ptr_issued_byte;
      curr_dram_output_stream_state_init->prev_batch_ptr_bytes = dram_ptr_issued_byte;
    } else {
      curr_dram_output_stream_state_init->prev_row_ptr_bytes   = 0;
      curr_dram_output_stream_state_init->prev_zc_ptr_bytes    = 0;
      curr_dram_output_stream_state_init->prev_zr_ptr_bytes    = 0;
      curr_dram_output_stream_state_init->prev_batch_ptr_bytes = 0;
    }
  }

  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    while (!ncrisc_noc_reads_flushed_l1(n, NCRISC_RD_START_TRID));
  }
  
#ifndef PERF_DUMP
  RISC_POST_STATUS(0xA9000000);
#endif  
  // Dram prefetch - push
  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {
    volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
    uint32_t stream_id = l1_stream_info->stream_id;
    uint32_t flags = l1_stream_info->flags;

    if (((flags & STREAM_DRAM_INPUT) != 0) && ((flags & STREAM_DRAM_IO) == 0) && ((flags & STREAM_DRAM_STREAMING) == 0)) {
      if ((flags & STREAM_INTERMEDIATE) != 0 || (flags & STREAM_MOVES_RAW_DATA) != 0) {
        uint32_t stream_phase_tiles = stream_src_endpoint_get_phase_tiles_count(stream_id);
        volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
        uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
        uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, stream_phase_tiles);
        tiles_received_ptr[0] = new_epoch_tiles_received;
      } else {
        uint32_t blob_start_addr = l1_stream_info->blob_start_addr;
        uint32_t start_phase_num_cfg_regs = l1_stream_info->start_phase_num_cfg_regs;
        stream_phase_blob_run(stream_id, blob_start_addr, start_phase_num_cfg_regs);
      }
    }
  }
  
  RISC_EPOCH_INFO_PTR->all_streams_ready = 1;
#if defined(PERF_DUMP)
  risc::record_timestamp_at_offset_l1(risc::perf_event::STREAM_HANDLER_INIT, risc::PROLOGUE_END_OFFSET);
#endif
}

// This is the main loop during the epoch. 
// It's processing DRAM reads and write in a non-blocking manner for each of the streams. 
void risc_dram_stream_handler_loop(
#ifdef RISC_GSYNC_ENABLED
  volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
  uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
  dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t tt_l1_ptr *active_stream_info,
  epoch_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info
#ifdef ERISC
  //unpack/pack interface for Data Copy Op.
  ,
  uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
  uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state
#endif
) {

#ifdef PERF_DUMP
  uint32_t perf_spill_check_cnt = 1;
  risc::record_timestamp_at_offset(risc::perf_event::STREAM_HANDLER_LOOP, risc::STREAM_HANDLER_START_OFFSET);
#endif
  uint32_t dram_ptr_update_cnt = 1; // I.e. dont check immediately since we checked in prologue
#ifdef ERISC
  uint32_t stream_restart_check_cnt = 1; // I.e. dont check immediately since streams most likely just started
  uint32_t stream_handler_iteration_count = 0;
#endif
  while (!risc_streams_kernels_done()) {

#ifdef ERISC
    stream_handler_iteration_count++;
    if (stream_handler_iteration_count >= ERISC_LOOP_ITER_COUNT) {
      stream_handler_iteration_count = 0;
      risc_context_switch();
    }
#endif

    RISC_POST_STATUS(0x11111111);

#ifdef PERF_DUMP
    bool check_dram_spill = (perf_spill_check_cnt == 0);
    perf_spill_check_cnt = (perf_spill_check_cnt + 1) & risc::PERF_SPILL_CHECK_MASK;
    if (check_dram_spill) {
      call_with_cpu_flush((void *)risc::check_dram_spill_requests_once, 0);
    }
#endif

#ifdef ERISC
    risc_stream_resart_check(stream_restart_check_cnt, num_active_streams, active_stream_info);
    //call unpack/pack handler to handle Data Copy Ops.
    risc_unpack_pack_stream_handler_loop(
       num_input_streams, input_stream_state,
       num_output_streams, output_stream_state,
       num_active_streams, active_stream_info,
       true
    );
#endif

    check_whether_poll_immediately(dram_ptr_update_cnt);

    bool dram_ptr_update = ((dram_ptr_update_cnt & DRAM_PTR_UPDATE_MASK) == 0);
    dram_ptr_update_cnt = (dram_ptr_update_cnt & ~DRAM_PTR_UPDATE_MASK) | ((dram_ptr_update_cnt & DRAM_PTR_UPDATE_MASK) + 1) ;

    if (dram_ptr_update) {
      RISC_POST_STATUS(0xC0000000);
      bool check_ptrs_flushed = false;
      bool all_ptrs_flushed;
  #if !defined(RISC_B0_HW) && (defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT))
      call_with_cpu_flush_args4((void *)poll_dram_ptrs,
                                (void *) &num_active_dram_queues, (void *) dram_q_state, (void *) &check_ptrs_flushed, (void *) &all_ptrs_flushed
      );
  #else
      poll_dram_ptrs(0, num_active_dram_queues, dram_q_state, check_ptrs_flushed, all_ptrs_flushed);
  #endif
    }

    dram_input_stream_state_t* curr_dram_input_stream_state = dram_input_stream_state;
    for (uint32_t i = 0;
         i < num_dram_input_streams;
         i++, curr_dram_input_stream_state++) {

      dram_q_state_t tt_l1_ptr * next_dram_q_issue_temp = curr_dram_input_stream_state->next_dram_q_issue;
      dram_q_state_t tt_l1_ptr * next_dram_q_issue = (dram_q_state_t tt_l1_ptr *)next_dram_q_issue_temp;
      uint32_t stream_id = curr_dram_input_stream_state->stream_id;
      uint32_t moves_raw_data = curr_dram_input_stream_state->moves_raw_data;
      uint32_t epoch_q_slots_remaining = curr_dram_input_stream_state->epoch_q_slots_remaining;
      uint32_t phase_active;
      uint32_t curr_phase_tiles_remaining_to_issue;
      read_phase_active(stream_id, curr_dram_input_stream_state, phase_active, curr_phase_tiles_remaining_to_issue);
      bool is_dram_streaming_read = next_dram_q_issue->l1_dram_incoming_ptr_index && (next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) != 0;
      
      if (!is_dram_streaming_read) {
        uint32_t prev_in_flight_data_rec_chunks = curr_dram_input_stream_state->in_flight_data_rec_chunks;
        bool q_ptr_update_pending = curr_dram_input_stream_state->q_ptr_update_pending;
        bool is_dram_read_opt_enabled = curr_dram_input_stream_state->dram_read_opt_enabled || stream_is_dram_read_opt_enabled(stream_id);
        curr_dram_input_stream_state->dram_read_opt_enabled = is_dram_read_opt_enabled;

        if (moves_raw_data) {
          if (phase_active && !curr_phase_tiles_remaining_to_issue && !prev_in_flight_data_rec_chunks) {
            uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
            uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
            uint16_t tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
            if (tiles_available == 0) {
              stream_force_next_phase(stream_id);
              phase_active = false;
            }
          }
        }

        if (epoch_q_slots_remaining && (is_dram_read_opt_enabled || (phase_active && curr_phase_tiles_remaining_to_issue && !q_ptr_update_pending))) {

          RISC_POST_STATUS(0xD1000000 | stream_id);

          volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_issue->l1_dram_ptrs;
          uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
          bool can_do_read = can_do_another_read(curr_dram_input_stream_state->input_noc, data_rec_chunk_size_bytes, curr_dram_input_stream_state->prev_rd_data_word_recv);

          if (can_do_read) {

            RISC_POST_STATUS(0xD1200000);

            uint32_t stream_buf_bytes_free;
            bool stream_has_free_space;
            get_buf_space_available_words(curr_dram_input_stream_state, next_dram_q_issue, stream_id, moves_raw_data, 
                                          stream_buf_bytes_free, stream_has_free_space);
            
            RISC_POST_DEBUG(stream_buf_bytes_free);
            RISC_POST_DEBUG(0xD1300000 | stream_has_free_space);

            if (stream_has_free_space) {

              RISC_POST_STATUS(0xD2000000);

  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
              if (!curr_dram_input_stream_state->stream_buf_available) {
                risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
                curr_dram_input_stream_state->stream_buf_available = 1;
              }
  #endif

              if (stream_dram_read_should_reset_pointers(stream_id)) {
                curr_dram_input_stream_state->stream_flushed_ptr_byte = 0;
              }

              // Implements Scatter read, Embeddings and Tilize. MMIO pipes are elsewhere
              dram_input_stream_scatter_read(stream_id, curr_phase_tiles_remaining_to_issue, stream_buf_bytes_free, curr_dram_input_stream_state, next_dram_q_issue, dram_ptr_update_cnt);
            }
  #if defined(PERF_DUMP) && PERF_DUMP_LEVEL > 2
            else {
              if (curr_dram_input_stream_state->stream_buf_available) {
                risc::record_timestamp(risc::perf_event::STREAM_BUF_STATUS | stream_id);
                curr_dram_input_stream_state->stream_buf_available = 0;
              }
            }
  #endif
          }
        }
        
        if ((is_dram_read_opt_enabled || q_ptr_update_pending || phase_active) && (prev_in_flight_data_rec_chunks > 0)) {

          RISC_POST_STATUS(0xE0000000 | stream_id);

          dram_q_state_t tt_l1_ptr * next_dram_q_in_flight = curr_dram_input_stream_state->next_dram_q_in_flight;
          volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)next_dram_q_in_flight->l1_dram_ptrs;
          dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state = l1_ptrs->local.dram_io_scatter_state;
          uint32_t input_noc = curr_dram_input_stream_state->input_noc;
          uint32_t stream_buf_addr_byte = curr_dram_input_stream_state->stream_buf_addr_byte;
          uint32_t stream_flushed_ptr_byte = curr_dram_input_stream_state->stream_flushed_ptr_byte;
          uint32_t data_rec_chunk_size_bytes = l1_ptrs->l1_to_dram.data_chunk_size_bytes;
#ifdef DRAM_DECOUPLE
          uint32_t dram_decoupled = l1_ptrs->dram_to_l1.dram_decoupled;
#else
          uint32_t dram_decoupled = 0;
#endif
          uint32_t data_rec_chunk_pending_start_addr = stream_buf_addr_byte + stream_flushed_ptr_byte;
          bool is_dram_io_scatter = dram_io_scatter_state != ((dram_io_scatter_state_t*)0);
          uint32_t scatter_chunk_size_bytes = is_dram_io_scatter ? dram_io_scatter_state->scatter_chunk_size_bytes : data_rec_chunk_size_bytes;
          uint32_t transaction_id = curr_dram_input_stream_state->transaction_id_flushed;
          bool next_data_rec_chunk_flushed = dram_decoupled ? true : dram_input_stream_check_next_chunk_flushed(input_noc, data_rec_chunk_pending_start_addr, data_rec_chunk_size_bytes, scatter_chunk_size_bytes, transaction_id);

          // event management code that happens when data comes back
          // checks the stream, and whether it is in the state to have data pushed into it
          // if so, ncrisc pushes data into stream and have stream sends it out 
          if (is_dram_read_opt_enabled) {
            read_phase_active(stream_id, curr_dram_input_stream_state, phase_active, curr_phase_tiles_remaining_to_issue);
            if (moves_raw_data) {
              if (phase_active && !curr_phase_tiles_remaining_to_issue) {
                uint16_t tiles_available = 0;
                if (stream_next_phase_src_change(stream_id)) {
                  uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
                  uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
                  tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
                }
                if (tiles_available == 0) {
                  stream_force_next_phase(stream_id);
                  phase_active = false;
                }
              }
            }
          }

          RISC_POST_DEBUG(0xE0100000 | input_noc);
          RISC_POST_DEBUG(stream_flushed_ptr_byte);
          RISC_POST_DEBUG(data_rec_chunk_pending_start_addr);
          RISC_POST_DEBUG(data_rec_chunk_size_bytes);
          RISC_POST_DEBUG(0xE0200000 | next_data_rec_chunk_flushed);

          if (next_data_rec_chunk_flushed && (!is_dram_read_opt_enabled || ((q_ptr_update_pending || phase_active) && curr_phase_tiles_remaining_to_issue))) {
            
            RISC_POST_STATUS(0xE1000000);

            uint32_t in_flight_data_rec_chunks = curr_dram_input_stream_state->in_flight_data_rec_chunks;
            uint32_t data_rec_chunk_size_tiles = l1_ptrs->l1_to_dram.data_chunk_size_tiles;
            uint32_t q_slot_size_tiles = l1_ptrs->local.dram_q_slot_size_tiles;
            uint32_t flushed_partial_q_slot_tiles = next_dram_q_in_flight->flushed_partial_q_slot_tiles;
            uint32_t stream_buf_size_bytes = stream_get_data_buf_size(stream_id) * MEM_WORD_WIDTH;

            // once data has been sent out, update dram pointer variables so you clear tiles from the dram so you dont read it again
            while (next_data_rec_chunk_flushed) {

              flushed_partial_q_slot_tiles += data_rec_chunk_size_tiles;
              bool full_q_slot_flushed = flushed_partial_q_slot_tiles == q_slot_size_tiles;

              uint32_t prev_data_rec_chunk_size_tiles = data_rec_chunk_size_tiles;
              uint32_t prev_data_rec_chunk_size_bytes = data_rec_chunk_size_bytes;

              if (full_q_slot_flushed) {
                bool update_pass = dram_input_stream_scatter_queue_update(stream_id, epoch_q_slots_remaining, moves_raw_data, input_noc, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes, 
                                                                          curr_dram_input_stream_state, next_dram_q_in_flight, dram_ptr_update_cnt, flushed_partial_q_slot_tiles);
                if (!update_pass)
                  break;
              } else {
                next_dram_q_in_flight->flushed_partial_q_slot_tiles = flushed_partial_q_slot_tiles;
              }

              if (is_dram_read_opt_enabled) {
                curr_phase_tiles_remaining_to_issue -= prev_data_rec_chunk_size_tiles;
                stream_src_endpoint_set_phase_tiles_count(stream_id, curr_phase_tiles_remaining_to_issue);

                uint32_t fork_index = 0;
                while (true) {
                  uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
                  if (fork_stream_id == NULL_STREAM) {
                    break;
                  }
                  stream_src_endpoint_set_phase_tiles_count(fork_stream_id, curr_phase_tiles_remaining_to_issue);
                  fork_index++;
                }
              }

              if (!curr_dram_input_stream_state->q_ptr_update_pending)
                push_readdata_to_stream(curr_dram_input_stream_state, stream_id, moves_raw_data, prev_data_rec_chunk_size_tiles, prev_data_rec_chunk_size_bytes);
              else
                curr_dram_input_stream_state->q_ptr_update_pending = false;

              prev_in_flight_data_rec_chunks--;
              in_flight_data_rec_chunks--;
              curr_dram_input_stream_state->in_flight_bytes -= prev_data_rec_chunk_size_bytes;
              stream_flushed_ptr_byte = buf_ptr_inc_wrap(stream_flushed_ptr_byte, prev_data_rec_chunk_size_bytes, stream_buf_size_bytes);

              RISC_POST_DEBUG(0xE2000000 | input_noc);
              RISC_POST_DEBUG(stream_flushed_ptr_byte);
              RISC_POST_DEBUG(epoch_q_slots_remaining);
              RISC_POST_DEBUG(0xE3000000 | prev_in_flight_data_rec_chunks);

              if (is_dram_read_opt_enabled) {
                if (!curr_phase_tiles_remaining_to_issue) {
                  break;
                }
              }

              if (!prev_in_flight_data_rec_chunks) {
                break;
              }
              data_rec_chunk_pending_start_addr = stream_buf_addr_byte + stream_flushed_ptr_byte;
              next_data_rec_chunk_flushed = dram_decoupled ? true : dram_input_stream_check_next_chunk_flushed(input_noc, data_rec_chunk_pending_start_addr, data_rec_chunk_size_bytes, scatter_chunk_size_bytes, transaction_id);
            }
            curr_dram_input_stream_state->transaction_id_flushed = transaction_id;
            curr_dram_input_stream_state->next_dram_q_in_flight = next_dram_q_in_flight;
            curr_dram_input_stream_state->epoch_q_slots_remaining = epoch_q_slots_remaining;
            curr_dram_input_stream_state->in_flight_data_rec_chunks = in_flight_data_rec_chunks;
            curr_dram_input_stream_state->stream_flushed_ptr_byte = stream_flushed_ptr_byte;
          }
        }
      } else { //is_dram_streaming_read
#if defined(ERISC) || defined(RISC_B0_HW)
        // in this mode, we prented data is not in DRAM, but in L1. 
        // what really happens is over PCIE some core will write into L1, and then we will read from L1
        // and pushing out directly.
        process_dram_streaming_read_l1(0, stream_id, phase_active, curr_phase_tiles_remaining_to_issue, curr_dram_input_stream_state, next_dram_q_issue, epoch_q_slots_remaining);
#else
        call_with_cpu_flush_args3((void *)process_dram_streaming_read_l1,
                                  (void *) &stream_id, (void *) &phase_active, (void *) &curr_phase_tiles_remaining_to_issue, (void *) curr_dram_input_stream_state, (void *) next_dram_q_issue, (void *) &epoch_q_slots_remaining
        );
#endif
      }
    }

    uint32_t total_tiles_to_clear = 0;
    // send the data out to dram
    process_dram_write(num_dram_output_streams, dram_output_stream_state, dram_ptr_update_cnt, total_tiles_to_clear);
    // once the date is sent out, one needs to clear the stream
    process_dram_write_clear(num_dram_output_streams, dram_output_stream_state, total_tiles_to_clear);
  }
  // finished dram reads/writes and brisc is done

  RISC_POST_STATUS(0x22223333);

  bool check_ptrs_flushed = true;
  bool all_ptrs_flushed;
#if !defined(RISC_B0_HW) && (defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT))
  call_with_cpu_flush_args4((void *)poll_dram_ptrs,
                            (void *) &num_active_dram_queues, (void *) dram_q_state, (void *) &check_ptrs_flushed, (void *) &all_ptrs_flushed
  );
#else
  poll_dram_ptrs(0, num_active_dram_queues, dram_q_state, check_ptrs_flushed, all_ptrs_flushed);
#endif
  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    while (!ncrisc_noc_reads_flushed(n, NCRISC_HEADER_RD_TRID));
  }
  while (true) {
#if !defined(RISC_B0_HW) && (defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT))
    call_with_cpu_flush_args4((void *)poll_dram_ptrs,
                              (void *) &num_active_dram_queues, (void *) dram_q_state, (void *) &check_ptrs_flushed, (void *) &all_ptrs_flushed
    );
#else
    poll_dram_ptrs(0, num_active_dram_queues, dram_q_state, check_ptrs_flushed, all_ptrs_flushed);
#endif
    if (all_ptrs_flushed)
      break;
    for (uint32_t n = 0; n < NUM_NOCS; n++) {
      while (!ncrisc_noc_reads_flushed(n, NCRISC_HEADER_RD_TRID));
    }
  }

}

// copy epilogue buffers into L1
void risc_dram_stream_handler_epilogue_l1(
  void * pFunction,
#ifdef RISC_GSYNC_ENABLED
  volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
  uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
  dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t *active_stream_info,
  epoch_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info
) {

#ifdef PERF_DUMP
  risc::record_timestamp_at_offset_l1(risc::perf_event::STREAM_HANDLER_LOOP, risc::STREAM_HANDLER_END_OFFSET);
  risc::record_timestamp_at_offset_l1(risc::perf_event::EPOCH_EPILOGUE, risc::EPILOGUE_START_OFFSET);
#endif
  // Full handshake with brisc
  RISC_EPOCH_INFO_PTR->all_streams_ready = 0;

  RISC_POST_STATUS(0x22222222);

  dram_q_state_t tt_l1_ptr * curr_dram_q_state = dram_q_state;
  for (uint32_t i = 0, old_i = 0; i < num_active_dram_queues; i++, curr_dram_q_state = old_i == MAX_L0_DRAM_Q_STATE_T-1 ? (dram_q_state_t tt_l1_ptr *)RISC_EPOCH_INFO_PTR->extra_dram_q_state_addr : curr_dram_q_state + 1) {
    old_i = i; // Not sure if i++ happens before or after the other incremrent, this statement makes it clear what the order should be
    volatile dram_io_state_t tt_l1_ptr * l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)curr_dram_q_state->l1_dram_ptrs;
    uint8_t q_stream_id = curr_dram_q_state->dram_q_stream_id;
    uint32_t stream_vc = stream_get_output_unicast_vc(q_stream_id);

    if (l1_ptrs->l1_to_dram.wr_stride == (DRAM_STRIDE_WRAP_BIT + 0)) {
      uint32_t input_noc = (curr_dram_q_state->dram_q_state_flags & DRAM_Q_INPUT_NOC_FLAG) >> 1;
      uint64_t dram_buf_addr_plus_offset = tt_l1_load(&l1_ptrs->local.dram_buf_addr) + DRAM_BUF_STRIDE_OFFSET;
      uint32_t l1_dram_incoming_ptr_index = curr_dram_q_state->l1_dram_incoming_ptr_index;
      l1_ptrs->l1_to_dram.wr_stride = 0;
      uint32_t output_vc = l1_dram_incoming_ptr_index ? DRAM_PTR_UPDATE_VC : stream_vc;
      // Reg poll loop, flushed immediately
      while (!ncrisc_noc_fast_write_ok_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write_l1(input_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_stride)), dram_buf_addr_plus_offset, 2,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
    }
  }

#ifndef ERISC
  for (uint32_t i = 0; i < num_dram_prefetch_streams; i++) {
    active_stream_info_t tt_l1_ptr * curr_active_stream_info = dram_prefetch_active_stream_info[i];
    uint32_t flags = curr_active_stream_info->flags;
    if ((flags & STREAM_DRAM_OUTPUT) != 0) {
      uint32_t stream_id = curr_active_stream_info->stream_id;
      epoch_stream_info_t tt_l1_ptr * l1_stream_info = dram_prefetch_epoch_stream_info[i];
      epoch_stream_dram_io_info_t tt_l1_ptr * l1_dram_io_info = l1_stream_info->dram_io_info;
      uint32_t output_noc = stream_get_output_noc(stream_id);
      uint32_t output_vc = stream_get_output_unicast_vc(stream_id);
      volatile dram_io_state_t tt_l1_ptr * l1_dram_io_state = l1_dram_io_info->dram_io_state;
      uint64_t dram_dest_addr = tt_l1_load(&l1_dram_io_state->local.dram_buf_addr);
      //uint32_t gwr_ptr_autoinc = l1_dram_io_state->dram_to_l1.rd_gwr_ptr_autoinc;
      uint32_t stream_buf_addr_byte = l1_stream_info->buf_base_addr;
      uint32_t stream_buf_size_bytes = l1_stream_info->buf_full_size_bytes;
      uint32_t inc_size_bytes = l1_dram_io_info->dram_q_slot_size_bytes;
      uint32_t dram_data_buf_size_bytes = l1_dram_io_state->local.dram_buf_size_bytes;
      uint32_t dram_ptr_issued_q_slots = ((flags & STREAM_DRAM_RAM) != 0) ? l1_dram_io_state->dram_to_l1.rd_dram_wrptr : l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr;
      uint32_t buf_offset = buf_ptr_inc_wrap(0, mulsi3(dram_ptr_issued_q_slots,inc_size_bytes), dram_data_buf_size_bytes);
      ncrisc_noc_fast_write_any_len_l1(output_noc, NCRISC_WR_CMD_BUF, stream_buf_addr_byte, dram_dest_addr+(DRAM_BUF_DATA_OFFSET+buf_offset), stream_buf_size_bytes, output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
      if ((flags & STREAM_DRAM_RAM) != 0) {
        //l1_dram_io_state->l1_to_dram.wr_dram_wrptr = dram_ptr_issued_q_slots + gwr_ptr_autoinc;
        l1_dram_io_state->l1_to_dram.wr_dram_wrptr = dram_ptr_issued_q_slots + 1;
      } else {
        //l1_dram_io_state->l1_to_dram.wr_dram_wrptr = dram_io_incr_ptr_l1(dram_ptr_issued_q_slots, dram_io_local_empty(l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr, l1_dram_io_state->dram_to_l1.rd_dram_rdptr, dram_ptr_issued_q_slots) ? gwr_ptr_autoinc : 0, 1);
        l1_dram_io_state->l1_to_dram.wr_dram_wrptr = dram_io_incr_ptr_l1(dram_ptr_issued_q_slots, dram_io_local_empty(l1_dram_io_state->dram_to_l1.rd_dram_local_rdptr, l1_dram_io_state->dram_to_l1.rd_dram_rdptr, dram_ptr_issued_q_slots) ? 1 : 0, 1);
        while (!ncrisc_noc_fast_write_ok_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_write_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_dram_io_state->l1_to_dram.wr_dram_wrptr)), dram_dest_addr+DRAM_BUF_WRPTR_OFFSET, 2,
                              output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
      }
#ifdef EPOCH_CHECK_ENABLED
      if (l1_dram_io_state->dram_to_l1.rd_epoch_id_tag != DRAM_QUEUE_NO_EPOCH_CHECK)
        l1_dram_io_state->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_EPOCH_CHECK_EN | (RISC_EPOCH_INFO_PTR->epoch_id);
      else
        l1_dram_io_state->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
#else
      // No epoch check works around #928, however it will not support multiple epoch consumers that need to sync on the same queue
      l1_dram_io_state->l1_to_dram.wr_epoch_id_tag = DRAM_QUEUE_NO_EPOCH_CHECK;
#endif
      while (!ncrisc_noc_fast_write_ok_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_write_l1(output_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_dram_io_state->l1_to_dram.wr_epoch_id_tag)), dram_dest_addr+DRAM_BUF_EPOCH_ID_TAG_OFFSET, 2,
                            output_vc, false, false, 1, NCRISC_WR_DEF_TRID);
    }
  }
#endif

  RISC_POST_STATUS(0x33333333);

#ifdef ERISC
  risc_all_streams_done_check(num_active_streams, active_stream_info);
#endif

  // Reset streams that need to be reset
  // We cannot easily reset this in brisc because we can have a case where brisc wants to go idle but risc hasnt loaded 
  // dram io yet (for example if the dram io stream goes to another core)
  dram_input_stream_state_t* curr_dram_input_stream_state = dram_input_stream_state;
  for (uint32_t i = 0;
       i < num_dram_input_streams;
       i++, curr_dram_input_stream_state++) {
        uint32_t stream_id = curr_dram_input_stream_state->stream_id;
        uint32_t moves_raw_data = curr_dram_input_stream_state->moves_raw_data;

        if (moves_raw_data) {
          while (stream_phase_is_active(stream_id)) {
            uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
            uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
            uint16_t tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
            if (tiles_available == 0) {
              stream_force_next_phase(stream_id);
            }
          }
        }

        while (!stream_phase_advance_wait(stream_id));
        stream_reset(stream_id);

        uint32_t fork_index = 0;
        while (true) {
          uint32_t fork_stream_id = curr_dram_input_stream_state->fork_stream_ids[fork_index];
          if (fork_stream_id == NULL_STREAM) {
            break;
          }
          while (!stream_phase_advance_wait(fork_stream_id));
          stream_reset(fork_stream_id);
          fork_index++;
        }
  }

  RISC_POST_STATUS(0x44444444);

  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    while (!ncrisc_noc_all_reads_flushed_l1(n));
  }
  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    while (!ncrisc_noc_nonposted_all_writes_flushed_l1(n));
  }

#ifdef RISC_GSYNC_ENABLED
  RISC_POST_STATUS(0x55555555);
  global_sync_update(gsync_epoch, epochs_in_progress);
#endif

#ifdef PERF_DUMP
  //Last check for dram spill requests before epoch finishes.
  //Trisc Kernels send a last spill request for last partial half buffer when kernels finish.
  risc::check_dram_spill_requests(true);
  risc::record_timestamp_at_offset_l1(risc::perf_event::EPOCH_EPILOGUE, risc::EPILOGUE_END_OFFSET);
#endif

}

