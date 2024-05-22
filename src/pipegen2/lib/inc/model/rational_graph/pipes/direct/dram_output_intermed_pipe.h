// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"
#include "model/rational_graph/pipes/ncrisc_writer_pipe_interface.h"

namespace pipegen2
{
class DramOutputIntermedPipe : public DirectPipe, public INcriscWriterPipe
{
public:
    DramOutputIntermedPipe(NodeId pipe_id) :
        DirectPipe(RGPipeType::DramOutputIntermed, RGPipeProperties(pipe_id), tt_cxy_pair())
    {
    }

    std::vector<tt_cxy_pair> get_ncrisc_writer_streams_locations() const override
    {
        // One stream and one NCRISC config is created at output intermed node's location.
        return {get_output_node()->get_physical_location()};
    }
};
}  // namespace pipegen2