// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include "epoch.h"
#include "noc_nonblocking_api.h"

#ifndef INTERMED_DUMP
#define INTERMED_DUMP 0
#endif

#ifndef PERF_DUMP_LEVEL
#define PERF_DUMP_LEVEL 0
#endif

#ifndef PERF_DUMP_CONCURRENT
#define PERF_DUMP_CONCURRENT 0
#endif

#ifndef OVERLAY_DECOUPLE
#define OVERLAY_DECOUPLE 0
#endif

extern volatile uint32_t local_mem_barrier;

namespace risc
{
const uint32_t PERF_SPILL_CHECK_MASK = (0x1 << 4) - 1;
const uint32_t PERF_DUMP_VC = 0;
const uint32_t PERF_DUMP_NOC = 0;
constexpr uint32_t PERF_DUMP_END_SIGNAL = 0xbeeff00d;
constexpr uint32_t PERF_DUMP_PADDING = 0xdeadbead;

#if PERF_DUMP_CONCURRENT
constexpr uint8_t reserve_sapce_for_header = 1;
#else
constexpr uint8_t reserve_sapce_for_header = 0;
#endif

constexpr uint32_t EPOCH_START_OFFSET = 1 + reserve_sapce_for_header;
constexpr uint32_t EPOCH_END_OFFSET = 4 + reserve_sapce_for_header;
constexpr uint32_t STREAM_HANDLER_START_OFFSET = 7 + reserve_sapce_for_header;
constexpr uint32_t STREAM_HANDLER_END_OFFSET = 10 + reserve_sapce_for_header;
constexpr uint32_t EPILOGUE_START_OFFSET = 13 + reserve_sapce_for_header;
constexpr uint32_t EPILOGUE_END_OFFSET = 16 + reserve_sapce_for_header;
constexpr uint32_t PROLOGUE_START_OFFSET = 19 + reserve_sapce_for_header;
constexpr uint32_t PROLOGUE_END_OFFSET = 22 + reserve_sapce_for_header;
constexpr uint32_t PERF_START_OFFSET = 25 + reserve_sapce_for_header;

extern uint32_t perf_end;
extern volatile uint32_t tt_l1_ptr *perf_double_buf_base[2];
extern volatile uint32_t tt_l1_ptr *perf_buf_base;
extern uint32_t perf_index;
extern volatile uint32_t tt_l1_ptr epoch_perf_scratch[PERF_START_OFFSET];
extern uint8_t epoch_perf_en;


struct perf_event {
    constexpr static uint32_t EPOCH = 1 << 24;
    constexpr static uint32_t STREAM_HANDLER_LOOP = 2 << 24;
    constexpr static uint32_t EPOCH_EPILOGUE = 3 << 24;
    constexpr static uint32_t STREAM_HANDLER_INIT = 4 << 24;
    constexpr static uint32_t EPOCH_Q_SLOT_COMPLETE = 5 << 24;
    constexpr static uint32_t WALL_CLOCK_TOP_32B = 6 << 24;
    constexpr static uint32_t DRAM_READ_ISSUED = 7 << 24;
    constexpr static uint32_t DRAM_READ_TILE_FLUSHED = 8 << 24;
    constexpr static uint32_t DRAM_WRITE_SENT = 9 << 24;
    constexpr static uint32_t DRAM_WRITE_TILES_CLEARED = 10 << 24;
    constexpr static uint32_t DRAM_IO_Q_STATUS = 11 << 24;
    constexpr static uint32_t STREAM_RESTART = 12 << 24;
    constexpr static uint32_t STREAM_INFO = 13 << 24;
    constexpr static uint32_t STREAM_BUF_STATUS = 14 << 24;
    constexpr static uint32_t EPOCH_Q_EMPTY = 15 << 24;
    constexpr static uint32_t STREAM_MISC_INFO = 16 << 24;

};

#if (PERF_DUMP_LEVEL == 0)
#define TRISC_PERF_BUF_SIZE l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0
#define NCRISC_PERF_BUF_SIZE l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0
#else
#define TRISC_PERF_BUF_SIZE l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1
#define NCRISC_PERF_BUF_SIZE l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1
#endif

#if (PERF_DUMP_LEVEL == 0)
#define CONCURRENT_PERF_BUF_SIZE l1_mem::address_map::BRISC_PERF_BUF_SIZE
#else
#define CONCURRENT_PERF_BUF_SIZE (l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1/2)
#endif

#define T1_PERF_L l1_mem::address_map::MATH_PERF_BUF_BASE_ADDR
#define T1_PERF_H l1_mem::address_map::MATH_PERF_BUF_BASE_ADDR + l1_mem::address_map::MATH_PERF_BUF_SIZE/2
#define T0_PERF_L l1_mem::address_map::UNPACK_PACK_PERF_BUF_BASE_ADDR
#define T0_PERF_H (T0_PERF_L + TRISC_PERF_BUF_SIZE/2)
#define T2_PERF_L (T0_PERF_L + TRISC_PERF_BUF_SIZE)
#define T2_PERF_H (T2_PERF_L + TRISC_PERF_BUF_SIZE/2)
#define T3_PERF_L l1_mem::address_map::NCRISC_L1_PERF_BUF_BASE
#define T3_PERF_H (T3_PERF_L + NCRISC_PERF_BUF_SIZE/2)
#define BRISC_PERF_L l1_mem::address_map::BRISC_PERF_BUF_BASE_ADDR
#define BRISC_PERF_H (BRISC_PERF_L + (l1_mem::address_map::BRISC_PERF_BUF_SIZE)/2)

#define thread_l1_addr_l(x) \
    ((x == 0) ? T0_PERF_L : \
    (x == 1) ? T1_PERF_L : \
    (x == 2) ? T2_PERF_L : \
    (x == 3) ? T3_PERF_L : \
    BRISC_PERF_L)

#define thread_l1_addr_h(x) \
    ((x == 0) ? T0_PERF_H : \
    (x == 1) ? T1_PERF_H : \
    (x == 2) ? T2_PERF_H : \
    (x == 3) ? T3_PERF_H : \
    BRISC_PERF_H)

inline void record_info(uint32_t event_id, uint32_t epoch_iters_remaining, uint32_t epoch_q_slots_remaining, uint32_t q_slot_size_tiles, uint32_t data_chunk_size_tiles, uint32_t data_chunk_size_bytes, uint32_t flags) {
    // stream information
    perf_buf_base[perf_index] = event_id;
    perf_buf_base[perf_index + 1] = flags;
    perf_buf_base[perf_index + 2] = epoch_iters_remaining;
    perf_buf_base[perf_index + 3] = epoch_q_slots_remaining;
    perf_buf_base[perf_index + 4] = q_slot_size_tiles;
    perf_buf_base[perf_index + 5] = data_chunk_size_tiles;
    perf_buf_base[perf_index + 6] = data_chunk_size_bytes;
    perf_index += 7;
}

inline void record_info_value(uint32_t event_id, uint32_t time, uint32_t data) {
    // Record a single value, and a timestamp
    perf_buf_base[perf_index] = event_id;
    perf_buf_base[perf_index + 1] = time;
    perf_buf_base[perf_index + 2] = data;
    perf_index += 3;
}

inline void record_perf_value(uint32_t event_id, uint32_t event_value) {
    // Record a single event, and a timestamp
    perf_buf_base[perf_index] = event_id;
    perf_buf_base[perf_index + 1] = event_value;
    perf_index += 2;
}

void __attribute__((section("code_l1"))) record_perf_value_l1(uint32_t event_id, uint32_t event_value);

inline void record_perf_value_at_offset(uint32_t event_id, uint32_t event_value1, uint32_t event_value2, uint32_t offset) {
    // Record a single value, and a timestamp at given offset
    if constexpr ((INTERMED_DUMP || PERF_DUMP_CONCURRENT) && (PERF_DUMP_LEVEL > 1)) {
        epoch_perf_scratch[offset] = event_id;
        epoch_perf_scratch[offset + 1] = event_value1;
        epoch_perf_scratch[offset + 2] = event_value2;
    } else {
        perf_buf_base[offset] = event_id;
        perf_buf_base[offset + 1] = event_value1;
        perf_buf_base[offset + 2] = event_value2;
    }
}

// This function gets called when half-perf-buffer is full and need to switch.
inline void switch_perf_buffers_and_record_event(uint32_t event_id, uint32_t event_value) {
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
        record_perf_value(event_id, event_value);
    }
}

