// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _DRAM_STREAM_INTF_H_
#define _DRAM_STREAM_INTF_H_

#include "dram_stream_intf_constants.h"
#include "epoch.h"
#include "stream_interface.h"
#include "risc.h"

#ifdef ERISC
#include "unpack_pack_stream_intf.h"
#endif
///////////////////////////////
//       VC Allocation       //
///////////////////////////////
// VC 0: Perf dump,          //
//       Stream to DRAM Data //
// VC 1: DRAM req/ptr        //
// VC 2: Stream data         //
// VC 3: Stream flow ctrl    //
// VC 4: Multicast           //
// VC 5: Unused              //
///////////////////////////////

// FIXME imatosevic - check assumptions on field width and struct size:
// - up to 64K-1 iterations in epoch
// - up to 16K-1 slots in dram queue
// - up to 8 active DRAM IO input streams
// - up to 8 active DRAM IO output streams
// - up to 40 total active DRAM queues
// - flags field has only bits [15:0] used

const uint32_t DRAM_BUF_RDPTR_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_dram_rdptr);
const uint32_t DRAM_BUF_WRPTR_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_dram_wrptr);
const uint32_t DRAM_BUF_LOCAL_RDPTR_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_dram_local_rdptr);
const uint32_t DRAM_BUF_EPOCH_ID_TAG_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_epoch_id_tag);
const uint32_t DRAM_BUF_STRIDE_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_stride);
const uint32_t DRAM_BUF_QUEUE_UPDATE_STRIDE_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, rd_queue_update_stride);
const uint32_t DRAM_BUF_STREAMING_TAG_OFFSET = offsetof(dram_io_state_t::dram_to_l1_t, dram_streaming_tag);
const uint32_t DRAM_BUF_DATA_OFFSET = sizeof(dram_io_state_t::dram_to_l1_t);

typedef struct quick_q_t { // Support for 2 entires only, cant support stream 0
  uint32_t entry_0;
  uint32_t entry_1;
  
  uint32_t peek(int index=0) {
    uint32_t ret_val;
    if (index == 1) {
      ret_val = entry_1;
    } else {
      ret_val = entry_0;
    }
    return ret_val;
  }
  void push(uint32_t val) {
    if (entry_1) {
      RISC_POST_STATUS(0xDEAD0200);
      while(true){}
    } else if (entry_0) {
      entry_1 = val;
    } else {
      entry_0 = val;
    }
  }
  uint32_t pop() {
    uint32_t ret_val = entry_0;
    entry_0 = entry_1;
    entry_1 = 0;
    return ret_val;
  }
  bool full() {
    return entry_1;
  }
} quick_q_t;

#pragma pack(push)
#pragma pack(4)

typedef struct dram_scatter_loop_t {

  uint32_t num_index_back;
  uint32_t num_iter;
  uint32_t inc;
  uint32_t unused0;

} dram_scatter_loop_t;

typedef struct dram_q_state_t {
  
  volatile uint32_t tt_l1_ptr * l1_dram_ptrs;
  
  uint8_t  dram_q_state_flags;
  uint8_t  l1_dram_incoming_ptr_index;
  uint16_t dram_ptr_issued_q_slots;
  uint16_t dram_ptr_flushed_q_slots;
  uint16_t dram_ptr_incoming_q_slots;
  uint16_t dram_buf_size_q_slots;
  uint8_t dram_q_stream_id;
  uint8_t temp0; // Used to initialize write_stride during init, can be reused during loop
  uint32_t dram_num_partial_q_slot_issued_tiles;
  uint32_t flushed_partial_q_slot_tiles;

  uint32_t dram_ptr_issued_byte;
  uint32_t scatter_offsets_index;
  dram_q_state_t tt_l1_ptr * next;
  
} dram_q_state_t;


