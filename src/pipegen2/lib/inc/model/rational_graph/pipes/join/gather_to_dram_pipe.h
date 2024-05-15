// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "join_pipe.h"
#include "model/rational_graph/pipes/ncrisc_writer_pipe_interface.h"

namespace pipegen2
{
    class GatherToDramPipe : public JoinPipe, public INcriscWriterPipe
    {
    public:
        GatherToDramPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            JoinPipe(RGPipeType::GatherToDram, DataFlowType::Serial, std::move(rg_pipe_properties), physical_location)
        {
        }

        std::vector<tt_cxy_pair> get_ncrisc_writer_streams_locations() const override;
    };
}