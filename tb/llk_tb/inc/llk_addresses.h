// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "l1_address_map.h"
#include "dram_address_map.h"

namespace llk {

struct test_address_map {
    static constexpr std::int32_t INPUT_DATA_SIZE = 256 * 1024;
    static constexpr std::int32_t TEST_NOC_STIMULUS_SIZE = 256 * 1024;
    static constexpr std::int32_t OUTPUT_DATA_SIZE = 256 * 1024;
    static constexpr std::int32_t INPUT_DATA_OFFSET = 128 * 1024;
    static constexpr std::int32_t EXTRA_TILE_HEADER_BUFFER_SIZE = 2 * 32 * 1024; // This is enough space for all 3 data types (bfp8, fp16, fp32). If we use more this should be increased.

    static constexpr std::int32_t INPUT_DATA_BASE = l1_mem::address_map::DATA_BUFFER_SPACE_BASE + EXTRA_TILE_HEADER_BUFFER_SIZE;
    static constexpr std::int32_t INPUT_A_DATA_BASE = INPUT_DATA_BASE;
    static constexpr std::int32_t INPUT_B_DATA_BASE = INPUT_DATA_BASE + INPUT_DATA_OFFSET;
    static constexpr std::int32_t TEST_NOC_STIMULUS_BASE = INPUT_DATA_BASE + INPUT_DATA_SIZE;
    static constexpr std::int32_t OUTPUT_DATA_BASE = INPUT_DATA_BASE + TEST_NOC_STIMULUS_SIZE;
};
}  // namespace llk
