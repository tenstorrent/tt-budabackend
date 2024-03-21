// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "model/rational_graph/rational_graph.h"

namespace pipegen2
{

class DataFlowGraph;
class DataFlowNode;

class DataFlowGraphCreator
{
public:
    // Creates multiple connected data flow graphs from the given rational graph.
    std::vector<std::unique_ptr<DataFlowGraph>> create_data_flow_graphs(const RationalGraph* rational_graph);

protected:
    // Finds all connecrted components among given data flow nodes and groups them into data flow graphs.
    virtual std::vector<std::unique_ptr<DataFlowGraph>> group_connected_data_flow_nodes(
        std::vector<std::unique_ptr<DataFlowNode>>&& data_flow_nodes);

private:
    // Creates data flow nodes from the nodes in the rational graph.
    std::vector<std::unique_ptr<DataFlowNode>> create_data_flow_nodes(
        const RationalGraph* rational_graph,
        std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node);

    // Creates a single data flow node from rational graph node.
    std::unique_ptr<DataFlowNode> create_data_flow_node(const RGBaseNode* rg_base_node);

    // Connects sources and desinations in data flow graph based on the rational graph connections.
    void create_data_flow_edges(
        const RationalGraph* rational_graph,
        const std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node);

    // Populates DF graph connections from which can be deduced from a single RG pipe.
    void create_data_flow_edges_from_pipe(
        const RGBasePipe* rg_pipe,
        const std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node);
};

} // namespace pipegen2