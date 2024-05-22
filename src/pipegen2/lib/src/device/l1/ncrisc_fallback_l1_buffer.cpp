// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/l1/ncrisc_fallback_l1_buffer.h"

namespace pipegen2
{

std::string NcriscFallbackL1Buffer::get_name() const { return "NCRISC fallback buffer"; }

std::string NcriscFallbackL1Buffer::get_allocation_info() const
{
    std::stringstream string_stream;
    string_stream << "\tType: " << get_name() << "\n"
                  << "\tBuffer size: " << get_size() << "\n"
                  << "\tBuffer address: " << get_address();

    return string_stream.str();
}

}  // namespace pipegen2