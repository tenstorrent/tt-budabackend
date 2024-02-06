// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"

namespace pipegen2
{
    class PCIeMulticastPipe : public ForkPipe
    {
    public:
        PCIeMulticastPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            ForkPipe(RGPipeType::PCIeMulticast, DataFlowType::ParallelCopy, std::move(rg_pipe_properties),
                     physical_location)
        {
        }
    };
}