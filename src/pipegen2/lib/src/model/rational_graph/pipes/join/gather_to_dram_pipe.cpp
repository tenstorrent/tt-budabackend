// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/join/gather_to_dram_pipe.h"

#include "device/tt_xy_pair.h"

namespace pipegen2
{

std::vector<tt_cxy_pair> GatherToDramPipe::get_ncrisc_writer_streams_locations() const
{
    std::vector<tt_cxy_pair> stream_locations;

    // One stream and one NCRISC config is created for each input of pipe.
    for (const RGBaseNode* input_node : get_unique_input_nodes())
    {
        // At pipe's inputs are VirtualNodes which have same physical locations as packers behind ParallelForkPipes.
        stream_locations.push_back(input_node->get_physical_location());
    }

    return stream_locations;
}

}  // namespace pipegen2