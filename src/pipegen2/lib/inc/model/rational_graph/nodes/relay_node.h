// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_output_node.h"

namespace pipegen2
{
    class RelayNode : public BaseOutputNode
    {
    public:
        RelayNode(NodeId node_id,
                  const tt_cxy_pair& physical_location,
                  unsigned int size_tiles,
                  unsigned int tile_size,
                  unsigned int num_epoch_tiles,
                  unsigned int tiles_per_input,
                  unsigned int operand_id,
                  unsigned int scatter_gather_num_tiles) :
            BaseOutputNode(node_id, RGNodeType::Relay, physical_location, size_tiles, tile_size, num_epoch_tiles,
                           tiles_per_input, operand_id, size_tiles /* transfer_granularity */, scatter_gather_num_tiles)
        {
        }
    };
}