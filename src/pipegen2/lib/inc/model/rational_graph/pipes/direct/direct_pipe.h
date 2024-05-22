// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/pipes/base_rg_pipe.h"

namespace pipegen2
{
class DirectPipe : public RGBasePipe
{
protected:
    DirectPipe(RGPipeType pipe_type, RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
        RGBasePipe(pipe_type, DataFlowType::ParallelCopy, std::move(rg_pipe_properties), physical_location)
    {
    }
};
}  // namespace pipegen2