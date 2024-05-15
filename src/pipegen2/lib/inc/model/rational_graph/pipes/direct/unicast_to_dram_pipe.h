// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"
#include "model/rational_graph/pipes/ncrisc_writer_pipe_interface.h"

namespace pipegen2
{
    class UnicastToDramPipe : public DirectPipe, public INcriscWriterPipe
    {
    public:
        UnicastToDramPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            DirectPipe(RGPipeType::UnicastToDram, std::move(rg_pipe_properties), physical_location)
        {
        }

        std::vector<tt_cxy_pair> get_ncrisc_writer_streams_locations() const override
        {
            // One stream and one NCRISC config is created at packer's location.
            return { get_input_node()->get_physical_location() };
        }
    };
}