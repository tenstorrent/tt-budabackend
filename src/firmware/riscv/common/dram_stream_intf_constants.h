// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _DRAM_STREAM_INTF_CONSTANTS_H_
#define _DRAM_STREAM_INTF_CONSTANTS_H_

#include <stdint.h>

#define MAX_DRAM_INPUT_STREAMS   8
#define MAX_DRAM_OUTPUT_STREAMS  8
#define MAX_L0_DRAM_Q_STATE_T    40
#define MAX_PREFETCH_STREAMS     24

#define DRAM_QUEUE_NO_EPOCH_CHECK 0
#define DRAM_QUEUE_EPOCH_CHECK_EN 0x8000

#define DRAM_STRIDE_UPDATE_WAIT 0x00008000
#define DRAM_STRIDE_WRAP_BIT    0x00004000

#define DRAM_IO_SCATTER_SKIP_TOKEN 0xFFE00000

#define DRAM_STREAMING_SYNC_NUMBER 0xbad0f00d

#define IS_DRAM_PADDING_SET    0x40000000

#define DRAM_SCATTER_LIST_START_OFFSET 16
#define DRAM_SCATTER_LIST_INVALID 0xFFFFFFFF
#define DRAM_IO_IS_SCATTER_LOOP    0x80000000
#define DRAM_IO_IS_SCATTER_LOOP_SIZE 2

#define READ_WORDS_THRESH    750

#define DRAM_Q_RAM (((uint32_t)0x1) << 0)
#define DRAM_Q_INPUT_NOC_FLAG (((uint32_t)0x3) << 1)
#define DRAM_Q_STREAMING_FLAG (((uint32_t)0x1) << 3)
#define DRAM_Q_PTRS_LOADED_FLAG (((uint32_t)0x1) << 4)

#define DRAM_Q_HEADER_ZERO_FLAG (((uint32_t)0x1) << 0)
#define DRAM_Q_DECOUPLED_FLAG (((uint32_t)0x1) << 1)

constexpr uint32_t ZERO_GRAD_CHUNK_SIZE_BYTES = 512;
constexpr uint32_t LOG_ZERO_GRAD_CHUNK_SIZE_BYTES = 9;

constexpr uint32_t DRAM_HEADER_SIZE = 32; //  we are not utilizing eventual padded bytes [no need to copy these from/to dram]

constexpr uint32_t DRAM_PTR_UPDATE_PENDING_MASK = 0x8000;

constexpr uint32_t DRAM_PTR_UPDATE_MASK = (0x1 << 6) - 1;
constexpr uint32_t DRAM_MIN_ENTIRES_POLL = 0;

constexpr uint8_t NULL_STREAM = 0xFF;

constexpr uint32_t EXPECTED_DRAM_INPUT_STREAM_STATE_T_STRUCT_SIZE_WITHOUT_FORKS = 68;
constexpr uint32_t EXPECTED_DRAM_OUTPUT_STREAM_STATE_T_STRUCT_SIZE = 96;
constexpr uint32_t EXPECTED_DRAM_Q_STATE_T_STRUCT_SIZE = 36;

constexpr uint32_t MAX_NCRISC_STRUCTS_SIZE = 2880;

#endif