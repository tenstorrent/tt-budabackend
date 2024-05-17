// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/subgraph_leaf_groups_finder.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "data_flow_calculator/subgraph_leaf_groups_finder_internal.h"
#include "model/data_flow/data_flow_node.h"
#include "model/data_flow/subgraph_leaf_groups.h"

namespace pipegen2
{
std::unique_ptr<SubgraphLeafGroups> SubgraphLeafGroupsFinder::find_leaf_groups(
    const std::vector<DataFlowNode*>& root_nodes)
{
    int next_available_subgraph_id = 0;
    std::unordered_map<unsigned int, unsigned int> max_root_to_leaf_paths_per_subgraph;
    for (DataFlowNode* df_node : root_nodes)
    {
        data_flow_internal::find_leaf_subgraphs(
            df_node, next_available_subgraph_id, max_root_to_leaf_paths_per_subgraph);
    }

    std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups =
        std::make_unique<SubgraphLeafGroups>(max_root_to_leaf_paths_per_subgraph);
    std::unordered_map<DataFlowNode*, unsigned int> node_visit_count;

    for (DataFlowNode* df_node : root_nodes)
    {
        data_flow_internal::find_leaf_groups_per_subgraph(
            df_node, node_visit_count, 0 /* root_to_leaf_path_index */, subgraph_leaf_groups.get());
    }

    return subgraph_leaf_groups;
}
}  // namespace pipegen2
