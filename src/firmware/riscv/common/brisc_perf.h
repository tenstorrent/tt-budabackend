// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include "risc_attribs.h"
#include "risc_common.h"
#include "tensix.h"
#include "tt_log.h"

#ifndef INTERMED_DUMP
#define INTERMED_DUMP 0
#endif

#ifndef PERF_DUMP_CONCURRENT
#define PERF_DUMP_CONCURRENT 0
#endif

#ifndef OVERLAY_DECOUPLE
#define OVERLAY_DECOUPLE 0
#endif

static constexpr uint32_t PERF_DUMP_END_SIGNAL = 0xbeeff00d;
extern volatile uint32_t tt_l1_ptr *perf_buf_base;
// extern uint8_t perf_buf_base_id;
extern uint16_t perf_index;
extern uint16_t perf_end;
// extern volatile uint tt_l1_ptr * ncrisc_ack_addr;
extern int32_t dram_dump_req_local;
// extern uint8_t perf_buf_base_id;
extern uint8_t thread_id;
extern uint16_t perf_epoch_id;
extern bool first_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
extern bool first_timestamp_recorded_output[PERF_MAX_NUM_OUTPUTS];
extern bool last_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
extern bool last_timestamp_recorded_output[PERF_MAX_NUM_OUTPUTS];
#if OVERLAY_DECOUPLE == 1
extern uint8_t output_decouple_mask;
extern uint32_t decoupled_output_num_tiles;
#endif

namespace perf {

enum class BriscEventType {
   INPUT_NUM_TILES = 1,
   OUTPUT_NUM_TILES = 2,
   INPUT_TILE_POP = 3,
   OUTPUT_TILE_PUSH = 4,
   STALL_BRISC_FOR_DRAM_PERF_DUMP = 5,
   EPOCH = 6,
};


inline uint32_t get_event_id(BriscEventType event_type, uint epoch_id, uint operand_idx = 0) {
   uint32_t event_id;
   event_id = (uint(event_type) & 0xf);
   event_id |= ((epoch_id & 0xffff) << 4);
   event_id |= ((operand_idx & 0xf) << 20);
   
   return event_id;
}

inline void record_perf_value(uint32_t event_id, uint32_t event_value_lo_32b, uint32_t event_value_hi_32b) {
   if (perf_index + 2 < perf_end - 1) {
    perf_buf_base[perf_index] = event_id;
    perf_buf_base[perf_index + 1] = event_value_hi_32b;
    perf_buf_base[perf_index + 2] = event_value_lo_32b;
    perf_index += 3;
   }
}

inline void record_perf_dump_end() {
   if (perf_index < perf_end) {
      perf_buf_base[perf_index] = PERF_DUMP_END_SIGNAL;
      perf_index += 1;      
   }
}

inline void record_timestamp_64b(uint event_id) {
   if (perf_index + 2 < perf_end - 1) {
      uint32_t timestamp_low = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_L);
      uint32_t timestamp_high = reg_read_barrier(RISCV_DEBUG_REG_WALL_CLOCK_H);
      record_perf_value(event_id, timestamp_low, timestamp_high);
   }
}


inline void last_trisc_perf_dump_to_dram() {
   if (perf_index > 0) {

      volatile uint tt_l1_ptr * ncrisc_ack_addr = &EPOCH_INFO_PTR->perf_dram_copy_ack[thread_id];
      int32_t dram_dump_req_local = EPOCH_INFO_PTR->perf_dram_copy_req[thread_id];
      int32_t ack_local = *ncrisc_ack_addr;
      while (ack_local <= dram_dump_req_local - 1) {
         ack_local = *ncrisc_ack_addr;
      }
#if (INTERMED_DUMP == 1)
      bool half_buf_full = perf_index >= (perf_end >> 1);
      if (half_buf_full) {
         dram_dump_req_local += 2;
      } else {
         dram_dump_req_local++;
      }
#elif (PERF_DUMP_CONCURRENT == 1)
      dram_dump_req_local += 1;
      perf_buf_base[perf_end - 1] = PERF_DUMP_END_SIGNAL;
#else
      dram_dump_req_local += 2;
#endif
      EPOCH_INFO_PTR->perf_dram_copy_req[thread_id] = dram_dump_req_local;
   }
   perf_epoch_id++;
}


#if OVERLAY_DECOUPLE == 1

inline void get_overlay_decouple_mailbox() {
  output_decouple_mask = (*PERF_RISC_MAILBOX_OUTPUT_DECOUPLE_MASK_PTR) & 0xff;
}
#endif

inline void setup_perf_trace() {
  perf_buf_base = reinterpret_cast<uint32_t *>(l1_mem::address_map::BRISC_PERF_BUF_BASE_ADDR);
  perf_end = l1_mem::address_map::BRISC_PERF_BUF_SIZE >> 2;
  perf_epoch_id = 0;
}