typedef struct dram_input_stream_state_t {
  
  uint8_t  stream_id;
  uint8_t  input_noc;
  uint8_t  q_ptr_update_pending;
  uint8_t  dram_read_opt_enabled;

  uint16_t in_flight_data_rec_chunks;
  uint16_t min_dram_io_entries;

  uint32_t stream_buf_addr_byte;
  uint32_t stream_flushed_ptr_byte;
  
  uint8_t  moves_raw_data;
  uint8_t  stream_buf_available;
  uint8_t  active_stream_id;
  uint8_t  io_q_empty;
  uint32_t in_flight_bytes;
  
  uint32_t epoch_q_slots_remaining;

  uint32_t scatter_list_input_index;
  uint16_t c_dim_size;
  uint16_t c_dim_count;
  uint32_t col_offset_bytes;
  uint32_t scatter_loop_index;
  uint32_t scatter_loop_inc;

  uint8_t transaction_id_issued;
  uint8_t transaction_id_flushed;
  uint16_t unused0;

  uint32_t prev_rd_data_word_recv;

  uint8_t fork_stream_ids[EPOCH_MAX_OUTPUT_FORKS];

  dram_q_state_t tt_l1_ptr * next_dram_q_issue;
  dram_q_state_t tt_l1_ptr * next_dram_q_in_flight;
  volatile epoch_stream_info_t tt_l1_ptr * stream_info;

} dram_input_stream_state_t;



typedef struct dram_output_stream_state_t {
  
  uint8_t  stream_id;
  uint8_t  in_flight_q_slots;  
  uint8_t  moves_raw_data;
  uint8_t  dram_q_available;

  uint16_t epoch_id;
  uint16_t tiles_to_clear;
  uint16_t in_flight_tiles;
  uint16_t curr_phase_tiles_sent;

  uint32_t stream_rd_ptr_byte;
  uint32_t stream_buf_size_bytes;
  uint32_t stream_buf_addr_byte;

  uint32_t epoch_q_slots_remaining;

  uint16_t write_stride;
  uint16_t total_write_strides;

  uint32_t skip_col_bytes;
  uint32_t skip_col_tile_row_bytes;
  uint32_t skip_col_row_bytes;
  uint32_t skip_zcol_bytes;
  uint32_t skip_col_zrow_bytes;
  union {
    quick_q_t stream_sending_q;
    struct {
      uint32_t prev_row_ptr_bytes;
      uint32_t prev_zc_ptr_bytes;
    };
  };
  union {
    struct {
      uint8_t  in_flight_q_slots_2;
      uint8_t  unused0;  
      uint16_t in_flight_tiles_2;
    };
    uint32_t prev_zr_ptr_bytes;
  };
  union {
    uint32_t dram_stream_write_start_time;
    uint32_t prev_batch_ptr_bytes;
  };
  uint16_t c_dim_size;
  uint16_t c_dim_count;
  union {
    struct {
      uint16_t r_dim_size;
      uint16_t r_dim_count;
    };
    uint32_t scatter_list_input_index;
  };
  union {
    struct {
      uint16_t zc_dim_size;
      uint16_t zc_dim_count;
    };
    uint32_t col_offset_bytes;
  };
  uint16_t zr_dim_size;
  uint16_t zr_dim_count;
  uint32_t q_slot_size_tiles; 
  
  dram_q_state_t tt_l1_ptr * next_dram_q_issue;
  volatile epoch_stream_info_t tt_l1_ptr * stream_info;

} dram_output_stream_state_t;

// make sure all these fit into nrisc local mem:
static_assert(
  sizeof(dram_input_stream_state_t) == 
  EXPECTED_DRAM_INPUT_STREAM_STATE_T_STRUCT_SIZE_WITHOUT_FORKS + EPOCH_MAX_OUTPUT_FORKS);
static_assert(sizeof(dram_output_stream_state_t) == EXPECTED_DRAM_OUTPUT_STREAM_STATE_T_STRUCT_SIZE);
static_assert(sizeof(dram_q_state_t) == EXPECTED_DRAM_Q_STATE_T_STRUCT_SIZE);

