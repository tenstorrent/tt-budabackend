// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace tt {
namespace constants {

using std::uint32_t;

constexpr uint32_t TILE_HEIGHT = 32;
constexpr uint32_t TILE_WIDTH = 32;

constexpr uint32_t MAX_DST_SIZE = 16;

// Defaults configured to match numodel numpy tile-checks
const double DEFAULT_RTOL = 1e-4;
const double DEFAULT_ATOL = 1e-3;
const double DEFAULT_PCT_MATCHED = 1.0;
const double DEFAULT_PCC_THRESH = 0.999;

constexpr uint32_t LF_MAN_PREC = 4;
constexpr uint32_t FP16_MAN_PREC = 10;
constexpr uint32_t FP16B_MAN_PREC = 7;

constexpr int DEFAULT_PARENT_GRAPH_ID = 0;

// Input stream IDs
const int64_t MIN_INPUT_STREAMID = 0;
const int64_t MAX_INPUT_STREAMID = 7;
// Output stream IDs
const int64_t MIN_OUTPUT_STREAMID = 16;
const int64_t MAX_OUTPUT_STREAMID = 23;
// Intermediate stream IDs
const int64_t MIN_INTERMEDIATE_STREAMID = 24;
const int64_t MAX_INTERMEDIATE_STREAMID = 31;
// Relay stream IDs
const int64_t MIN_RELAY_STREAMID = 32;
const int64_t MAX_RELAY_STREAMID = 63;
// Maximum number of intermediate buffers possible
const int64_t MAX_NUM_INTERMEDIATE_BUFFERS = 8;

}  // namespace constants
}  // namespace tt
