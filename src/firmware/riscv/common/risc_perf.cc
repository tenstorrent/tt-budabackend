// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "epoch.h"
#include "l1_address_map.h"
#include "host_mem_address_map.h"
#include "tensix.h"
#include "noc_nonblocking_api.h"
#include "risc_perf.h"
#include "risc_common.h"
#include "tt_log.h"
namespace risc
{
uint32_t perf_dram_initialized = 0;
uint8_t epoch_perf_en __attribute__((section(".bss"))) = 0;
uint32_t perf_index __attribute__((section(".bss"))) = 0;
uint32_t perf_end __attribute__((section(".bss"))) = 0;
uint32_t wall_clk_h __attribute__((section(".bss"))) = 0;
volatile uint32_t tt_l1_ptr *perf_double_buf_base[2] = {nullptr, nullptr};
volatile uint32_t tt_l1_ptr *perf_buf_base;
uint64_t thread_dram_ptr[l1_mem::address_map::PERF_NUM_THREADS];
uint16_t thread_req_max[l1_mem::address_map::PERF_NUM_THREADS];
uint16_t thread_dram_copy_ack[l1_mem::address_map::PERF_NUM_THREADS];
uint8_t thread_l1_buf_sel[l1_mem::address_map::PERF_NUM_THREADS];
volatile uint32_t tt_l1_ptr epoch_perf_scratch[PERF_START_OFFSET] __attribute__((section("data_l1_noinit"))) __attribute__((aligned(32))) ;

#if PERF_DUMP_CONCURRENT == 1
bool pending_write = false;
uint32_t epoch_id = 0;
#endif

#if OVERLAY_DECOUPLE == 1
uint32_t overlay_decouple_epoch_idx = 0;
bool selected = false;
void wait_for_all_epoch_commands_sent_signal() {

  overlay_decouple_epoch_idx++;
  selected = PERF_ANALYZER_COMMAND_START_PTR[0] == 0x12345678;
  uint64_t epoch_command_start_addr = (uint64_t(PERF_ANALYZER_COMMAND_START_PTR[1]) << 32) | PERF_ANALYZER_COMMAND_START_PTR[0];
  uint32_t dst_addr = l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR;

  uint32_t epoch_command_pushed_idx = PERF_ANALYZER_COMMAND_START_VAL[0];

  if (selected) {
    while(epoch_command_pushed_idx != overlay_decouple_epoch_idx) {
      epoch_command_pushed_idx = PERF_ANALYZER_COMMAND_START_VAL[0];
    }
  } else {
    while (epoch_command_pushed_idx != overlay_decouple_epoch_idx) {
      while (!ncrisc_noc_fast_read_ok(PERF_DUMP_NOC, NCRISC_SMALL_TXN_CMD_BUF)) {};
      ncrisc_noc_fast_read_any_len_l1(PERF_DUMP_NOC, NCRISC_SMALL_TXN_CMD_BUF, epoch_command_start_addr, dst_addr, 4, NCRISC_RD_DEF_TRID);
      while (!ncrisc_noc_reads_flushed_l1(PERF_DUMP_NOC, NCRISC_RD_DEF_TRID)) {};

      epoch_command_pushed_idx = PERF_ANALYZER_COMMAND_START_VAL[0];
      // TT_LOG("overlay_decouple_epoch_idx = {}", overlay_decouple_epoch_idx);
      // TT_LOG("epoch_command_pushed_idx = {}", epoch_command_pushed_idx);
    }
  }
}
#endif

void init_perf_dram_state_first_epoch() {
  for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
    thread_dram_ptr[i] = tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[i]);
    thread_req_max[i] = EPOCH_INFO_PTR->perf_req_max[i]; 
    thread_dram_copy_ack[i] = 0;

    //Reset thread l1 half buffer select.
    thread_l1_buf_sel[i] = 0;
  }

  for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
    EPOCH_INFO_PTR->perf_dram_copy_req[i] = 0;
    EPOCH_INFO_PTR->perf_dram_copy_ack[i] = 0;
  }

  perf_dram_initialized = 1;
}

