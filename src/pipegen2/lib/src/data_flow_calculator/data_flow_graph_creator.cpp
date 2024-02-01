// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/data_flow_graph_creator.h"

#include "data_flow_calculator/connected_data_flow_graph_finder.h"
#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_node.h"

namespace pipegen2
{

std::vector<std::unique_ptr<DataFlowGraph>> DataFlowGraphCreator::create_data_flow_graphs(
    const RationalGraph* rational_graph)
{
    std::unordered_map<const RGBaseNode*, DataFlowNode*> rg_node_to_df_node;
    std::vector<std::unique_ptr<DataFlowNode>> data_flow_nodes = create_data_flow_nodes(
        rational_graph, rg_node_to_df_node);

    create_data_flow_edges(rational_graph, rg_node_to_df_node);

    return group_connected_data_flow_nodes(std::move(data_flow_nodes));
}

std::vector<std::unique_ptr<DataFlowNode>> DataFlowGraphCreator::create_data_flow_nodes(
    const RationalGraph* rational_graph,
    std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node)
{
    std::vector<std::unique_ptr<DataFlowNode>> data_flow_nodes;
    for (const std::unique_ptr<RGBaseNode>& rg_node : rational_graph->get_nodes())
    {
        data_flow_nodes.emplace_back(create_data_flow_node(rg_node.get()));
        rg_node_to_df_node.emplace(rg_node.get(), data_flow_nodes.back().get());
    }

    return data_flow_nodes;
}

std::unique_ptr<DataFlowNode> DataFlowGraphCreator::create_data_flow_node(const RGBaseNode* rg_base_node)
{
    return std::make_unique<DataFlowNode>(rg_base_node);
}

void DataFlowGraphCreator::create_data_flow_edges(
    const RationalGraph* rational_graph,
    const std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node)
{
    for (const std::unique_ptr<RGBasePipe>& rg_pipe : rational_graph->get_pipes())
    {
        create_data_flow_edges_from_pipe(rg_pipe.get(), rg_node_to_df_node);
    }
}

void DataFlowGraphCreator::create_data_flow_edges_from_pipe(
    const RGBasePipe* rg_pipe,
    const std::unordered_map<const RGBaseNode*, DataFlowNode*>& rg_node_to_df_node)
{
    std::unordered_set<DataFlowNode*> seen_source_nodes;
    for (const PipeInput& pipe_input : rg_pipe->get_inputs())
    {
        const RGBaseNode* rg_node = pipe_input.get_node();
        DataFlowNode* source_node = rg_node_to_df_node.at(rg_node);

        for (const RGBaseNode* pipe_out_node : rg_pipe->get_output_nodes())
        {
            DataFlowNode* dest_node = rg_node_to_df_node.at(pipe_out_node);
            dest_node->add_input(source_node, pipe_input.get_offset());

            // Prevent creating multiple source -> dest edges in the data flow graph if the source is appearing
            // multiple times in the pipe's input list.
            if (!seen_source_nodes.count(source_node))
            {
                source_node->add_dest(dest_node);
                dest_node->add_source(source_node);
            }
        }
        seen_source_nodes.insert(source_node);
    }
}

std::vector<std::unique_ptr<DataFlowGraph>> DataFlowGraphCreator::group_connected_data_flow_nodes(
    std::vector<std::unique_ptr<DataFlowNode>>&& data_flow_nodes)
{
    ConnectedDataFlowGraphFinder connected_data_flow_graph_finder;
    return connected_data_flow_graph_finder.group_connected_data_flow_nodes(std::move(data_flow_nodes));
}

} // namespace pipegen2