// total local mem size = 4KB:
//    stack = 1KB
//    ncrisc_streams.h structures = 2.7KB
//    remaining for misc. variables = 0.3KB
//    No usable bytes remain (ncrisc w/ perf dump)
static_assert(((sizeof(dram_input_stream_state_t) * MAX_DRAM_INPUT_STREAMS) +
               (sizeof(dram_output_stream_state_t) * MAX_DRAM_OUTPUT_STREAMS) +
               (sizeof(dram_q_state_t) * MAX_L0_DRAM_Q_STATE_T)) <= MAX_NCRISC_STRUCTS_SIZE);

#pragma pack(pop)

// untilize_copy is assembly-level optimzied, it might look odd, but produces the best code
template <int NOC_ID>
void untilize_copy(dram_q_state_t tt_l1_ptr * next_dram_q_issue, uint64_t dram_buf_addr, uint32_t stream_buf_addr, uint32_t stream_rd_ptr_byte,
                    uint32_t data_send_chunk_size_bytes, dram_output_stream_state_t* curr_dram_output_stream_state, uint32_t* dram_wrptr_byte_ptr, uint32_t untilize_copy_iters, bool single_row_copy) {
  asm volatile ("" ::: "memory");
  uint32_t cmd_buf_a_ctrl;
  uint32_t cmd_buf_b_ctrl;
  uint32_t dram_dest_addr;
  uint32_t stream_rd_addr;
  uint32_t skip_col_bytes_lw = curr_dram_output_stream_state->skip_col_bytes;
  uint32_t dram_wrptr_byte = next_dram_q_issue->dram_ptr_issued_byte;      

  cmd_buf_a_ctrl = NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_0, NOC_CMD_CTRL);
  dram_dest_addr = ((uint32_t)dram_buf_addr) + dram_wrptr_byte;
  if ((next_dram_q_issue->dram_q_state_flags & DRAM_Q_STREAMING_FLAG) == 0) {
    dram_dest_addr += DRAM_BUF_DATA_OFFSET;
  }
  stream_rd_addr = stream_buf_addr + stream_rd_ptr_byte;

  if (single_row_copy) {
    while(NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_0, NOC_CMD_CTRL));
    ncrisc_noc_blitz_write(NOC_ID, NCRISC_WR_CMD_BUF_0, stream_rd_addr, dram_dest_addr);
  } else {
    for (uint32_t iter = 0; iter < untilize_copy_iters; iter++) {
      if (__builtin_expect (!cmd_buf_a_ctrl, 1)) {
        cmd_buf_b_ctrl = NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_1, NOC_CMD_CTRL);

        ncrisc_noc_blitz_write(NOC_ID, NCRISC_WR_CMD_BUF_0, stream_rd_addr, dram_dest_addr);

        dram_dest_addr += skip_col_bytes_lw;
        stream_rd_addr += data_send_chunk_size_bytes;
      } else {
        while(NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_0, NOC_CMD_CTRL));

        cmd_buf_b_ctrl = NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_1, NOC_CMD_CTRL);

        ncrisc_noc_blitz_write(NOC_ID, NCRISC_WR_CMD_BUF_0, stream_rd_addr, dram_dest_addr);

        dram_dest_addr += skip_col_bytes_lw;
        stream_rd_addr += data_send_chunk_size_bytes;
      }

      if (__builtin_expect (!cmd_buf_b_ctrl, 1)) {
        cmd_buf_a_ctrl = NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_0, NOC_CMD_CTRL);

        ncrisc_noc_blitz_write(NOC_ID, NCRISC_WR_CMD_BUF_1, stream_rd_addr, dram_dest_addr);

        dram_dest_addr += skip_col_bytes_lw;
        stream_rd_addr += data_send_chunk_size_bytes;
      } else {
        while (NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_1, NOC_CMD_CTRL));

        cmd_buf_a_ctrl = NOC_CMD_BUF_READ_REG(NOC_ID, NCRISC_WR_CMD_BUF_0, NOC_CMD_CTRL);

        ncrisc_noc_blitz_write(NOC_ID, NCRISC_WR_CMD_BUF_1, stream_rd_addr, dram_dest_addr);

        dram_dest_addr += skip_col_bytes_lw;
        stream_rd_addr += data_send_chunk_size_bytes;
      } 
    }
  }

  *dram_wrptr_byte_ptr = dram_wrptr_byte;
}

