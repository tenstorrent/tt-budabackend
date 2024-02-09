// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "virtual_node.h"

namespace pipegen2
{
    class SerialForkNode : public VirtualNode
    {
    public:
        SerialForkNode(const RGBaseNode* rg_node, const tt_cxy_pair& physical_location, unsigned int scatter_index) :
            VirtualNode(rg_node, physical_location),
            m_scatter_index(scatter_index)
        {
            m_node_type = RGNodeType::SerialFork;
        }

        // TODO: Move this constructor to unit test mocks.
        SerialForkNode(NodeId node_id, unsigned int scatter_index) :
            VirtualNode(node_id),
            m_scatter_index(scatter_index)
        {
            m_node_type = RGNodeType::SerialFork;
        }

        unsigned int get_scatter_index() const { return m_scatter_index; }

    private:
        // Output order of this node among all outputs of the serial fork pipe.
        unsigned int m_scatter_index;
    };
}