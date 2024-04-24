// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "device/l1/l1_buffer.h"
#include "device/tt_xy_pair.h"
#include "pipegen2_constants.h"

namespace pipegen2 {

// Represents an allocation of buffer in core's L1 memory which will be used as tile header buffer.
class TileHeaderL1Buffer : public L1Buffer {
   public:
    TileHeaderL1Buffer(const tt_cxy_pair& core_physical_location, const unsigned int buffer_address,
                       const unsigned int tile_size)
        : L1Buffer(core_physical_location, buffer_address, constants::tile_header_buffer_size_bytes),
          m_tile_size(tile_size) {}

    std::string get_name() const override;
    
    std::string get_allocation_info() const override;

   private:
    // Size of the tile for which this buffer is allocated.
    unsigned int m_tile_size;
};

}  // namespace pipegen2
