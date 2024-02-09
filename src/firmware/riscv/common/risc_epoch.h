// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _RISC_EPOCH_H_
#define _RISC_EPOCH_H_

#include <stdint.h>

#include "risc_common.h"
#include "noc_parameters.h"
#include "noc_overlay_parameters.h"
#include "noc_wrappers.h"
#include "epoch.h"
#include "risc.h"
#include "stream_interface.h"
#include "risc_chip_specific.h"
#include "tensix.h"
#include "epoch_q.h"
#include "dram_stream_intf.h"
#include "dram_address_map.h"
#include "l1_address_map.h"
#ifdef PERF_DUMP
#include "risc_perf.h"
#endif

#define SCRATCH_PAD_DRAM_READ_IDX 0
#define SCRATCH_PAD_DRAM_WRITE_IDX 16

#define MAX_DRAM_QUEUES_TO_UPDATE 256

#define EPOCH_EMPTY_CHECK_MASK ((0x1 << 12) - 1)

// FW loop info structure, 16b fields should be enough
struct LoopInfo {
    uint32_t curr_iter = 0;
    uint32_t last_iter = 0;  // loop_count-1, so we fit power-of-2 numbers
    uint32_t epoch_ptr = 0;
};

// FW loop stack structure
template <int STACK_SIZE=4>
struct LoopStack {
    LoopInfo table[STACK_SIZE];
    int ptr = -1;
    LoopInfo* top() {
        if (ptr < 0) return nullptr;
        return &table[ptr];
    }
    void push(const LoopInfo &info) {
        ptr++;
        table[ptr] = info;
        // if (ptr >= STACK_SIZE) throw std::runtime_error("LoopStack overflow");
    }
    void pop() {
        ptr--;
    }

    bool is_empty() {
      return ptr < 0 ? true : false;
    }
};

inline void risc_l1_epoch_q_reset() {
  RISC_L1_EPOCH_Q_PTR->rd_ptr = 0;
  RISC_L1_EPOCH_Q_PTR->wr_ptr = 0;
}

inline uint32_t risc_get_l1_epoch_q_rdptr() {
  return RISC_L1_EPOCH_Q_PTR->rd_ptr;
}

inline uint32_t risc_get_l1_epoch_q_wrptr() {
  return RISC_L1_EPOCH_Q_PTR->wr_ptr;
}

inline void risc_l1_epoch_q_rdptr_update(uint32_t rd_ptr) {
  RISC_L1_EPOCH_Q_PTR->rd_ptr = rd_ptr;
}

inline bool risc_is_l1_epoch_q_empty(uint32_t &my_q_rd_ptr) {
  bool is_empty = my_q_rd_ptr == RISC_L1_EPOCH_Q_PTR->wr_ptr;
  return is_empty;
}
// Default parameters for epoch command queue. These are used during a regular compile.
// Can be overwritten during test runtime by recompiling NCRISC. 
#ifndef EPOCH_QUEUE_NUM_SLOTS
  #define EPOCH_QUEUE_NUM_SLOTS epoch_queue::EPOCH_Q_NUM_SLOTS
#endif

#ifndef EPOCH_TABLE_DRAM_ADDRESS
  #define EPOCH_TABLE_DRAM_ADDRESS epoch_queue::EPOCH_TABLE_DRAM_ADDR
#endif

#ifndef EPOCH_ENTRY_SIZE_BYTES
  #define EPOCH_ENTRY_SIZE_BYTES epoch_queue::EPOCH_TABLE_ENTRY_SIZE_BYTES
#endif

#ifndef EPOCH_ALLOC_QUEUE_SYNC_ADDRESS
  #define EPOCH_ALLOC_QUEUE_SYNC_ADDRESS epoch_queue::EPOCH_ALLOC_QUEUE_SYNC_ADDR
#endif

// Default parameters for epoch binary layout. These are used during a regular compile, and
// can be overwritten during test runtime by recompiling NCRISC (Kernel Cache changes layout)
#ifndef DRAM_EPOCH_RUNTIME_CONFIG_BASE
  #define DRAM_EPOCH_RUNTIME_CONFIG_BASE dram_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE_DEFAULT
#endif

#ifndef DRAM_OVERLAY_BLOB_BASE
  #define DRAM_OVERLAY_BLOB_BASE dram_mem::address_map::OVERLAY_BLOB_BASE_DEFAULT
#endif

