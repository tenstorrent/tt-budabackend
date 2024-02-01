// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/pipe_graph/pipe_graph.h"
#include "model/typedefs.h"

namespace pipegen2
{
    // Pipe Graph subgraph containing connected buffers and pipes from the pipe graph.
    class PGSubgraph
    {
    public:
        // Gets buffers in this subgraph.
        const std::vector<const PGBuffer*>& get_buffers() const { return m_buffers; }

        // Gets pipes in this subgraph.
        const std::vector<const PGPipe*>& get_pipes() const { return m_pipes; }

        // Adds buffer to this subgraph.
        void add_buffer(const PGBuffer* buffer) { m_buffers.push_back(buffer); }

        // Adds pipe to this subgraph.
        void add_pipe(const PGPipe* pipe) { m_pipes.push_back(pipe); }

    private:
        // Buffers in this subgraph.
        std::vector<const PGBuffer*> m_buffers;

        // Pipes in this subgraph.
        std::vector<const PGPipe*> m_pipes;
    };

    class PGSubgraphFinder
    {
    public:
        // Finds subgraphs of connected buffers and pipes in the pipe graph.
        std::vector<PGSubgraph> find_subgraphs(const PipeGraph& pipe_graph);

    private:
        // Goes through all the nodes in a pipe graph and unions connected nodes into same subgraph group.
        // Uses Union Find Disjoint Set implementation to model subgraph groups.
        void find_connected_nodes(const PipeGraph& pipe_graph);

        // Connects two nodes into same subgraph group.
        void connect_nodes(NodeId node1_id, NodeId node2_id);

        // Finds id of the subgraph that node with the given id belongs to.
        NodeId find_node_subgraph_id(NodeId node_id);

        // Adds each pipe graph node to its subgraph vector of buffers/pipes.
        void add_subgraph_nodes(const PipeGraph& pipe_graph);

        // Adds each pipe graph buffer to its subgraph.
        void add_subgraph_buffers(const PipeGraph& pipe_graph);

        // Adds each pipe graph pipe to its subgraph.
        void add_subgraph_pipes(const PipeGraph& pipe_graph);

        // Mapping node id to the id of the subgraph that node belongs to.
        // Used for creating union find disjoint sets.
        std::unordered_map<NodeId, NodeId> m_node_subgraph_id;

        // Mapping subgraph id to the size of the subgraph.
        // Used for optimizing unions.
        std::unordered_map<NodeId, std::size_t> m_subgraph_size;

        // Mapping subgraph id to the subgraph. Map (instead of unordered_map) needs to be used here to ensure that the
        // order of traversal of elements is always the same, across multiple program runs, which is not guaranteed by
        // the standard for the case of unordered_map and std::hash<T> (hash only has a requirement to produce same
        // input for the same output within a single program run).
        std::map<NodeId, PGSubgraph> m_subgraphs_per_id;
    };
}