inline bool stride_iter_matches(volatile dram_io_state_t tt_l1_ptr * l1_ptrs, uint32_t& rd_stride, uint32_t& curr_stride_wrap, uint32_t& next_stride_wrap) {
  uint32_t vanilla_stride_wrap = l1_ptrs->local.stride_wrap;
  rd_stride = l1_ptrs->dram_to_l1.rd_stride;
  curr_stride_wrap = rd_stride & DRAM_STRIDE_WRAP_BIT;
  uint32_t stride_wrap = vanilla_stride_wrap ? DRAM_STRIDE_WRAP_BIT : 0;
  bool stride_iter_matches_expr = !(stride_wrap ^ curr_stride_wrap);
  rd_stride = rd_stride & ~DRAM_STRIDE_WRAP_BIT;
  next_stride_wrap = !vanilla_stride_wrap;
  return stride_iter_matches_expr;
}

inline __attribute__((always_inline)) bool can_do_another_read(uint32_t input_noc, uint32_t data_rec_chunk_size_bytes, uint32_t prev_rd_data_word_recv) {
  if (prev_rd_data_word_recv == 0)
    return true;
  uint32_t cur_rd_data_word_recv = ncrisc_rd_data_word_recv(input_noc);
  uint32_t words_recv = cur_rd_data_word_recv - prev_rd_data_word_recv;
  uint32_t data_rec_chunk_size_words = data_rec_chunk_size_bytes/NOC_WORD_BYTES;
  uint32_t data_rec_chunk_size_words_thresh = data_rec_chunk_size_words > READ_WORDS_THRESH ? data_rec_chunk_size_words - READ_WORDS_THRESH : 0;
  if (words_recv >= data_rec_chunk_size_words_thresh)
    return true;
  else
    return false;
}

inline __attribute__((always_inline)) bool get_scatter_offsets(dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state, volatile tt_uint64_t tt_l1_ptr * &scatter_offsets, uint32_t scatter_list_stream_id) {
  if (scatter_list_stream_id) {
    uint32_t phase_active = stream_phase_is_active(scatter_list_stream_id);
    int32_t tiles_available = stream_tiles_outstanding(scatter_list_stream_id);
    if (!phase_active || !tiles_available)
      return false;
    scatter_offsets = ((volatile tt_uint64_t tt_l1_ptr *)(stream_phase_next_recved_tile_addr(scatter_list_stream_id)*MEM_WORD_WIDTH + DRAM_SCATTER_LIST_START_OFFSET));
    return true;
  } else {
    scatter_offsets = dram_io_scatter_state->scatter_offsets;
    return true;
  }
}

void clear_scatter_offsets(uint32_t& scatter_list_input_index_saved, dram_q_state_t tt_l1_ptr * next_dram_q_issue, dram_io_scatter_state_t tt_l1_ptr * dram_io_scatter_state, 
                           epoch_stream_dram_io_info_t tt_l1_ptr * dram_io_info,
                           uint32_t dram_io_scatter_size, uint32_t& scatter_idx, uint32_t scatter_list_stream_id);
