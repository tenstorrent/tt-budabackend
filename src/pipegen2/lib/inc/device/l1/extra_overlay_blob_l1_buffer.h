// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "device/l1/l1_buffer.h"
#include "device/tt_xy_pair.h"

namespace pipegen2
{

// Represents an allocation of buffer in core's L1 memory which will be used as extra space for overlay blob.
class ExtraOverlayBlobL1Buffer : public L1Buffer
{
public:
    ExtraOverlayBlobL1Buffer(
        const tt_cxy_pair& core_physical_location, const unsigned int buffer_address, const unsigned int buffer_size) :
        L1Buffer(core_physical_location, buffer_address, buffer_size)
    {
    }

    std::string get_name() const override;

    std::string get_allocation_info() const override;
};

}  // namespace pipegen2