inline void risc_epoch_q_rdptr_update(uint32_t rd_ptr, volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset) {
  uint32_t noc_write_dest_buf_offset = my_q_table_offset % (NOC_WORD_BYTES);
  uint32_t noc_write_dest_buf_addr = (uint32_t)(&(noc_read_scratch_buf[SCRATCH_PAD_DRAM_WRITE_IDX]));
  uint32_t noc_write_dest_buf_ptr_addr = noc_write_dest_buf_addr+noc_write_dest_buf_offset;
  volatile uint32_t *noc_write_dest_buf_ptr = (volatile uint32_t *)(noc_write_dest_buf_ptr_addr + epoch_queue::EPOCH_Q_RDPTR_OFFSET); 
  *noc_write_dest_buf_ptr = rd_ptr;
  uint64_t q_rd_ptr_addr = my_q_table_offset + epoch_queue::EPOCH_Q_RDPTR_OFFSET;
  RISC_POST_DEBUG(0x10000009);
  RISC_POST_DEBUG(rd_ptr);
  RISC_POST_DEBUG(noc_write_dest_buf_ptr_addr);
  RISC_POST_DEBUG(q_rd_ptr_addr >> 32);
  RISC_POST_DEBUG(q_rd_ptr_addr & 0xFFFFFFFF);
  // Reg poll loop, flushed immediately
  while (!ncrisc_noc_fast_write_ok(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
  ncrisc_noc_fast_write(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, noc_write_dest_buf_ptr_addr, q_rd_ptr_addr, 4,
                        DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
}

inline uint64_t risc_get_epoch_q_dram_ptr(uint32_t my_x, uint32_t my_y) {
  my_x = get_epoch_index_x(my_x);
  my_y = get_epoch_index_y(my_y);
  const uint64_t INITIAL_EPOCH_VECTOR_TABLE_ADDR = NOC_XY_ADDR(NOC_X(get_epoch_table_x(my_x, my_y)), NOC_Y(get_epoch_table_y(my_x, my_y)), EPOCH_TABLE_DRAM_ADDRESS);
  uint64_t q_table_offset = INITIAL_EPOCH_VECTOR_TABLE_ADDR + ((my_y * epoch_queue::GridSizeCol) + my_x) * EPOCH_ENTRY_SIZE_BYTES;
  return q_table_offset;
}

inline void risc_epoch_q_get_ptr(volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset, uint32_t &my_q_rd_ptr, uint32_t &my_q_wr_ptr) {

  uint32_t noc_read_dest_buf_offset = my_q_table_offset % (NOC_WORD_BYTES);
  uint32_t noc_read_dest_buf_addr = (uint32_t)(noc_read_scratch_buf);
  uint32_t noc_read_dest_buf_ptr_addr = noc_read_dest_buf_addr+noc_read_dest_buf_offset;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF,
                               my_q_table_offset,
                               noc_read_dest_buf_ptr_addr,
                               8, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));
  volatile uint32_t *noc_read_dest_buf_ptr = (volatile uint32_t *)(noc_read_dest_buf_ptr_addr); 
  my_q_rd_ptr = noc_read_dest_buf_ptr[0];
  my_q_wr_ptr = noc_read_dest_buf_ptr[1];
}

inline bool risc_is_epoch_q_empty(volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset, uint32_t &my_q_rd_ptr, uint32_t &my_q_wr_ptr, uint32_t &epoch_empty_check_cnt) {

  bool is_empty;

  if (dram_io_empty(my_q_rd_ptr, my_q_wr_ptr)) {
    if (risc_get_l1_epoch_q_wrptr() == epoch_queue::POLLING_EPOCH_QUEUE_TAG ? epoch_empty_check_cnt == 0 : !risc_is_l1_epoch_q_empty(my_q_rd_ptr)) {
      uint32_t noc_read_dest_buf_offset = my_q_table_offset % (NOC_WORD_BYTES);
      uint32_t noc_read_dest_buf_addr = (uint32_t)(noc_read_scratch_buf);
      uint32_t noc_read_dest_buf_ptr_addr = noc_read_dest_buf_addr+noc_read_dest_buf_offset;
      ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF,
                                  my_q_table_offset,
                                  noc_read_dest_buf_ptr_addr,
                                  8, NCRISC_RD_DEF_TRID);
      while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));
      volatile uint32_t *noc_read_dest_buf_ptr = (volatile uint32_t *)(noc_read_dest_buf_ptr_addr); 
      my_q_wr_ptr = noc_read_dest_buf_ptr[1];
      is_empty = dram_io_empty(my_q_rd_ptr, my_q_wr_ptr);
    } else {
      is_empty = true;
    }
  } else {
    is_empty = false;
  }

  epoch_empty_check_cnt = (epoch_empty_check_cnt + 1) & EPOCH_EMPTY_CHECK_MASK;

  return is_empty;
}