void __attribute__ ((noinline)) dram_output_stream_issue_scatter_write_indicies(uint32_t output_noc, uint32_t output_vc, uint32_t dram_data_buf_wr_offset_byte, uint32_t data_send_chunk_size_tiles, uint32_t dram_io_scatter_chunk_size_tiles,
                                                                               uint32_t dram_io_scatter_chunk_size_bytes, uint32_t stream_src_addr, volatile tt_uint64_t tt_l1_ptr * scatter_offsets, uint32_t& scatter_idx,
                                                                               uint64_t dram_addr, uint32_t dram_embeddings_row_shift, uint32_t& c_dim_count, uint32_t c_dim_size, uint32_t c_dim_loop_num_rows, uint32_t& col_offset_bytes, uint32_t transaction_id,
                                                                               uint32_t stream_msg_info_buf_addr, uint32_t dram_writes_with_cmd_buf, uint32_t dram_stream);
bool __attribute__((section("code_l1"))) get_stream_push_flushed_l1(dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t stream_id, uint32_t data_rec_chunk_size_bytes);
void __attribute__((section("code_l1"))) push_readdata_to_stream_l1(dram_input_stream_state_t* curr_dram_input_stream_state, uint32_t stream_id, bool moves_raw_data,
                                                                    uint32_t prev_data_rec_chunk_size_tiles, uint32_t prev_data_rec_chunk_size_bytes);
void __attribute__((section("code_l1"))) __attribute__((noinline)) process_dram_streaming_read_l1(
  void * pFunction, uint32_t& stream_id, uint32_t& phase_active, uint32_t& curr_phase_tiles_remaining_to_issue, 
  dram_input_stream_state_t* curr_dram_input_stream_state, dram_q_state_t tt_l1_ptr * next_dram_q_issue,
  uint32_t &epoch_q_slots_remaining);                                                                    

void update_dram_write_ptrs(bool is_ram, bool is_strided_write, uint32_t write_stride, uint32_t total_write_strides, uint32_t dram_wrptr_q_slots, uint32_t output_noc, uint32_t output_vc, 
                            uint64_t dram_buf_addr, dram_output_stream_state_t* curr_dram_output_stream_state, volatile dram_io_state_t tt_l1_ptr * l1_ptrs, uint32_t curr_stride_wrap, uint32_t next_stride_wrap);
void check_whether_poll_immediately(uint32_t& dram_ptr_update_cnt);
void set_dont_poll_immediately();
#if defined(PERF_DUMP) || defined(INTERMED_DUMP) || defined(DRAM_DECOUPLE) || defined(PERF_DUMP_CONCURRENT)
void __attribute__((section("code_l1"))) __attribute__((noinline)) poll_dram_ptrs(void * pFunction, uint32_t &num_active_dram_queues, dram_q_state_t tt_l1_ptr *dram_q_state, bool& check_ptrs_flushed, bool& all_ptrs_flushed);
#else
void poll_dram_ptrs(void * pFunction, uint32_t &num_active_dram_queues, dram_q_state_t tt_l1_ptr *dram_q_state, bool& check_ptrs_flushed, bool& all_ptrs_flushed);
#endif
#ifdef DRAM_DECOUPLE
void risc_decoupled_stream_buffer_init_l1(uint32_t stream_id);
void risc_decoupled_dram_buffer_init_l1(volatile dram_io_state_t tt_l1_ptr * dram_io_state, uint32_t noc, uint32_t msg_info_buf_start);
#endif

#ifdef DRAM_DECOUPLE
void __attribute__((section("code_l1"))) risc_decoupled_stream_buffer_init_l1(uint32_t stream_id);
#endif
void __attribute__((section("code_l1"))) __attribute__((noinline)) risc_dram_stream_handler_init_l1(
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
);
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
);
void __attribute__((section("code_l1"))) __attribute__((noinline)) risc_dram_stream_handler_epilogue_l1(
  void * pFunction,
#ifdef RISC_GSYNC_ENABLED
  volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
  uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
  dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t tt_l1_ptr *active_stream_info,
  epoch_stream_info_t tt_l1_ptr * tt_l1_ptr * dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info
);

#endif

