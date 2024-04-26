// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/pipes/base_rg_pipe.h"

namespace pipegen2
{
    class JoinPipe : public RGBasePipe
    {
    protected:
        JoinPipe(RGPipeType pipe_type, DataFlowType dataflow_type, RGPipeProperties&& rg_pipe_properties,
                 const tt_cxy_pair& physical_location) :
            RGBasePipe(pipe_type, dataflow_type, std::move(rg_pipe_properties), physical_location)
        {
        }
    };
}