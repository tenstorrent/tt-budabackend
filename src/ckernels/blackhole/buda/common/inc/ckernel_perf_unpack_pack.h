// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <l1_address_map.h>
#include "ckernel_include.h"
#include "ckernel_globals.h"
#include "ckernel.h"
#include "tensix.h"
#include "fw_debug.h"
#include "epoch.h"

#include "ckernel_perf_include.h"

#pragma GCC diagnostic ignored "-Wunused-function"

// Comment in/out to enable perf scratch even logging

namespace ckernel
{
extern uint32_t perf_index;
extern uint32_t perf_end;
// Perf-buffer are double buffered for spill_to_dram.
// Ncrisc will move one half to dram while trisc populates the other half.
// When INTERMED_DUMP = 0, we only dump into perf_buf_base[0].
extern volatile uint32_t *perf_buf_base[2];
// Selects the half of perf_buffer that trisc is currently writing into.
extern uint8_t perf_buf_base_id;
extern uint8_t thread_id;

// In math thread, THCON dumps perf buffers in l1.
// Therefore, incrementing the ncrisc perf_dram_buffer_req must be done by THCON as well.
// Flipping the l1 perf start address must also be done by THCON for math thread.
// Following variable keeps track of latest value of perf_dram_copy_req[1] from trisc perspective.
// The actual value might be different, because the queued THCON updates for perf_dram_copy_req[1] might have yet not been executed.
// We read this value initially for all threads to reduce the l1-reads.
extern int32_t dram_dump_req_local;
extern bool record_perf_events;
extern uint32_t perf_events_target_idx;
extern bool first_unpack_recorded;
extern volatile uint * ncrisc_ack_addr;
extern uint16_t current_outer_loop_iter;
#if OVERLAY_DECOUPLE == 1
extern uint8_t overlay_output_decouple_mask;
#endif

void allocate_perf_buffer();

// This function gets called when half-perf-buffer is full and need to switch.
// Only used for threads 0 and 2.
// For thread 1 a different function is used: switch_perf_buffers_for_math_thread
// If ncrisc has not yet finished dumping the next half of perf-buffer, trisc will stall.
// If is_perf_end_signal is true, we just need to write the PERF_DUMP_END_SIGNAL.
// This function should only get executed in INTERMED_DUMP mode.
void switch_perf_buffers();
void last_trisc_perf_dump_to_dram();

// Records three values into l1 and increments the wr-ptr by 3.
// Mostly used for recorfing the event id, and 64b wallclock.
inline void record_perf_value(uint32_t event_id, uint32_t event_value_lo_32b, uint32_t event_value_hi_32b) {
   perf_buf_base[perf_buf_base_id][perf_index] = event_id;
   perf_buf_base[perf_buf_base_id][perf_index + 1] = event_value_hi_32b;
   perf_buf_base[perf_buf_base_id][perf_index + 2] = event_value_lo_32b;
   perf_index += 3;
}

// Records the PERF_END_SIGNAL which indicates to the postprocessor that the epoch perf trace is done
// In concurrent mode we have to add the signal to the last word in the perf-trace
// Postprocessor in concurrent mode also checks the last word, to ensure the full trace has been sent to ci
inline void record_perf_dump_end() {
   if (perf_index < perf_end) {
      perf_buf_base[perf_buf_base_id][perf_index] = PERF_DUMP_END_SIGNAL;
      perf_index += 1;      
   }
#if PERF_DUMP_CONCURRENT == 1
   if (perf_index < perf_end) {
      perf_buf_base[perf_buf_base_id][perf_end - 1] = PERF_DUMP_END_SIGNAL;
   }
#endif
}

// Checks for buffer overflow, then records the perf-value
// In case of overflow, switch half buffers and signal ncrisc to send the perf-trace to dram in perf-intermediate or host in concurrent mode
inline void record_perf_value_and_check_overflow(uint32_t event_id, uint32_t event_value_lo_32b, uint32_t event_value_hi_32b, uint32_t leave_space = 0) {
   // In l1 mode always reserve the last event for PERF_DUMP_END_SIGNAL.
   int reserve_space_for_trisc_end_signal = 1;

#if (INTERMED_DUMP == 1) || (PERF_DUMP_CONCURRENT == 1)
   leave_space = 0;
   reserve_space_for_trisc_end_signal = 0;
   if (perf_index + 2 >= perf_end - reserve_space_for_trisc_end_signal - leave_space) {
      switch_perf_buffers();
   }
   record_perf_value(event_id, event_value_lo_32b, event_value_hi_32b);
#else
   if (perf_index + 2 < perf_end - reserve_space_for_trisc_end_signal - leave_space) {
      record_perf_value(event_id, event_value_lo_32b, event_value_hi_32b);
   }
#endif
}

// Records a 64b timestamp both the upper and lower 32b wallclock registers
inline void record_timestamp_64b(uint event_id, uint leave_space = 0) {
   if (record_perf_events) {
      uint32_t timestamp_low = reg_read(RISCV_DEBUG_REG_WALL_CLOCK_L);
      uint32_t timestamp_high = reg_read(RISCV_DEBUG_REG_WALL_CLOCK_H);
      record_perf_value_and_check_overflow(event_id, timestamp_low, timestamp_high, leave_space);
   }
}

// Only for B0, read the proper registers containing the counter value for total fpu/sfpu active cycles
// Record the perf-end. Check for overflow before recording the perf-end. In case of overflow, switch half buffers in concurrent/intermediate modes
inline void record_perf_dump_end_and_check_overflow() {
   if (thread_id == 1) {
      uint32_t reserve_space_for_trisc_end_signal = 1;
      if (perf_index + 3 <= perf_end-reserve_space_for_trisc_end_signal) { // Last event is always set to a default.
         perf_buf_base[perf_buf_base_id][perf_index] = reg_read(0xFFB12000 + 0x120);
         perf_buf_base[perf_buf_base_id][perf_index+1] = reg_read(0xFFB12000 + 0x124);
         perf_buf_base[perf_buf_base_id][perf_index+2] = 0;
         perf_buf_base[perf_buf_base_id][perf_index+3] = 0;
         perf_index += (PERF_CNT_DUMP_ENTRY_SIZE/sizeof(uint32_t));
      }
   }

#if (INTERMED_DUMP == 1) || (PERF_DUMP_CONCURRENT == 1)
   if (perf_index >= perf_end) {
      switch_perf_buffers();
   }
   record_perf_dump_end();
#else
   if (perf_index < perf_end) {
      record_perf_dump_end();
   }
#endif
}

// Temporarily stores the upper and lower 32b of wallclock into gpr's for the timestamp representating the latest wait-for-tile
// We only store timestamps before the first unpack
// The final timestamp in the gpr's will represent the timestamp of the last block of data available before the first-unpack
// This measurement is used as the first-unpack in the first-unpack-to-last-pack / total-runtime measurements
inline void record_latest_wait_for_tile() {
#if defined(PERF_DUMP)
   if (!first_unpack_recorded) {
      uint32_t timestamp_low = reg_read(RISCV_DEBUG_REG_WALL_CLOCK_L);
      uint32_t timestamp_high = reg_read(RISCV_DEBUG_REG_WALL_CLOCK_H);
      regfile[p_gpr_unpack::PERF_FIRST_UNP_LO] = timestamp_low & 0xffffffff;
      sync_regfile_write(p_gpr_unpack::PERF_FIRST_UNP_LO);
      regfile[p_gpr_unpack::PERF_FIRST_UNP_HI] = timestamp_high & 0xffffffff;
      sync_regfile_write(p_gpr_unpack::PERF_FIRST_UNP_HI);
   }
#endif
}

void increment_unpack_tiles(uint operand_idx, uint num_tiles);
void increment_pack_tiles(uint num_tiles);
#if OVERLAY_DECOUPLE == 1
inline uint32_t get_active_stream_idx(uint32_t stream_id) {
    std::uint32_t active_stream_idx;
    for (uint32_t active_streams_idx = 0; active_streams_idx < NOC_NUM_STREAMS; active_streams_idx++) {
      if (stream_id == EPOCH_INFO_PTR->active_streams[active_streams_idx]->stream_id) {
        active_stream_idx = active_streams_idx;
        break;
      }
    }
    return active_stream_idx;
}

void llk_push_all_packer_tiles_for_decoupling();
#endif

}
