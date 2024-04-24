// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once


constexpr int TILE_HEADER_SIZE = 16;

constexpr int MAX_STREAMS_USED_FOR_OPTIMIZED_GATHER = 3;

constexpr int OUTPUT_BUFFER_STREAM_START = 16;
constexpr int INTERMEDIATE_BUFFER_STREAM = 24;

static constexpr int RELAY_BUFFER_STREAM_FIRST = 33;
static constexpr int RELAY_BUFFER_STREAM_LAST = 64;
static constexpr int RELAY_BUFFER_LIMIT = RELAY_BUFFER_STREAM_LAST - RELAY_BUFFER_STREAM_FIRST;
static constexpr int NUM_BUFFER_STREAMS = 64;

static constexpr int MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL = 3;
static constexpr int MAX_MCAST_STREAMS_PER_CORE = 4;
static constexpr int MAX_DRAM_IO_INPUT_STREAMS = 8;
static constexpr int MAX_DRAM_IO_OUTPUT_STREAMS = 8;

static constexpr int NET2PIPE_STREAM_MEM_PIPE_PER_CORE = 56 * 1024;
static constexpr int PIPEGEN_STREAM_MEM_PIPE_PER_CORE = 200 * 1024;
static constexpr int PIPEGEN_STREAM_MEM_PIPE_PER_CORE_ETHERNET = 64 * 1024;

static constexpr int PIPE_REDUCE_MEM_USAGE_THRESHOLD_ETHERNET = 32 * 1024;
static constexpr int DRAM_INPUT_STREAM_MAX_PENDING_READ_BYTES = 64 * 1024;

static constexpr int EXTRA_STREAM_ID_START = 40;
static constexpr int MAX_EXTRA_STREAMS_PER_CORE = 64 - EXTRA_STREAM_ID_START;

static constexpr int ETH_GATHER_STREAMED_READ_MAX_INPUT_BUFFER_TILES = 12;
static constexpr int ETH_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES = 4;
static constexpr int WORKER_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES = 4;
static constexpr int ETH_SUM_GATHER_INPUT_MIN_SIZE_TILES = 8;
static constexpr int SUM_GATHER_INPUT_MIN_SIZE_TILES = 12;

static constexpr int TILE_HEADER_SIZE_BYTES = 16;

// Ethernet Constants -- NEED TO MOVE THESE CONSTANTS TO ARCH SPECIFIC LOOKUP TABLES
static constexpr int MAX_NUM_NON_MCAST_ETH_STREAMS = 27;
static constexpr int MAX_NUM_ETH_STREAMS_TOTAL = MAX_NUM_NON_MCAST_ETH_STREAMS + MAX_MCAST_STREAMS_PER_CORE;
static constexpr int MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL = 7;
static_assert(MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL <= MAX_NUM_ETH_STREAMS_TOTAL, "MAX_NUM_ETH_STREAMS_TOTAL must be atleast as large as MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL");

// TODO: commonize this to an interface that pipegen can use. This will be presented as a queryable
//       as a statically type interface
static constexpr int MAX_TILES_MSG_INFO_BUF_PER_PHASE = 2048; 

  static constexpr int KERNEL_INPUT_MIN_LATENCY_CYCLES = 1024; // Keep KERNEL_INPUT_MIN_LATENCY_CYCLES*NOC_BW_BYTES_PER_CYCLE as a power of 2 so we can enable some optimizations in pipegen