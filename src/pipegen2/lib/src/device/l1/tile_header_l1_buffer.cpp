// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/l1/tile_header_l1_buffer.h"

namespace pipegen2
{

std::string TileHeaderL1Buffer::get_name() const { return "Tile header buffer"; }

std::string TileHeaderL1Buffer::get_allocation_info() const
{
    std::stringstream string_stream;
    string_stream << "\t\tType: " << get_name() << "\n"
                  << "\t\t\tBuffer size: " << get_size() << "\n"
                  << "\t\t\tBuffer address: " << get_address();

    return string_stream.str();
}

}  // namespace pipegen2