// CMD in DRAM has 1 address
inline void risc_get_noc_addr_from_dram_ptr(volatile uint32_t tt_l1_ptr *noc_read_dest_buf_ptr, uint64_t& dram_addr_offset, uint32_t& dram_coord_x, uint32_t& dram_coord_y) {
  uint64_t dram_addr_offset_lo = noc_read_dest_buf_ptr[0];
  uint64_t dram_addr_offset_hi = noc_read_dest_buf_ptr[1] & 0xFFFF;
  dram_addr_offset = dram_addr_offset_lo | (dram_addr_offset_hi << 32);
  dram_coord_x = (noc_read_dest_buf_ptr[1] >> 16) & 0x3F;
  dram_coord_y = (noc_read_dest_buf_ptr[1] >> 22) & 0x3F;
}

#ifdef KERNEL_CACHE_ENA
// CMD in DRAM has 2 addresses
inline void risc_get_noc_addrs_from_dram_ptr(volatile uint32_t tt_l1_ptr *noc_read_dest_buf_ptr, uint64_t& dram_addr0_offset, uint64_t& dram_addr1_offset, uint32_t& dram_coord_x, uint32_t& dram_coord_y) {
  uint64_t dram_addr0_offset_lo = noc_read_dest_buf_ptr[0];
  uint64_t dram_addr0_offset_hi = noc_read_dest_buf_ptr[1] & 0xFFFF;
  dram_addr0_offset = dram_addr0_offset_lo | (dram_addr0_offset_hi << 32);
  uint64_t dram_addr1_offset_lo = noc_read_dest_buf_ptr[3];
  uint64_t dram_addr1_offset_hi = noc_read_dest_buf_ptr[4] & 0xFFFF;
  dram_addr1_offset = dram_addr1_offset_lo | (dram_addr1_offset_hi << 32);
  dram_coord_x = (noc_read_dest_buf_ptr[1] >> 16) & 0x3F;
  dram_coord_y = (noc_read_dest_buf_ptr[1] >> 22) & 0x3F;
}
#endif

inline __attribute__((section("code_l1"))) void risc_get_noc_addr_from_dram_ptr_l1(volatile uint32_t *noc_read_dest_buf_ptr, uint64_t& dram_addr_offset, uint32_t& dram_coord_x, uint32_t& dram_coord_y) {
  uint64_t dram_addr_offset_lo = noc_read_dest_buf_ptr[0];
  uint64_t dram_addr_offset_hi = noc_read_dest_buf_ptr[1] & 0xFFFF;
  dram_addr_offset = dram_addr_offset_lo | (dram_addr_offset_hi << 32);
  dram_coord_x = (noc_read_dest_buf_ptr[1] >> 16) & 0x3F;
  dram_coord_y = (noc_read_dest_buf_ptr[1] >> 22) & 0x3F;
}

inline __attribute__((section("code_l1"))) void risc_get_noc_addr_from_dram_ptr_l1_with_l1_ptr(volatile uint32_t tt_l1_ptr *noc_read_dest_buf_ptr, uint64_t& dram_addr_offset, uint32_t& dram_coord_x, uint32_t& dram_coord_y) {
  uint64_t dram_addr_offset_lo = noc_read_dest_buf_ptr[0];
  uint64_t dram_addr_offset_hi = noc_read_dest_buf_ptr[1] & 0xFFFF;
  dram_addr_offset = dram_addr_offset_lo | (dram_addr_offset_hi << 32);
  dram_coord_x = (noc_read_dest_buf_ptr[1] >> 16) & 0x3F;
  dram_coord_y = (noc_read_dest_buf_ptr[1] >> 22) & 0x3F;
}