void check_host_dram_buffer_drain_signal_and_initialize() {
  if (PERF_DRAM_BUFFER_RESET_MAILBOX_PTR[0] == 0xffffffff) {
    init_perf_dram_state_first_epoch();
    PERF_DRAM_BUFFER_RESET_MAILBOX_PTR[0] = 0;
  } else {
    for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
      //When epoch starts, restore the req/ack conters as they do not reset across epochs.
      EPOCH_INFO_PTR->perf_dram_copy_req[i] = thread_dram_copy_ack[i];
      EPOCH_INFO_PTR->perf_dram_copy_ack[i] = thread_dram_copy_ack[i];
            //Reset thread l1 half buffer select.
      thread_l1_buf_sel[i] = 0;

    }
    //For risc, update new epoch's dram buf start address.
    //This address is used by risc at the end of epoch to spill epoch perf scratch buffer.
    //Set perf_dram_addr to 0 in dram buffer is already full from previous epochs.
    //NCRISC will use perf_dram_addr == 0 as a flag to not write epoch scratch buffer.
    if (thread_dram_copy_ack[3] < thread_req_max[3]) {
      EPOCH_INFO_PTR->perf_dram_addr[3].v = thread_dram_ptr[3];
    } else {
      EPOCH_INFO_PTR->perf_dram_addr[3].v = 0;
    }
  }
}

void init_perf_dram_state() {
  
  if (epoch_perf_en == 0) {
    return;
  }
  if (perf_dram_initialized == 0) {
    init_perf_dram_state_first_epoch();
  } else {
    check_host_dram_buffer_drain_signal_and_initialize();
  }
#if PERF_DUMP_CONCURRENT == 1
  uint32_t perf_trace_header_word = (thread_dram_ptr[1] >> 32) & 0xffffffff;    
  perf_trace_header_word = (perf_trace_header_word & 0x0000ffff) | ((epoch_id & 0x7ff) << 19) | ((uint32_t)0b011 << 16);
  volatile uint32_t tt_l1_ptr *perf_header = reinterpret_cast<volatile uint32_t tt_l1_ptr*>(l1_mem::address_map::PERF_THREAD_HEADER);
  perf_header[0] = perf_trace_header_word;

  perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
  perf_buf_base[1] = perf_header[0];
  
  epoch_id++;
#endif
}

void allocate_perf_buffer() {
#if PERF_DUMP_CONCURRENT && PERF_DUMP_LEVEL > 1
  perf_index = 2;
#else
  perf_index = PERF_START_OFFSET; // The first 4B value is always initialized to 0xbaddf00d. Also account for 6 fixed epoch related events.
#endif
  perf_double_buf_base[0] = reinterpret_cast<volatile uint32_t tt_l1_ptr *>(l1_mem::address_map::NCRISC_L1_PERF_BUF_BASE); // Start address of lower half buffer.
  perf_double_buf_base[1] = reinterpret_cast<volatile uint32_t tt_l1_ptr *>(l1_mem::address_map::NCRISC_L1_PERF_BUF_BASE + (NCRISC_PERF_BUF_SIZE >> 1)); //Start address of upper half buffer
  perf_buf_base = perf_double_buf_base[0]; // Start dumping from lower half of L1 perf buffer.
  if constexpr (INTERMED_DUMP) {
    epoch_perf_scratch[0] = PERF_DUMP_END_SIGNAL;
    perf_end = NCRISC_PERF_BUF_SIZE >> 3; // 4 bytes per event for half buffer
    if constexpr (PERF_DUMP_LEVEL < 2) {
      perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
      perf_end = NCRISC_PERF_BUF_SIZE >> 2; // 4 bytes per event, no double buffer in this mode.
    }

  } else if (PERF_DUMP_CONCURRENT) {
#if PERF_DUMP_CONCURRENT == 1
    perf_end = NCRISC_PERF_BUF_SIZE >> 3;
    // This should be set to the largest power of 2 that is smaller than num_perf_queue_dram_slots
#endif
  } else {
    perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
    perf_end = NCRISC_PERF_BUF_SIZE >> 2; // 4 bytes per event
  }
  if constexpr (PERF_DUMP_LEVEL > 1) {
    perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
    perf_buf_base[perf_end-2] = PERF_DUMP_PADDING;
    perf_buf_base[perf_end-3] = PERF_DUMP_PADDING;
  }
  if constexpr (PERF_DUMP_CONCURRENT) {
    perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
  }
  uint32_t temp = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_L);
  wall_clk_h = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_H);
  record_perf_value_l1(perf_event::WALL_CLOCK_TOP_32B, wall_clk_h);
  
}

