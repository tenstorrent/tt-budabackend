// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/rational_graph/pg_subgraph_finder.h"

namespace pipegen2
{
std::vector<PGSubgraph> PGSubgraphFinder::find_subgraphs(const PipeGraph& pipe_graph)
{
    find_connected_nodes(pipe_graph);
    add_subgraph_nodes(pipe_graph);

    std::vector<PGSubgraph> subgraphs;
    subgraphs.reserve(m_subgraphs_per_id.size());
    for (auto& subgraph_it : m_subgraphs_per_id)
    {
        subgraphs.push_back(std::move(subgraph_it.second));
    }

    return subgraphs;
}

void PGSubgraphFinder::find_connected_nodes(const PipeGraph& pipe_graph)
{
    for (NodeId node_id : pipe_graph.get_all_node_ids())
    {
        // At the beginning each node is in its own group.
        m_node_subgraph_id[node_id] = node_id;
        m_subgraph_size[node_id] = 1;
    }

    // It is enough to iterate through all the pipes and connect them to their input/output buffers,
    // that will create all the subgraph nodes unions.
    for (const std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        for (const PGBuffer* input_buffer : pipe->get_unique_input_buffers_including_output_padding())
        {
            connect_nodes(input_buffer->get_id(), pipe->get_id());
        }
        for (const std::vector<PGBuffer*>& scatter_output_buffers : pipe->get_output_buffers())
        {
            for (const PGBuffer* output_buffer : scatter_output_buffers)
            {
                connect_nodes(output_buffer->get_id(), pipe->get_id());
            }
        }
    }

    // Connect buffers that share L1 space to the same subgraph.
    for (const std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
    {
        if (buffer->shares_l1_space())
        {
            connect_nodes(buffer->get_id(), buffer->get_shared_space_buffer_id());
        }
    }
}

void PGSubgraphFinder::connect_nodes(NodeId node1_id, NodeId node2_id)
{
    NodeId node1_subgraph_id = find_node_subgraph_id(node1_id);
    NodeId node2_subgraph_id = find_node_subgraph_id(node2_id);

    if (node1_subgraph_id == node2_subgraph_id)
    {
        return;
    }

    // "Union by size" optimization for Union Find Disjoint Sets.
    if (m_subgraph_size[node1_subgraph_id] < m_subgraph_size[node2_subgraph_id])
    {
        std::swap(node1_subgraph_id, node2_subgraph_id);
    }
    m_subgraph_size[node1_subgraph_id] += m_subgraph_size[node2_subgraph_id];

    m_node_subgraph_id[node2_subgraph_id] = node1_subgraph_id;
}

NodeId PGSubgraphFinder::find_node_subgraph_id(NodeId node_id)
{
    NodeId node_subraph_id = m_node_subgraph_id[node_id];

    if (node_subraph_id != node_id)
    {
        // "Path compression" optimization for Union Find Disjoint Sets.
        node_subraph_id = find_node_subgraph_id(node_subraph_id);
        m_node_subgraph_id[node_id] = node_subraph_id;
    }

    return node_subraph_id;
}

void PGSubgraphFinder::add_subgraph_nodes(const PipeGraph& pipe_graph)
{
    add_subgraph_buffers(pipe_graph);
    add_subgraph_pipes(pipe_graph);
}

void PGSubgraphFinder::add_subgraph_buffers(const PipeGraph& pipe_graph)
{
    for (const std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
    {
        NodeId subgraph_id = find_node_subgraph_id(buffer->get_id());

        auto subgraph_it = m_subgraphs_per_id.find(subgraph_id);
        if (subgraph_it == m_subgraphs_per_id.end())
        {
            subgraph_it = m_subgraphs_per_id.insert(std::make_pair(subgraph_id, PGSubgraph())).first;
        }

        subgraph_it->second.add_buffer(buffer.get());
    }
}

void PGSubgraphFinder::add_subgraph_pipes(const PipeGraph& pipe_graph)
{
    for (const std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        NodeId subgraph_id = find_node_subgraph_id(pipe->get_id());
        // Subgraph should already exist because it was added for buffers of this pipe.
        m_subgraphs_per_id[subgraph_id].add_pipe(pipe.get());
    }
}
}  // namespace pipegen2