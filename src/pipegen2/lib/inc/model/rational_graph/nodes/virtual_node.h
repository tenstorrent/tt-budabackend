// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_rg_node.h"

namespace pipegen2
{
class VirtualNode : public RGBaseNode
{
public:
    VirtualNode(
        const tt_cxy_pair& physical_location,
        unsigned int size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input) :
        RGBaseNode(-1, RGNodeType::Virtual, physical_location, size_tiles, tile_size, num_epoch_tiles, tiles_per_input),
        m_parent_rg_node(nullptr)
    {
    }

    explicit VirtualNode(const RGBaseNode* rg_node) : VirtualNode(rg_node, rg_node->get_physical_location()) {}

    VirtualNode(const RGBaseNode* rg_node, const tt_cxy_pair& physical_location) :
        RGBaseNode(
            -1,
            RGNodeType::Virtual,
            physical_location,
            rg_node->get_size_tiles(),
            rg_node->get_tile_size(),
            rg_node->get_num_epoch_tiles(),
            rg_node->get_tiles_per_input(),
            rg_node->get_scatter_gather_num_tiles()),
        m_parent_rg_node(rg_node)
    {
    }

    // TODO: Move this constructor to unit test mocks.
    explicit VirtualNode(NodeId node_id) :
        RGBaseNode(node_id, RGNodeType::Virtual, tt_cxy_pair(), 0, 0, 0, 0), m_parent_rg_node(nullptr)
    {
    }

    explicit VirtualNode(const tt_cxy_pair& physical_location) :
        RGBaseNode(-1, RGNodeType::Virtual, physical_location, 0, 0, 0, 0), m_parent_rg_node(nullptr)
    {
    }

    const RGBaseNode* get_parent_rg_node() const { return m_parent_rg_node; }

    // Checks if node is virtual and returns the node which it has replaced in the rational graph.
    static const RGBaseNode* get_non_virtual_node(const RGBaseNode* rg_node)
    {
        const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(rg_node);
        return virtual_node == nullptr ? rg_node : virtual_node->get_parent_rg_node();
    }

private:
    // Pointer to the node which this virtual node has replaced in the rational graph. This is possible due to
    // forks. Pointer may be nullptr when virtual node is not part of fork subgraph.
    const RGBaseNode* m_parent_rg_node;
};
}  // namespace pipegen2