void __attribute__ ((noinline)) record_perf_dump_end() {
  if constexpr (PERF_DUMP_CONCURRENT && PERF_DUMP_LEVEL > 1) {
    spill_risc_epoch_perf_scratch();
  }
  if (perf_index < perf_end) {
    perf_buf_base[perf_index] = PERF_DUMP_END_SIGNAL;
    perf_index += 1;
  }
  if constexpr (PERF_DUMP_CONCURRENT) {
    perf_buf_base[perf_end - 1] = PERF_DUMP_END_SIGNAL;
  }
  if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
    EPOCH_INFO_PTR->perf_dram_copy_req[3]++;
  } else {
    EPOCH_INFO_PTR->perf_dram_copy_req[3] += 2;
  }
  check_dram_spill_requests(true);
  if constexpr (PERF_DUMP_LEVEL > 1 && INTERMED_DUMP) {
    spill_risc_epoch_perf_scratch();
  }
}

void record_info_timestamp(uint32_t event_id, uint32_t data) {
  uint32_t low;
  uint32_t high;
  low = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
  high = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_H);
  if (high != wall_clk_h) {
    wall_clk_h = high;
    record_perf_value_and_check_overflow(perf_event::WALL_CLOCK_TOP_32B, high);
  }
  record_info_value_and_check_overflow(event_id, low, data);
}

void record_timestamp(uint32_t event_id) {
  uint32_t low;
  uint32_t high;
  low = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
  high = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_H);
  if (high != wall_clk_h) {
    wall_clk_h = high;
    //record_perf_value(perf_event::WALL_CLOCK_TOP_32B, high);
    record_perf_value_and_check_overflow(perf_event::WALL_CLOCK_TOP_32B, high);
  }
  //record_perf_value(event_id, low);
  record_perf_value_and_check_overflow(event_id, low);
}

void record_timestamp_l1(uint32_t event_id) {
  uint32_t low;
  uint32_t high;
  low = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_L);
  high = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_H);
  if (high != wall_clk_h) {
    wall_clk_h = high;
    //record_perf_value(perf_event::WALL_CLOCK_TOP_32B, high);
    record_perf_value_and_check_overflow_l1(perf_event::WALL_CLOCK_TOP_32B, high);
  }
  //record_perf_value(event_id, low);
  record_perf_value_and_check_overflow_l1(event_id, low);
}

void record_timestamp_at_offset(uint32_t event_id, uint32_t offset) {
  uint32_t low;
  uint32_t high;
  low = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
  high = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_H);
  record_perf_value_at_offset(event_id, low, high, offset);
  
}

void record_timestamp_at_offset_l1(uint32_t event_id, uint32_t offset) {
  uint32_t low;
  uint32_t high;
  low = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_L);
  high = reg_read_barrier_l1(RISCV_DEBUG_REG_WALL_CLOCK_H);
  record_perf_value_at_offset(event_id, low, high, offset);
  
}

#if PERF_DUMP_CONCURRENT == 1
// Check if queue is full
// queue wraps at 2 * num_slots
// num_slots must be a power of 2
inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool is_queue_full_l1(uint32_t rd_ptr, uint32_t wr_ptr, uint32_t num_slots) {
  uint32_t limit = (num_slots << 1);
  uint32_t occupancy;
  if (rd_ptr <= wr_ptr) {
    occupancy = wr_ptr - rd_ptr;
  } else {
    occupancy = limit + wr_ptr - rd_ptr;
  }
  return occupancy >= num_slots;
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) uint32_t get_wrapped_ptr(uint32_t ptr, uint32_t num_slots) {
  if (ptr >= num_slots) {
    ptr -= num_slots;
  }
  return ptr;
}
#endif

