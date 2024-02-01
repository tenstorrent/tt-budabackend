// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_rg_node.h"

namespace pipegen2
{
    class BaseInputNode : public RGBaseNode
    {
    public:
        unsigned int get_operand_id() const { return m_operand_id; }

        unsigned int get_num_scatter_chunks() const { return m_num_scatter_chunks; }

        bool is_scatter() const { return m_num_scatter_chunks > 0; }

    protected:
        BaseInputNode(NodeId node_id,
                      RGNodeType node_type,
                      const tt_cxy_pair& physical_location,
                      unsigned int size_tiles,
                      unsigned int tile_size,
                      unsigned int num_epoch_tiles,
                      unsigned int tiles_per_input,
                      unsigned int operand_id,
                      unsigned int num_scatter_chunks,
                      unsigned int scatter_gather_num_tiles):
            RGBaseNode(node_id, node_type, physical_location, size_tiles, tile_size, num_epoch_tiles, tiles_per_input,
                       scatter_gather_num_tiles),
            m_operand_id(operand_id),
            m_num_scatter_chunks(num_scatter_chunks)
        {
        }

        // Operand id, if node is kernel operand input.
        unsigned int m_operand_id;

        // Number of scatter chunks of this input, positive if it is scattered zero otherwise.
        unsigned int m_num_scatter_chunks;
    };
}