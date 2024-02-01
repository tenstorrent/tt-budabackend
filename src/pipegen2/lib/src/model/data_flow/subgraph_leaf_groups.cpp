// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/subgraph_leaf_groups.h"

namespace pipegen2
{
    SubgraphLeafGroups::SubgraphLeafGroups(
        const std::unordered_map<unsigned int, unsigned int>& root_to_leaf_paths_per_subgrpah)
    {
        for (const auto& [subgraph_id, num_root_to_leaf_paths_in_subgraph] : root_to_leaf_paths_per_subgrpah)
        {
            m_subgraph_leaves.emplace(subgraph_id, LeafGroupOrder(num_root_to_leaf_paths_in_subgraph));
        }
    }

    LeafGroupOrder::LeafGroupOrder(const unsigned int num_root_to_leaf_paths_in_subgraph)
        : m_leaf_groups(num_root_to_leaf_paths_in_subgraph)
    {
    }

    void LeafGroupOrder::add_leaf_node(DataFlowNode* leaf_node, const unsigned int subgraph_path_index)
    {
        LeafGroup& leaf_group = m_leaf_groups[subgraph_path_index];

        leaf_group.add_leaf_node(leaf_node);
    }

    void SubgraphLeafGroups::add_leaf_node(DataFlowNode* leaf_node, unsigned int subgraph_path_index)
    {
        LeafGroupOrder& subgraph_leaves = m_subgraph_leaves.at(leaf_node->get_leaf_subgraph_id());

        subgraph_leaves.add_leaf_node(leaf_node, subgraph_path_index);
    }
}