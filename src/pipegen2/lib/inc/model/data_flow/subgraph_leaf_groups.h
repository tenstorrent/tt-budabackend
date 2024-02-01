// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data_flow_node.h"

namespace pipegen2
{
    // Leaves that are time-independent of each other are grouped in the same group. Typically, a leaf group is a
    // group of leaves that are connected to the same output pipe of a serial data flow node.
    class LeafGroup
    {
    public:
        void add_leaf_node(DataFlowNode* leaf_node) { m_leaf_nodes.insert(leaf_node); }

        const std::unordered_set<DataFlowNode*>& get_leaf_nodes() const { return m_leaf_nodes; }

        bool empty() const { return m_leaf_nodes.empty(); }

    private:
        // All leaf nodes which are part of the same group.
        std::unordered_set<DataFlowNode*> m_leaf_nodes;
    };

    // Holds all leaf groups that are ordered between each other within a single subgraph.
    class LeafGroupOrder
    {
    public:
        explicit LeafGroupOrder(const unsigned int num_root_to_leaf_paths_in_subgraph);

        // Adds a leaf node to a leaf group on a given root to leaf path index.
        void add_leaf_node(DataFlowNode* leaf_node, const unsigned int subgraph_path_index);

        const std::vector<LeafGroup>& get_leaf_groups() const { return m_leaf_groups; }

    private:
        // Ordered list of leaf groups within a single subgraph.
        std::vector<LeafGroup> m_leaf_groups;
    };

    // Data structure for storing and managing leaf groups. Leaves from one group are time-independent of leaves in
    // other groups. The only dependency between leaves from different groups is when they are connected to the same
    // input pipe of a serial data flow node. In that case, leaf groups are stored in another data structure which
    // keeps them in order in which they need to be processed.
    class SubgraphLeafGroups
    {
    public:
        explicit SubgraphLeafGroups(
            const std::unordered_map<unsigned int, unsigned int>& root_to_leaf_paths_per_subgrpah);

        // Adds a leaf node to the corresponding subgraph cluster.
        void add_leaf_node(DataFlowNode* leaf_node, unsigned int subgraph_path_index);

        const std::unordered_map<unsigned int, LeafGroupOrder>& get_subgraph_leaves() const
        {
            return m_subgraph_leaves;
        }

    private:
        // Mapping between subgraph id and the corresponding ordered leaf groups.
        std::unordered_map<unsigned int, LeafGroupOrder> m_subgraph_leaves;
    };
}