inline void risc_get_epoch_dram_ptrs(uint32_t &epoch_command, uint32_t &dram_decouple_mask, volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset, uint32_t &my_q_rd_ptr, uint32_t &num_loops, uint64_t &dram_next_epoch_ptr, uint64_t &dram_next_epoch_triscs_ptr) {

  uint64_t my_q_entry_offset = (my_q_rd_ptr & (EPOCH_QUEUE_NUM_SLOTS-1)) * epoch_queue::EPOCH_Q_SLOT_SIZE + epoch_queue::EPOCH_Q_SLOTS_OFFSET + my_q_table_offset;
  uint32_t noc_read_dest_buf_offset = my_q_entry_offset % (NOC_WORD_BYTES);
  uint32_t noc_read_dest_buf_addr = (uint32_t)(noc_read_scratch_buf);
  uint32_t noc_read_dest_buf_ptr_addr = noc_read_dest_buf_addr+noc_read_dest_buf_offset;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF,
                               my_q_entry_offset,
                               noc_read_dest_buf_ptr_addr,
                               32, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));
  volatile uint32_t *noc_read_dest_buf_ptr = (uint32_t *)(noc_read_dest_buf_ptr_addr); 
  uint64_t dram_addr_offset_combined; // Combined overlay+trisc+etc binaries addr
  uint64_t dram_addr_offset_trisc;    // trisc-only binaries addr (to save space)
  uint32_t dram_coord_x;
  uint32_t dram_coord_y;

#ifdef KERNEL_CACHE_ENA
  // Trisc binary cache is seperate address if enabled, otherwise trisc binaries are in combined epoch binary.
  risc_get_noc_addrs_from_dram_ptr(noc_read_dest_buf_ptr, dram_addr_offset_combined, dram_addr_offset_trisc, dram_coord_x, dram_coord_y);
  dram_next_epoch_ptr         = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset_combined);
  dram_next_epoch_triscs_ptr  = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset_trisc);
#else
  risc_get_noc_addr_from_dram_ptr(noc_read_dest_buf_ptr, dram_addr_offset_combined, dram_coord_x, dram_coord_y);
  dram_next_epoch_ptr         = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset_combined);
  dram_next_epoch_triscs_ptr  = dram_next_epoch_ptr;
#endif

  epoch_command = (noc_read_dest_buf_ptr[1] >> 28) & 0xF;
  num_loops = (epoch_command == epoch_queue::EpochCmdLoopStart) ? noc_read_dest_buf_ptr[1] & 0xFFFFFFF : 1;
#ifdef PERF_DUMP
  risc::epoch_perf_en = noc_read_dest_buf_ptr[2] & 0xff;
#if OVERLAY_DECOUPLE == 1
  uint8_t input_decouple_mask = (noc_read_dest_buf_ptr[2] >> 8) & 0xff;
  uint8_t output_decouple_mask = (noc_read_dest_buf_ptr[2] >> 16) & 0xff;
  PERF_RISC_MAILBOX_INPUT_DECOUPLE_MASK_PTR[0] = input_decouple_mask;
  PERF_RISC_MAILBOX_OUTPUT_DECOUPLE_MASK_PTR[0] = output_decouple_mask;
#else
  PERF_RISC_MAILBOX_INPUT_DECOUPLE_MASK_PTR[0] = 0;
  PERF_RISC_MAILBOX_OUTPUT_DECOUPLE_MASK_PTR[0] = 0;
#endif
#endif
}

inline __attribute__((section("code_l1"))) void risc_get_epoch_update_info(epoch_queue::IOQueueUpdateCmdInfo &queue_update_info, volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset, uint32_t &my_q_rd_ptr, uint64_t dram_next_epoch_ptr) {

  volatile epoch_queue::IOQueueUpdateCmdInfo* update_info;

  uint64_t my_q_entry_offset = (my_q_rd_ptr & (EPOCH_QUEUE_NUM_SLOTS-1)) * epoch_queue::EPOCH_Q_SLOT_SIZE + epoch_queue::EPOCH_Q_SLOTS_OFFSET + my_q_table_offset;
  uint32_t noc_read_dest_buf_offset = my_q_entry_offset % (NOC_WORD_BYTES);
  uint32_t noc_read_dest_buf_addr = (uint32_t)(noc_read_scratch_buf);
  uint32_t noc_read_dest_buf_ptr_addr = noc_read_dest_buf_addr+noc_read_dest_buf_offset;

  update_info = (volatile epoch_queue::IOQueueUpdateCmdInfo*)noc_read_dest_buf_ptr_addr;

  queue_update_info.queue_header_addr = update_info->queue_header_addr;
  queue_update_info.num_buffers = update_info->num_buffers;
  queue_update_info.reader_index = update_info->reader_index;
  queue_update_info.num_readers = update_info->num_readers;
  queue_update_info.update_mask = update_info->update_mask;
  queue_update_info.header[0] = update_info->header[0];
  queue_update_info.header[1] = update_info->header[1];
  queue_update_info.header[2] = update_info->header[2];
  queue_update_info.header[3] = update_info->header[3];
  queue_update_info.header[4] = update_info->header[4];
}

