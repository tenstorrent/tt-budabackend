// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"
#include "model/rational_graph/pipes/ncrisc_reader_pipe_interface.h"

namespace pipegen2
{
    class PCIeUnicastPipe : public DirectPipe, public INcriscReaderPipe
    {
    public:
        PCIeUnicastPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            DirectPipe(RGPipeType::PCIeUnicast, std::move(rg_pipe_properties), physical_location)
        {
        }

        std::vector<tt_cxy_pair> get_ncrisc_reader_streams_locations() const override
        {
            // One stream and one NCRISC config will be allocated at unpacker's location.
            return { get_output_node()->get_physical_location() };
        }
    };
}