void check_dram_spill_requests_once() {
  if (epoch_perf_en == 0) {
    for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
      uint32_t current_req = EPOCH_INFO_PTR->perf_dram_copy_req[i];
      while(thread_dram_copy_ack[i] < current_req) {
        thread_dram_copy_ack[i]++;
        thread_l1_buf_sel[i] = 1 - thread_l1_buf_sel[i]; //toggle thread_l1_buf_sel for next time.
      }
      EPOCH_INFO_PTR->perf_dram_copy_ack[i] = thread_dram_copy_ack[i];      
    }
    return;
  }
  RISC_POST_STATUS(0x1FFFFFF7);
  bool thread_ack_change[l1_mem::address_map::PERF_NUM_THREADS];
  for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
    thread_ack_change[i] = false;
    if (EPOCH_INFO_PTR->perf_dram_copy_req[i] != thread_dram_copy_ack[i]) {
#if PERF_DUMP_CONCURRENT == 1
      // thread_dram_ptr's, for real-time perf, is populated by pipegen with the following information:
      // thread_dram_ptr[0]: Address of the host queue rd/wr ptr's of the selected core for each dram channel
      //                    l1_mem::address_map::PERF_QUEUE_PTRS of the first core assigned to each dram channel
      //                    At this address pointers are placed as follows:
      //                       rd_host_q    - 4B    updated by host
      //                       wr_host_q    - 4B    updated every time the first core assigned to each dram channel dumps to host
      //                       trace_header - 4B
      //                       last_wr_host_q ptr that was read from the first core assigned to each dram bank
      //                    Same l1 address for all other cores assigned to that dram core will just keep a copy of
      //                    this header (except wr_host_q. This only gets atomically incremented for the first core)
      //                    Ncrisc of each core must reset its own rd/wr pointer region in the beginning and
      //                    update rd_host_q when there is no sace to dump
      //                    atomic update wr_host_q whenever it wants to dump a new perf trace
      // thread_dram_ptr[1]: 
      //                    0 -> 3: bit[0] is set to 1 for the seclected core for each dram channel. And set to 0 for all other cores
      //                    4 -> 7: log base 2 of num_slots of the host queue -> total number of slots of the queue = 2 ^ <THIS_VALUE>
      //                    8 -> 31: Unused
      //                    32 -> 63: Initial performance trace header for each queue. Thread_id gets updated after every epoch by ncrisc, and epoch_id gets updated by each thread
      // thread_dram_ptr[2]: The base address for the host queue assigned to this core
      // thread_dram_ptr[3]: Unused
      
      // If in the previous iteration of the event loop, the wr pointer was updated but dram was full and thread dump was not copied to dram,
      // skip the wr pointer update and check for space and copy to dram

      bool is_selected_core = tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[1]) & 0b1;
      // Do not update the host rd pointer for the seclected core since host directly updates it
      if (!is_selected_core) {
        // If the host queue was full, update the rd pointer from the first core assigned to each dram bank
        // That register get sdirectly updated by host
        // Do not update for the selected core
        uint64_t l1_address_src_core = tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[0]);
        uint32_t dst_addr_local_core = l1_mem::address_map::PERF_QUEUE_PTRS;
        while (!ncrisc_noc_fast_read_ok(PERF_DUMP_NOC, NCRISC_SMALL_TXN_CMD_BUF)) {};
        ncrisc_noc_fast_read_any_len_l1(PERF_DUMP_NOC, NCRISC_SMALL_TXN_CMD_BUF, l1_address_src_core, dst_addr_local_core, 4, NCRISC_RD_DEF_TRID);
        while (!ncrisc_noc_reads_flushed_l1(PERF_DUMP_NOC, NCRISC_RD_DEF_TRID)) {};
      }
      uint32_t num_host_queue_slots_pow_2 = (tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[1]) >> 8) & 0xff;
      if (!pending_write) {
        // The wr_host_q address of the first core assigned to each dram bank
        uint64_t l1_address_src_core = tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[0]) + 4;
        // The last_wr_host_q pointer for the current core
        uint32_t l1_return_addr = l1_mem::address_map::PERF_WR_PTR_COPY;
        uint64_t l1_return_addr_noc = NOC_XY_ADDR(my_x[0], my_y[0], l1_return_addr);
        while (!ncrisc_noc_fast_read_ok_l1(0, NCRISC_SMALL_TXN_CMD_BUF));
        while (!ncrisc_noc_fast_write_ok_l1(0, NCRISC_SMALL_TXN_CMD_BUF));
        // Read wr_host_q of the first core into last_wr_host_q of current core and do atomic increment for wr_host_q of first core
        // Wrap at 2 ^ (num_host_queue_slots_pow_2 + 1)
        noc_atomic_read_and_increment_l1(0, NCRISC_SMALL_TXN_CMD_BUF, l1_address_src_core, 1, num_host_queue_slots_pow_2, l1_return_addr_noc, false, NCRISC_WR_DEF_TRID);
        while (!ncrisc_noc_nonposted_writes_sent_l1(0, NCRISC_WR_DEF_TRID)){};
        while(!ncrisc_noc_reads_flushed_l1(0, NCRISC_WR_DEF_TRID)) {};
        pending_write = true;
      }
      volatile tt_uint64_t tt_l1_ptr *perf_queue_ptr = reinterpret_cast<volatile tt_uint64_t tt_l1_ptr*>(l1_mem::address_map::PERF_QUEUE_PTRS);
      volatile tt_uint64_t tt_l1_ptr *atomic_wr_ptr = reinterpret_cast<volatile tt_uint64_t tt_l1_ptr*>(l1_mem::address_map::PERF_WR_PTR_COPY);
      uint32_t atomic_wr = uint32_t(tt_l1_load(atomic_wr_ptr)&0xffffffff);
      uint32_t perf_queue = uint32_t(tt_l1_load(perf_queue_ptr)&0xffffffff);
      if (!is_queue_full_l1(perf_queue, atomic_wr, (1 << num_host_queue_slots_pow_2))) {
        // debug_mailbox_base_copy[0]++;
        // get address of perf trace in l1
        uint32_t l1_address = thread_l1_buf_sel[i] ? thread_l1_addr_h(i) : thread_l1_addr_l(i);
        // get address in host queue to dump into
        uint32_t wrapped_ptr = get_wrapped_ptr(atomic_wr,(1 << num_host_queue_slots_pow_2));
        uint64_t dst_addr = tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[2]) +
           wrapped_ptr * (host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP * CONCURRENT_PERF_BUF_SIZE);
        ncrisc_noc_fast_write_any_len_l1(PERF_DUMP_NOC, NCRISC_WR_CMD_BUF, l1_address, dst_addr, CONCURRENT_PERF_BUF_SIZE, PERF_DUMP_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        while (!ncrisc_noc_nonposted_writes_sent_l1(PERF_DUMP_NOC, NCRISC_WR_DEF_TRID)){};
        // while (!ncrisc_noc_nonposted_writes_flushed_l1(PERF_DUMP_NOC, NCRISC_WR_DEF_TRID)){};
        pending_write = false;
        thread_l1_buf_sel[i] = 1 - thread_l1_buf_sel[i]; // toggle thread_l1_buf_sel for next dump
        thread_dram_copy_ack[i]++;
        thread_ack_change[i] = true;
      }
