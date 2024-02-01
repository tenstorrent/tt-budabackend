// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "join_pipe.h"

namespace pipegen2
{
    class GatherToPCIePipe : public JoinPipe
    {
    public:
        GatherToPCIePipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            JoinPipe(RGPipeType::GatherToPCIe, DataFlowType::Serial, std::move(rg_pipe_properties), physical_location)
        {
        }
    };
}