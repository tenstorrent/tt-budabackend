// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>

#include "l1_address_map.h"

using dram_perf_buf_noc_addr_t = std::array<uint64_t, l1_mem::address_map::PERF_NUM_THREADS>;
using dram_perf_buf_max_req_t = std::array<uint16_t, l1_mem::address_map::PERF_NUM_THREADS>;
using dram_perf_info_t = std::pair<dram_perf_buf_noc_addr_t, dram_perf_buf_max_req_t>;