inline __attribute__((section("code_l1"))) void risc_get_epoch_varinst_info(epoch_queue::VarinstCmdInfo &varinst_cmd_info, volatile uint32_t *noc_read_scratch_buf, uint64_t &my_q_table_offset, uint32_t &my_q_rd_ptr, uint64_t dram_next_epoch_ptr) {

  volatile epoch_queue::VarinstCmdInfo* varinst_info;

  uint64_t my_q_entry_offset = (my_q_rd_ptr & (EPOCH_QUEUE_NUM_SLOTS-1)) * epoch_queue::EPOCH_Q_SLOT_SIZE + epoch_queue::EPOCH_Q_SLOTS_OFFSET + my_q_table_offset;
  uint32_t noc_read_dest_buf_offset = my_q_entry_offset % (NOC_WORD_BYTES);
  uint32_t noc_read_dest_buf_addr = (uint32_t)(noc_read_scratch_buf);
  uint32_t noc_read_dest_buf_ptr_addr = noc_read_dest_buf_addr+noc_read_dest_buf_offset;

  varinst_info = (volatile epoch_queue::VarinstCmdInfo*)noc_read_dest_buf_ptr_addr;

  varinst_cmd_info.dram_addr_lower = varinst_info->dram_addr_lower;
  varinst_cmd_info.dram_addr_upper = varinst_info->dram_addr_upper;
  varinst_cmd_info.dram_core_x = varinst_info->dram_core_x;
  varinst_cmd_info.dram_core_y = varinst_info->dram_core_y;
  varinst_cmd_info.cmd_type = varinst_info->cmd_type;
  varinst_cmd_info.num_buffers = varinst_info->num_buffers;
  varinst_cmd_info.reader_index = varinst_info->reader_index;
  varinst_cmd_info.num_readers = varinst_info->num_readers;
  varinst_cmd_info.update_mask = varinst_info->update_mask;
  varinst_cmd_info.opcode = varinst_info->opcode;
  varinst_cmd_info.field_size_bytes = varinst_info->field_size_bytes;
  varinst_cmd_info.operand_0 = varinst_info->operand_0;
  varinst_cmd_info.operand_1 = varinst_info->operand_1;
  varinst_cmd_info.sync_loc_enable = varinst_info->sync_loc_enable;
  varinst_cmd_info.sync_loc_dram_core_x = varinst_info->sync_loc_dram_core_x;
  varinst_cmd_info.sync_loc_dram_core_y = varinst_info->sync_loc_dram_core_y;
  varinst_cmd_info.sync_loc_index = varinst_info->sync_loc_index;

}

#ifdef ERISC
void __attribute__((section("code_l1"))) __attribute__((no_inline)) run_epoch(
#else
void run_epoch(
#endif
    void (*risc_epoch_load)(uint64_t), void (*risc_kernels_load)(uint64_t), void (*risc_extra_overlay_blob_load)(uint64_t, uint32_t), void (*init_ncrisc_streams)(void *, uint32_t),
    bool skip_initial_epoch_dram_load, uint64_t dram_next_epoch_ptr, uint64_t dram_next_epoch_triscs_ptr, bool& skip_kernels,
#ifdef RISC_GSYNC_ENABLED
    volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
#ifdef ERISC
    volatile uint32_t &epoch_id_to_send, volatile uint32_t &other_chip_epoch_id,
    uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
    uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state,
#endif
    uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
    dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t tt_l1_ptr *active_stream_info,
    epoch_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info, uint32_t dram_decouple_mask
);

void __attribute__((section("code_l1"))) __attribute__((noinline)) run_dram_queue_update(
    void * pFunction, volatile uint32_t *noc_read_scratch_buf, uint64_t& my_q_table_offset, uint32_t& my_q_rd_ptr, uint64_t& dram_next_epoch_ptr, uint8_t& loading_noc, bool &varinst_cmd, uint32_t &loop_iteration
);

void __attribute__((section("code_l1"))) run_varinst_cmd_dram_rmw(
    epoch_queue::VarinstCmdInfo &varinst_cmd_info, uint64_t &queue_addr_ptr, uint32_t header_addr, volatile uint32_t tt_l1_ptr *header_addr_ptr
);
#endif