inline void initialize_perf_trace_for_epoch() {

    volatile uint tt_l1_ptr * ncrisc_ack_addr = &EPOCH_INFO_PTR->perf_dram_copy_ack[thread_id];
    int32_t dram_dump_req_local = EPOCH_INFO_PTR->perf_dram_copy_req[thread_id];
    int32_t ack_local = *ncrisc_ack_addr;
    while (ack_local != dram_dump_req_local) {
        ack_local = *ncrisc_ack_addr;
    }
    perf_buf_base[0] = PERF_DUMP_END_SIGNAL;
    perf_index = 2;
#if (PERF_DUMP_CONCURRENT == 1)
    volatile uint32_t* header_ptr = reinterpret_cast<volatile uint32_t *>(l1_mem::address_map::PERF_THREAD_HEADER);
    uint32_t header = header_ptr[0];
    header = (header & 0xfff8ffff) | ((thread_id & 0b111) << 16);
    perf_buf_base[1] = header;
#else
    perf_buf_base[1] = 0xffffffff;
#endif
    for (uint i = 0; i < PERF_MAX_NUM_INPUTS; i++) {
        first_timestamp_recorded_input[i] = false;
        last_timestamp_recorded_input[i] = false;
    }
    for (uint i = 0; i < PERF_MAX_NUM_OUTPUTS; i++) {
        first_timestamp_recorded_output[i] = false;
        last_timestamp_recorded_output[i] = false;
    }
#if OVERLAY_DECOUPLE == 1
    get_overlay_decouple_mailbox();
    decoupled_output_num_tiles = 0;
#endif
}

inline __attribute__((always_inline)) bool first_bw_timestamp_recorded(uint8_t operand_idx, bool is_input) {
  if (is_input) {
    if (operand_idx >= PERF_MAX_NUM_INPUTS) {
      return true;
    } else {
      return first_timestamp_recorded_input[operand_idx];
    }
  } else {
    if (operand_idx >= PERF_MAX_NUM_OUTPUTS) {
      return true;
    } else {
      return first_timestamp_recorded_output[operand_idx];
    }
  }
}

inline __attribute__((always_inline)) bool last_bw_timestamp_recorded(uint8_t operand_idx, bool is_input) {
  if (is_input) {
    if (operand_idx >= PERF_MAX_NUM_INPUTS) {
      return true;
    } else {
      return last_timestamp_recorded_input[operand_idx];
    }
  } else {
    if (operand_idx >= PERF_MAX_NUM_OUTPUTS) {
      return true;
    } else {
      return last_timestamp_recorded_output[operand_idx];
    }
  }
}

#if OVERLAY_DECOUPLE == 1
inline __attribute__((always_inline)) void record_input_start_bw_timestamp(uint8_t operand_idx, uint32_t total_num_tiles, uint32_t current_num_tiles) {
  if (!first_bw_timestamp_recorded(operand_idx, true)) {
    uint32_t event_id = get_event_id(BriscEventType::INPUT_NUM_TILES, perf_epoch_id, operand_idx);
    record_perf_value(event_id, total_num_tiles - current_num_tiles, 0);
    event_id = get_event_id(BriscEventType::INPUT_TILE_POP, perf_epoch_id, operand_idx);
    record_timestamp_64b(event_id);
    first_timestamp_recorded_input[operand_idx] = true;
  }
}

inline __attribute__((always_inline)) void record_input_end_bw_timestamp(uint8_t operand_idx) {
  if (!last_bw_timestamp_recorded(operand_idx, true)) {
    uint32_t event_id = get_event_id(BriscEventType::INPUT_TILE_POP, perf_epoch_id, operand_idx);
    record_timestamp_64b(event_id);
    last_timestamp_recorded_input[operand_idx] = true;
  }
}

inline __attribute__((always_inline)) void record_output_start_bw_timestamp(uint8_t operand_idx) {
  if (!first_bw_timestamp_recorded(operand_idx, false)) {
    uint32_t event_id = get_event_id(BriscEventType::OUTPUT_TILE_PUSH, perf_epoch_id, operand_idx);
    record_timestamp_64b(event_id);
    first_timestamp_recorded_output[operand_idx] = true;
  }
}

inline __attribute__((always_inline)) void record_output_end_bw_timestamp(uint8_t operand_idx) {
  if (!last_bw_timestamp_recorded(operand_idx, false)) {
    uint32_t event_id = get_event_id(BriscEventType::OUTPUT_NUM_TILES, perf_epoch_id, operand_idx);
    record_perf_value(event_id, decoupled_output_num_tiles, 0);
    event_id = get_event_id(BriscEventType::OUTPUT_TILE_PUSH, perf_epoch_id, operand_idx);
    record_timestamp_64b(event_id);
    last_timestamp_recorded_output[operand_idx] = true;
    decoupled_output_num_tiles = 0;
  }
}
#endif

}