#else
      if (thread_dram_copy_ack[i] < thread_req_max[i]) {
        uint32_t l1_address = thread_l1_buf_sel[i] ? thread_l1_addr_h(i) : thread_l1_addr_l(i);
        if constexpr (INTERMED_DUMP) {
          const int32_t trisc_perf_buf_size = (i == 1) ? l1_mem::address_map::MATH_PERF_BUF_SIZE/2 :
                                              (i == 3) ? NCRISC_PERF_BUF_SIZE/2 :
                                              (i == 4) ? l1_mem::address_map::BRISC_PERF_BUF_SIZE/2 : TRISC_PERF_BUF_SIZE/2;
          ncrisc_noc_fast_write_any_len_l1(PERF_DUMP_NOC, NCRISC_WR_CMD_BUF, l1_address, thread_dram_ptr[i], trisc_perf_buf_size, PERF_DUMP_VC, false, false, 1, NCRISC_WR_DEF_TRID);
          thread_dram_ptr[i] += trisc_perf_buf_size;
          thread_l1_buf_sel[i] = 1 - thread_l1_buf_sel[i]; //toggle thread_l1_buf_sel for next time.
        } else {
          const int32_t trisc_perf_buf_size = (i == 1) ? l1_mem::address_map::MATH_PERF_BUF_SIZE :
                                              (i == 3) ? NCRISC_PERF_BUF_SIZE :
                                              (i == 4) ? l1_mem::address_map::BRISC_PERF_BUF_SIZE : TRISC_PERF_BUF_SIZE;
          ncrisc_noc_fast_write_any_len_l1(PERF_DUMP_NOC, NCRISC_WR_CMD_BUF, l1_address, thread_dram_ptr[i], trisc_perf_buf_size, PERF_DUMP_VC, false, false, 1, NCRISC_WR_DEF_TRID);
          thread_dram_ptr[i] += trisc_perf_buf_size;
        }
      }
      if constexpr (INTERMED_DUMP) {
        thread_dram_copy_ack[i]++;
      } else {
        thread_dram_copy_ack[i] += 2;
      }
      thread_ack_change[i] = true;
