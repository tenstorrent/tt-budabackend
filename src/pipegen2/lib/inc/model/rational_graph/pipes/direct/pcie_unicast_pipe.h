// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"

namespace pipegen2
{
    class PCIeUnicastPipe : public DirectPipe
    {
    public:
        PCIeUnicastPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            DirectPipe(RGPipeType::PCIeUnicast, std::move(rg_pipe_properties), physical_location)
        {
        }
    };
}