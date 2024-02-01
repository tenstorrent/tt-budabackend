// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

namespace pipegen2
{

class DataFlowGraph;
class DataFlowNode;

class ConnectedDataFlowGraphFinder
{
public:
    // Creates a data flow graph for every connected component of data flow nodes. Ownership of data flow nodes is
    // transfered to the corresponding data flow graph.
    std::vector<std::unique_ptr<DataFlowGraph>> group_connected_data_flow_nodes(
        std::vector<std::unique_ptr<DataFlowNode>>&& data_flow_nodes);

private:
    // Creates a data flow graph for every connected component of data flow nodes.
    std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>> create_data_flow_graph_per_connected_component(
        std::vector<std::unique_ptr<DataFlowNode>>& data_flow_nodes,
        const std::unordered_map<const DataFlowNode*, unsigned int>& df_node_to_component);

    // Returns data flow graph which corresponds to the given connected component.
    DataFlowGraph* get_data_flow_graph_for_component(
        unsigned int component_idx,
        std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>>& data_flow_graph_per_component);

    // Finds all the connected data flow nodes and groups them in connected components.
    std::unordered_map<const DataFlowNode*, unsigned int> find_connected_components(
        const std::vector<std::unique_ptr<DataFlowNode>>& data_flow_nodes);

    // Groups all connected data flow nodes in the same connected component.
    void find_connected_data_flow_nodes(
        const DataFlowNode* df_node, 
        unsigned int component_index,
        std::unordered_map<const DataFlowNode*, unsigned int>& df_node_to_component);

    // Moves all created data flow graphs per connected components to a new vector and returns it.
    std::vector<std::unique_ptr<DataFlowGraph>> get_created_data_flow_graphs(
        std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>>&& data_flow_graph_per_component);
};

} // namespace pipegen2
