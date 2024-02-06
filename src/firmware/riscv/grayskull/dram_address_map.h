// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <cstdlib>

#include "l1_address_map.h"

namespace dram_mem {

struct address_map {
    // Sizes

    // Actual memory allocated to each bank for perf is 39896 * 1024.
    // It is reduced below for faster dram perf dump.
    // This can be increased to maximum 39896 * 1024 if more space was needed.
    static constexpr std::int32_t DRAM_EACH_BANK_PERF_BUFFER_SIZE = 4800 * 1024;
    static constexpr std::int32_t DRAM_EACH_BANK_CONST_BLOB_SIZE = 1 * 1024 * 1024;
    static constexpr std::int32_t DRAM_EACH_BANK_CONST_BLOB_ADDR = 6 * 1024 * 1024;
    static constexpr std::int32_t FW_DRAM_BLOCK_SIZE_NO_EXTRAS =
        l1_mem::address_map::TRISC0_SIZE + l1_mem::address_map::TRISC1_SIZE + l1_mem::address_map::TRISC2_SIZE +
        l1_mem::address_map::OVERLAY_BLOB_SIZE + l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE;
    static constexpr std::int32_t DRAM_BARRIER_SIZE = 0x20; // 32 bytes reserved for L1 Barrier
    static std::int32_t OVERLAY_MAX_EXTRA_BLOB_SIZE() {
        char const* env_override = std::getenv("TT_BACKEND_OVERLAY_MAX_EXTRA_BLOB_SIZE");
        if (env_override) {
            std::int32_t max_extra_blob_size = atoi(env_override);
            max_extra_blob_size = (max_extra_blob_size / 128) * 128; // Align to 128 bytes for noc (gs/wh really need 32 but this is to future proof)
            return max_extra_blob_size;
        }
        return 0;
    };

    static std::int32_t OVERLAY_FULL_BLOB_SIZE() {
        return l1_mem::address_map::OVERLAY_BLOB_SIZE + dram_mem::address_map::OVERLAY_MAX_EXTRA_BLOB_SIZE();
    };

    static std::int32_t FW_DRAM_BLOCK_SIZE() {
        return FW_DRAM_BLOCK_SIZE_NO_EXTRAS + dram_mem::address_map::OVERLAY_MAX_EXTRA_BLOB_SIZE();
    };

    // Base addresses

    static constexpr std::int32_t DRAM_EACH_BANK_PERF_BUFFER_BASE = 1024 * 1024;

    static constexpr std::int32_t DRAM_BARRIER_BASE = 0;
    static constexpr std::int32_t TRISC_BASE = 0;
    static constexpr std::int32_t TRISC0_BASE = TRISC_BASE;
    static constexpr std::int32_t TRISC1_BASE = TRISC0_BASE + l1_mem::address_map::TRISC0_SIZE;
    static constexpr std::int32_t TRISC2_BASE = TRISC1_BASE + l1_mem::address_map::TRISC1_SIZE;
    static constexpr std::int32_t EPOCH_RUNTIME_CONFIG_BASE = TRISC2_BASE + l1_mem::address_map::TRISC2_SIZE;
    static constexpr std::int32_t OVERLAY_BLOB_BASE = EPOCH_RUNTIME_CONFIG_BASE + l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE;

    static_assert((TRISC0_BASE + l1_mem::address_map::TRISC0_SIZE) < FW_DRAM_BLOCK_SIZE_NO_EXTRAS);
    static_assert((TRISC1_BASE + l1_mem::address_map::TRISC1_SIZE) < FW_DRAM_BLOCK_SIZE_NO_EXTRAS);
    static_assert((TRISC2_BASE + l1_mem::address_map::TRISC2_SIZE) < FW_DRAM_BLOCK_SIZE_NO_EXTRAS);
    static_assert((OVERLAY_BLOB_BASE + l1_mem::address_map::OVERLAY_BLOB_SIZE) <= FW_DRAM_BLOCK_SIZE_NO_EXTRAS);
    static_assert((EPOCH_RUNTIME_CONFIG_BASE + l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE) < FW_DRAM_BLOCK_SIZE_NO_EXTRAS);
};
}  // namespace dram_mem
