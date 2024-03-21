// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/subgraph_leaf_groups_finder_internal.h"

#include "model/data_flow/data_flow_node.h"
#include "model/data_flow/subgraph_leaf_groups.h"
#include "utils/logger.hpp"

namespace pipegen2::data_flow_internal
{

void find_leaf_subgraphs(
    DataFlowNode* df_node,
    int& subgraph_id,
    std::unordered_map<unsigned int, unsigned int>& max_root_to_leaf_paths_per_subgraph)
{
    if (df_node->has_assigned_leaf_subgraph_id())
    {
        return;
    }

    df_node->set_leaf_subgraph_id(subgraph_id);
    max_root_to_leaf_paths_per_subgraph[subgraph_id] = std::max(df_node->get_num_root_to_leaf_paths_in_subgraph(),
                                                                max_root_to_leaf_paths_per_subgraph[subgraph_id]);

    for (DataFlowNode* dest : df_node->get_destinations())
    {
        find_leaf_subgraphs(dest, subgraph_id, max_root_to_leaf_paths_per_subgraph);
    }

    // After processing DF node with parallel DF type, subgraph id is increased, which means that the next node in
    // the graph (possibly the node's sibling) will get a new subgraph id.
    if (df_node->get_dataflow_type() == DataFlowType::Parallel)
    {
        ++subgraph_id;
    }
}

void find_leaf_groups_per_subgraph(
    DataFlowNode* node,
    std::unordered_map<DataFlowNode*, unsigned int>& node_visit_count,
    unsigned int root_to_leaf_path_index,
    SubgraphLeafGroups* subgraph_leaf_groups)
{
    if (did_process_node_subtree(node, node_visit_count))
    {
        return;
    }

    node_visit_count[node]++;

    if (node->is_leaf_node())
    {
        log_assert(node->has_assigned_leaf_subgraph_id(),
                   "Expecting leaf node {} to have leaf subgraph id assigned", node->get_id());
        subgraph_leaf_groups->add_leaf_node(node, root_to_leaf_path_index);
        return;
    }

    // TODO: For now only one index per path is supported (i.e. a single serial fork per root to leaf path), so
    // override the default one if the current node has path index set, otherwise, propagate the default value (0).
    // Support for multiple serial forks per path could be added by combining multiple indexes per path and using
    // that as a key in a subgrpah leaf cluster.
    root_to_leaf_path_index = node->get_root_to_leaf_path_index().value_or(root_to_leaf_path_index);

    for (DataFlowNode* dest : node->get_destinations())
    {
        find_leaf_groups_per_subgraph(
            dest, node_visit_count, root_to_leaf_path_index, subgraph_leaf_groups);
    }
}

bool did_process_node_subtree(
    DataFlowNode* node,
    std::unordered_map<DataFlowNode*, unsigned int>& node_visit_count)
{
    return node_visit_count[node] >= node->get_number_of_unique_df_paths();
}

} // namespace pipegen2
