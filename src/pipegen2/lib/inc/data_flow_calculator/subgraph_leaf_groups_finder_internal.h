// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>

namespace pipegen2
{
class DataFlowNode;
class SubgraphLeafGroups;

namespace data_flow_internal
{
// Groups leaf nodes in a subgraph - leaves which are on different parallel data flow edges are placed in
// different subgraphs as their phase offsets are independent.
void find_leaf_subgraphs(
    DataFlowNode* df_node,
    int& subgraph_id,
    std::unordered_map<unsigned int, unsigned int>& max_root_to_leaf_paths_per_subgraph);

// Finds all leaf nodes in data flow graph and groups them based on the subgraph id the leaf node belongs to and
// the root to leaf path index within that subgraph.
void find_leaf_groups_per_subgraph(
    DataFlowNode* node,
    std::unordered_map<DataFlowNode*, unsigned int>& node_visit_count,
    unsigned int root_to_leaf_path_index,
    SubgraphLeafGroups* subgraph_leaf_groups);

// Returns if subtree of the given node is already processed. A node-rooted subtree is considered processed if
// the node was already visited as many times as there are unique data flow paths going through the node.
bool did_process_node_subtree(
    DataFlowNode* node,
    std::unordered_map<DataFlowNode*, unsigned int>& node_visit_count);
} // namespace data_flow_internal
} // namespace pipegen2