// This function gets called when half-perf-buffer is full and need to switch.
void __attribute__((section("code_l1"))) switch_perf_buffers_and_record_event_l1(uint32_t event_id, uint32_t event_value);

inline void record_perf_value_and_check_overflow(uint32_t event_id, uint32_t event_value, uint32_t leave_space = 0) {
    // Record a single value, and a timestamp
    if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
        if (perf_index + 1 < perf_end - 1) {
            record_perf_value(event_id, event_value);
        } else {
            switch_perf_buffers_and_record_event(event_id, event_value);
        }
    } else {
        if (perf_index + 1 < perf_end - 1) {
            record_perf_value(event_id, event_value);
        }
    }
}

void __attribute__((section("code_l1"))) record_perf_value_and_check_overflow_l1(uint32_t event_id, uint32_t event_value, uint32_t leave_space = 0);

// This function gets called when half-perf-buffer is full and need to switch.
void __attribute__((section("code_l1"))) switch_perf_buffers_and_record_info(uint32_t event_id, uint32_t event_time, uint32_t event_value);

inline void record_info_value_and_check_overflow(uint32_t event_id, uint32_t event_time, uint32_t event_value, uint32_t leave_space = 0) {
    // Record a single value, and a timestamp
    if constexpr (INTERMED_DUMP || PERF_DUMP_CONCURRENT) {
        if (perf_index + 2 < perf_end - 1) {
            record_info_value(event_id, event_time, event_value);
        } else {
            switch_perf_buffers_and_record_info(event_id, event_time, event_value);
        }
    } else {
        if (perf_index + 2 < perf_end - 1) {
            record_info_value(event_id, event_time, event_value);
        }
    }
}

void __attribute__((section("code_l1"))) spill_risc_epoch_perf_scratch();

#if OVERLAY_DECOUPLE == 1
void wait_for_all_epoch_commands_sent_signal()__attribute__((section("code_l1")));
#endif
void init_perf_dram_state_first_epoch()__attribute__((section("code_l1")));
void check_host_dram_buffer_drain_signal_and_initialize()__attribute__((section("code_l1")));
void init_perf_dram_state()__attribute__((section("code_l1")));
void allocate_perf_buffer()__attribute__((section("code_l1")));
void record_perf_dump_end()__attribute__((section("code_l1")));
void record_timestamp(uint32_t event_id);
void record_timestamp_l1(uint32_t event_id)__attribute__((section("code_l1")));
void record_timestamp_at_offset(uint32_t event_id, uint32_t offset);
void record_timestamp_at_offset_l1(uint32_t event_id, uint32_t offset)__attribute__((section("code_l1")));
void check_dram_spill_requests(bool blocking)__attribute__((section("code_l1")));
void check_dram_spill_requests_once()__attribute__((section("code_l1")));
void record_info_timestamp(uint32_t event_id, uint32_t data);

}
