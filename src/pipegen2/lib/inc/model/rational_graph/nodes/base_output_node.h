// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_rg_node.h"

namespace pipegen2
{
class BaseOutputNode : public RGBaseNode
{
public:
    unsigned int get_operand_id() const { return m_operand_id; }

    unsigned int get_transfer_granularity() const { return m_transfer_granularity; }

protected:
    BaseOutputNode(
        NodeId node_id,
        RGNodeType node_type,
        const tt_cxy_pair& physical_location,
        unsigned size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input,
        unsigned int operand_id,
        unsigned int transfer_granularity,
        unsigned int scatter_gather_num_tiles) :
        RGBaseNode(
            node_id,
            node_type,
            physical_location,
            size_tiles,
            tile_size,
            num_epoch_tiles,
            tiles_per_input,
            scatter_gather_num_tiles),
        m_operand_id(operand_id),
        m_transfer_granularity(transfer_granularity)
    {
    }

    // Operand id, if node is kernel operand input.
    unsigned int m_operand_id;

    // Minimal number of tiles output node can receive in one step.
    unsigned int m_transfer_granularity;
};
}  // namespace pipegen2