// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_intermed_node.h"

namespace pipegen2
{
class NonSharedIntermedNode : public BaseIntermedNode
{
public:
    NonSharedIntermedNode(
        NodeId node_id,
        const tt_cxy_pair& physical_location,
        unsigned size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        unsigned int operand_id,
        unsigned int transfer_granularity,
        unsigned int scatter_gather_num_tiles,
        BlockingParams&& blocking_params) :
        BaseIntermedNode(
            node_id,
            RGNodeType::NonSharedIntermed,
            physical_location,
            size_tiles,
            tile_size,
            num_epoch_tiles,
            operand_id,
            transfer_granularity,
            scatter_gather_num_tiles,
            std::move(blocking_params))
    {
    }
};
}  // namespace pipegen2