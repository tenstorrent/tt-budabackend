// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace perf {

  enum class EventType {
    WAIT_FOR_INCOMING_TILES = 1,
    WAIT_FOR_FREE_TILES = 2,
    PACK_EACH_INPUT = 3,
    MATH_PERF_COUNTERS = 4,
    UNPACK_FIRST_INSTRUCTION = 5,
    STALL_TRISC_FOR_DRAM_PERF_DUMP = 7,
    NUM_TILES_UNPACK = 8,
    NUM_TILES_PACK = 9,
    OUTPUT_NUM_TILES = 10,
    OUTPUT_TIMESTAMP = 11,
  };

  enum class NcriscEventType {
    EPOCH = 1,
    STREAM_HANDLER_LOOP = 2,
    EPOCH_EPILOGUE = 3,
    STREAM_HANDLER_INIT = 4,
    EPOCH_Q_SLOT_COMPLETE = 5,
    WALL_CLOCK_TOP_32B = 6,
    DRAM_READ_ISSUED = 7,
    DRAM_READ_TILE_FLUSHED = 8,
    DRAM_WRITE_SENT = 9,
    DRAM_WRITE_TILES_CLEARED = 10,
    DRAM_IO_Q_STATUS = 11,
    STREAM_RESTART = 12,
    STREAM_INFO = 13,
    STREAM_BUF_STATUS = 14,
    EPOCH_Q_EMPTY = 15,
    STREAM_MISC_INFO = 16,
  };

  enum class BriscEventType {
    INPUT_NUM_TILES = 1,
    OUTPUT_NUM_TILES = 2,
    INPUT_TILE_POP = 3,
    OUTPUT_TILE_PUSH = 4,
    STALL_BRISC_FOR_DRAM_PERF_DUMP = 5,
    EPOCH = 6,
  };

  static constexpr int perf_inputs_number_of_bits = 12;

  inline uint32_t get_event_id(uint operand, uint num_tiles, EventType event_type, int outer_loop_idx = 0) {
    uint32_t event_id;
    
    event_id  = (operand          & 0xff);
    event_id |= (num_tiles        & 0xff) << 8;
    event_id |= (uint(event_type) & 0xf)  << 16;
    event_id |= (outer_loop_idx   & 0xfff) << 20;
   
    return event_id;
  }

  // 4B perf dump header
  struct PerfDumpHeader {
    uint8_t   x         : 4;
    uint8_t   y         : 4;
    uint8_t   chip_id;
    uint16_t  thread_id : 3;
    uint16_t  epoch_id  : 13;

  };

  struct PerfDumpHeaderWrap {
    PerfDumpHeader header;

    bool operator<(const perf::PerfDumpHeaderWrap &o)  const {
      const uint32_t this_val = *(reinterpret_cast<const uint32_t*>(&(this->header)));
      const uint32_t other_val = *(reinterpret_cast<const uint32_t*>(&o.header));
      return this_val < other_val;
    }

    inline uint32_t get_uint_from_header() {
      return *(reinterpret_cast<uint32_t*>(&header));
    }

    PerfDumpHeaderWrap(uint32_t* val) {
      header = *(reinterpret_cast<PerfDumpHeader*>(val));
    }
    PerfDumpHeaderWrap() {}
  };

  struct PerfDumpCoreHeader {
    uint8_t x : 4;
    uint8_t y : 4;
    uint8_t chip_id;
    uint16_t epoch_id;
  };

  struct PerfDumpCoreHeaderWrapper {
    PerfDumpCoreHeader core_header;

    bool operator<(const perf::PerfDumpCoreHeaderWrapper &o)  const {
      const uint32_t this_val = *(reinterpret_cast<const uint32_t*>(&(this->core_header)));
      const uint32_t other_val = *(reinterpret_cast<const uint32_t*>(&(o.core_header)));
      return this_val < other_val;
    }

    PerfDumpCoreHeaderWrapper(const PerfDumpHeader& thread_header) {
      core_header.x = thread_header.x;
      core_header.y = thread_header.y;
      core_header.chip_id = thread_header.chip_id;
      core_header.epoch_id = thread_header.epoch_id;
    }
    PerfDumpCoreHeaderWrapper() {}
  };

}
