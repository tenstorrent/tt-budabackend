// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "device/l1/l1_buffer.h"
#include "device/tt_xy_pair.h"

namespace pipegen2
{

// Represents an allocation of buffer in core's L1 memory which NCRISC will use to store additional structures through
// which it handles reads/writes from/to extra DRAM buffers which could not be handled through structures allocated in
// NCRISC's L0 due to space limitations (thus name "fallback").
class NcriscFallbackL1Buffer : public L1Buffer
{
public:
    NcriscFallbackL1Buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_address, const unsigned int buffer_size) :
        L1Buffer(core_physical_location, buffer_address, buffer_size)
    {
    }

    std::string get_name() const override;

    std::string get_allocation_info() const override;
};

}  // namespace pipegen2