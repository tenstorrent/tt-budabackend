// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

namespace eth_l1_mem {


struct address_map {
  // Sizes
  static constexpr std::int32_t FIRMWARE_SIZE = 0;
  static constexpr std::int32_t ERISC_BARRIER_SIZE = 0;
  static constexpr std::int32_t EPOCH_RUNTIME_CONFIG_SIZE = 0;
  static constexpr std::int32_t OVERLAY_BLOB_SIZE = 0;
  static constexpr std::int32_t OVERLAY_MAX_EXTRA_BLOB_SIZE = 0;
  static constexpr std::int32_t TILE_HEADER_BUFFER_SIZE = 0;
  static constexpr std::int32_t NCRISC_L1_EPOCH_Q_SIZE = 0;
  static constexpr std::int32_t FW_L1_BLOCK_SIZE = OVERLAY_BLOB_SIZE + EPOCH_RUNTIME_CONFIG_SIZE + TILE_HEADER_BUFFER_SIZE;
  static constexpr std::int32_t FW_DRAM_BLOCK_SIZE = FIRMWARE_SIZE + OVERLAY_BLOB_SIZE + OVERLAY_MAX_EXTRA_BLOB_SIZE + EPOCH_RUNTIME_CONFIG_SIZE + TILE_HEADER_BUFFER_SIZE;

  static std::int32_t OVERLAY_FULL_BLOB_SIZE() {
      return OVERLAY_BLOB_SIZE + OVERLAY_MAX_EXTRA_BLOB_SIZE;
  };

  // Base addresses
  static constexpr std::int32_t FIRMWARE_BASE = 0;
  static constexpr std::int32_t ERISC_BARRIER_BASE = 0;
  static constexpr std::int32_t L1_DRAM_POLLING_CTRL_BASE = 0;
  static constexpr std::int32_t TILE_HEADER_BUFFER_BASE = 0;
  static constexpr std::int32_t EPOCH_RUNTIME_CONFIG_BASE = FIRMWARE_BASE + FIRMWARE_SIZE + TILE_HEADER_BUFFER_SIZE;
  static constexpr std::int32_t OVERLAY_BLOB_BASE = EPOCH_RUNTIME_CONFIG_BASE + EPOCH_RUNTIME_CONFIG_SIZE;
  static constexpr std::int32_t DATA_BUFFER_SPACE_BASE = EPOCH_RUNTIME_CONFIG_BASE + EPOCH_RUNTIME_CONFIG_SIZE + OVERLAY_BLOB_SIZE;

  static constexpr std::int32_t MAX_SIZE = 0;
  static constexpr std::uint32_t FW_VERSION_ADDR = 0;
  static constexpr std::int32_t L1_EPOCH_Q_BASE = 0; // Epoch Q start in L1.

};
}  // namespace llk