#endif
    }
  }
#if PERF_DUMP_CONCURRENT == 0
  while (!ncrisc_noc_nonposted_writes_sent_l1(PERF_DUMP_NOC, NCRISC_WR_DEF_TRID)){};
#endif
  for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
    if (thread_ack_change[i]) {
      EPOCH_INFO_PTR->perf_dram_copy_ack[i] = thread_dram_copy_ack[i];
    }
  }
  RISC_POST_STATUS(0x1FFFFFF8);
}

void check_dram_spill_requests(bool blocking) {
  if (epoch_perf_en == 0) {
    for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
      uint32_t current_req = EPOCH_INFO_PTR->perf_dram_copy_req[i];
      while(thread_dram_copy_ack[i] < current_req) {
        thread_dram_copy_ack[i]++;
        thread_l1_buf_sel[i] = 1 - thread_l1_buf_sel[i]; //toggle thread_l1_buf_sel for next time.
      }
      EPOCH_INFO_PTR->perf_dram_copy_ack[i] = thread_dram_copy_ack[i];      
    }
    return;
  }
  RISC_POST_STATUS(0x1FFFF127);
  if (blocking) {
    bool any_remaining_dumps = true;
    while (any_remaining_dumps) {
      any_remaining_dumps = false;
      for (int i = 0; i < l1_mem::address_map::PERF_NUM_THREADS; i++) {
        if (EPOCH_INFO_PTR->perf_dram_copy_req[i] != thread_dram_copy_ack[i]) {
          any_remaining_dumps = true;
          break;
        }
      }
      if (any_remaining_dumps) {
        check_dram_spill_requests_once();
      }
    }
  } else {
    check_dram_spill_requests_once();
  }
  RISC_POST_STATUS(0x1FFFF128);
}

void record_perf_value_l1(uint32_t event_id, uint32_t event_value) {
    // Record a single event, and a timestamp
    perf_buf_base[perf_index] = event_id;
    perf_buf_base[perf_index + 1] = event_value;
    perf_index += 2;
}

