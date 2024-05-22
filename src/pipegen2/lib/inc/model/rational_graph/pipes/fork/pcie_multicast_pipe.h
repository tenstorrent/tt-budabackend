// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"
#include "model/rational_graph/pipes/ncrisc_reader_pipe_interface.h"

namespace pipegen2
{
class PCIeMulticastPipe : public ForkPipe, public INcriscReaderPipe
{
public:
    PCIeMulticastPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
        ForkPipe(
            RGPipeType::PCIeMulticast, DataFlowType::ParallelCopy, std::move(rg_pipe_properties), physical_location)
    {
    }

    std::vector<tt_cxy_pair> get_ncrisc_reader_streams_locations() const override
    {
        // One stream and one NCRISC config will be allocated at pipe's location. From that location data is mcasted
        // to other cores.
        return {get_physical_location()};
    }
};
}  // namespace pipegen2