// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/connected_data_flow_graph_finder.h"

#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_node.h"
#include "utils/logger.hpp"

namespace pipegen2
{
    
std::vector<std::unique_ptr<DataFlowGraph>> ConnectedDataFlowGraphFinder::group_connected_data_flow_nodes(
    std::vector<std::unique_ptr<DataFlowNode>>&& data_flow_nodes)
{
    std::unordered_map<const DataFlowNode*, unsigned int> df_node_to_component = 
        find_connected_components(data_flow_nodes);
    log_assert(
        df_node_to_component.size() == data_flow_nodes.size(), "Expecting all data flow nodes to be visited");

    std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>> data_flow_graph_per_component =
        create_data_flow_graph_per_connected_component(data_flow_nodes, df_node_to_component);

    return get_created_data_flow_graphs(std::move(data_flow_graph_per_component));
}

std::unordered_map<const DataFlowNode*, unsigned int> ConnectedDataFlowGraphFinder::find_connected_components(
    const std::vector<std::unique_ptr<DataFlowNode>>& data_flow_nodes)
{
    std::unordered_map<const DataFlowNode*, unsigned int> df_node_to_component;
    unsigned int component_index = 0;

    for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_nodes)
    {
        find_connected_data_flow_nodes(df_node.get(), component_index++, df_node_to_component);
    }

    return df_node_to_component;
}

void ConnectedDataFlowGraphFinder::find_connected_data_flow_nodes(
    const DataFlowNode* df_node, 
    unsigned int component_index,
    std::unordered_map<const DataFlowNode*, unsigned int>& df_node_to_component)
{
    if (df_node_to_component.count(df_node))
    {
        return;
    }

    df_node_to_component[df_node] = component_index;

    for (const DataFlowNode* source : df_node->get_sources())
    {
        find_connected_data_flow_nodes(source, component_index, df_node_to_component);
    }

    for (const DataFlowNode* dest : df_node->get_destinations())
    {
        find_connected_data_flow_nodes(dest, component_index, df_node_to_component);
    }
}

std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>> 
ConnectedDataFlowGraphFinder::create_data_flow_graph_per_connected_component(
    std::vector<std::unique_ptr<DataFlowNode>>& data_flow_nodes,
    const std::unordered_map<const DataFlowNode*, unsigned int>& df_node_to_component)
{
    std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>> data_flow_graph_per_component;
    for (std::unique_ptr<DataFlowNode>& df_node : data_flow_nodes)
    {
        const unsigned int component_idx = df_node_to_component.at(df_node.get());
        DataFlowGraph* df_graph = get_data_flow_graph_for_component(component_idx, data_flow_graph_per_component);

        df_graph->add_node(std::move(df_node));
    }

    return data_flow_graph_per_component;
}

DataFlowGraph* ConnectedDataFlowGraphFinder::get_data_flow_graph_for_component(
    unsigned int component_idx,
    std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>>& data_flow_graph_per_component)
{
    auto it = data_flow_graph_per_component.find(component_idx);
    if (it == data_flow_graph_per_component.end())
    {
        it = data_flow_graph_per_component.emplace(component_idx, std::make_unique<DataFlowGraph>()).first;
    }

    return it->second.get();
}

std::vector<std::unique_ptr<DataFlowGraph>> ConnectedDataFlowGraphFinder::get_created_data_flow_graphs(
    std::unordered_map<unsigned int, std::unique_ptr<DataFlowGraph>>&& data_flow_graph_per_component)
{
    std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs;
    for (auto& [component_idx, component_df_graph] : data_flow_graph_per_component)
    {
        connected_data_flow_graphs.emplace_back(std::move(component_df_graph));
    }

    return connected_data_flow_graphs;
}

} // namespace pipegen2