void switch_perf_buffers_and_record_event_l1(uint32_t event_id, uint32_t event_value) {
    if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
        EPOCH_INFO_PTR->perf_dram_copy_req[3]++;
        perf_buf_base = perf_buf_base == perf_double_buf_base[0] ? perf_double_buf_base[1] : perf_double_buf_base[0];
        //add padding to last two locations incase events cannot be written to them becuase of overlap 
        //with last location that is supposed to hold perf dump end signal.
        //This is done so that post processor does not read any stale event as a valid event.
        perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
        perf_buf_base[perf_end-2] = PERF_DUMP_PADDING;
        perf_buf_base[perf_end-3] = PERF_DUMP_PADDING;
        if constexpr (INTERMED_DUMP) {
            perf_index = 0;
        } else {
            perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
            volatile uint32_t tt_l1_ptr *perf_header = reinterpret_cast<volatile uint32_t tt_l1_ptr*>(l1_mem::address_map::PERF_THREAD_HEADER);
            perf_buf_base[1] = perf_header[0];
            perf_index = 2;
            perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
        }
        record_perf_value_l1(event_id, event_value);
    }
}

void record_perf_value_and_check_overflow_l1(uint32_t event_id, uint32_t event_value, uint32_t leave_space) {
    // Record a single value, and a timestamp
    if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
        if (perf_index + 1 < perf_end - 1) {
            record_perf_value_l1(event_id, event_value);
        } else {
            switch_perf_buffers_and_record_event_l1(event_id, event_value);
        }
    } else {
        if (perf_index + 1 < perf_end - 1) {
            record_perf_value_l1(event_id, event_value);
        }
    }
}

// This function gets called when half-perf-buffer is full and need to switch.
void switch_perf_buffers_and_record_info(uint32_t event_id, uint32_t event_time, uint32_t event_value) {
    if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
        EPOCH_INFO_PTR->perf_dram_copy_req[3]++;
        perf_buf_base = perf_buf_base == perf_double_buf_base[0] ? perf_double_buf_base[1] : perf_double_buf_base[0];
        //add padding to last two locations incase events cannot be written to them becuase of overlap 
        //with last location that is supposed to hold perf dump end signal.
        //This is done so that post processor does not read any stale event as a valid event.
        perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
        perf_buf_base[perf_end-2] = PERF_DUMP_PADDING;
        perf_buf_base[perf_end-3] = PERF_DUMP_PADDING;
        if constexpr (INTERMED_DUMP) {
            perf_index = 0;
        } else {
            perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
            volatile uint32_t tt_l1_ptr *perf_header = reinterpret_cast<volatile uint32_t tt_l1_ptr*>(l1_mem::address_map::PERF_THREAD_HEADER);
            perf_buf_base[1] = perf_header[0];
            perf_index = 2;
            perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
        }
        record_info_value(event_id, event_time, event_value);
    }
}

void spill_risc_epoch_perf_scratch() {
    if (epoch_perf_en == 0) {
        return;
    }

#if PERF_DUMP_CONCURRENT && PERF_DUMP_LEVEL > 1
    for (uint i = 2; i < PERF_START_OFFSET; i++) {
        if (perf_index < perf_end -1) {
            perf_buf_base[perf_index] = epoch_perf_scratch[i];
            perf_index++;
        } else {
            EPOCH_INFO_PTR->perf_dram_copy_req[3]++;
            perf_buf_base = perf_buf_base == perf_double_buf_base[0] ? perf_double_buf_base[1] : perf_double_buf_base[0];
            perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
            volatile uint32_t tt_l1_ptr *perf_header = reinterpret_cast<volatile uint32_t tt_l1_ptr*>(l1_mem::address_map::PERF_THREAD_HEADER);
            perf_buf_base[1] = perf_header[0];
            perf_index = 2;
            perf_buf_base[perf_end-1] = PERF_DUMP_PADDING;
        }
    }
#else
    //EPOCH_INFO_PTR->perf_dram_addr[3] == 0 signals that dram buffer has been filled up and that
    //for current epoch, we do not have dram buffer space available for perf dump.
    if (tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[3])) {
        ncrisc_noc_fast_write_any_len_l1(PERF_DUMP_NOC, NCRISC_WR_CMD_BUF, (uint32_t)epoch_perf_scratch, tt_l1_load(&EPOCH_INFO_PTR->perf_dram_addr[3]), sizeof(epoch_perf_scratch), PERF_DUMP_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        while (!ncrisc_noc_nonposted_writes_sent_l1(PERF_DUMP_NOC, NCRISC_WR_DEF_TRID)){};
    }
#endif
}

}

