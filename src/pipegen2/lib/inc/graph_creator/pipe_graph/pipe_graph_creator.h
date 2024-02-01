// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/pipe_graph/pipe_graph.h"
#include "model/typedefs.h"

namespace pipegen2
{
    class PipeGraphCreator
    {
    public:
        // Creates pipe graph from the pipegen yaml.
        std::unique_ptr<PipeGraph> create_pipe_graph(const std::string& pipegen_yaml_path);

    private:
        // Creates pipe inputs for scatter buffers and adds them to map per buffer ID.
        void create_pipe_inputs_for_scatter_buffers(const PipeGraph& pipe_graph);

        // Populates map of buffers per ID.
        void populate_buffers_map(PipeGraph& pipe_graph);

        // Adds buffer to map of buffers per ID.
        void add_buffer_to_map_per_id(NodeId buffer_id, PGBuffer* buffer);

        // Finds and sets input and output nodes for each pipe graph node.
        void find_pipe_graph_nodes_connections(PipeGraph& pipe_graph);

        // Finds DRAM embedding pipes and connects them with their embedding index.
        void handle_embedding_connections(PipeGraph& pipe_graph);

        // Finds DRAM buffers which are both input and output to DRAM pipes and replicates them so that we have separate
        // buffers for input and output. This has to be done because those two pipes shouldn't be connected in a single
        // data movement graph since DRAM creates a separation in data movement. It has to be done here in pipe graph
        // creator in order to avoid getting those pipes together in a single subgraph later in rational graph creator.
        void handle_end_to_end_queues(PipeGraph& pipe_graph);

        // Finds embedding index buffer that is on the same core as the embedding table kernel buffer.
        const PGBuffer* find_embedding_index_kernel_buffer(PipeGraph& pipe_graph,
                                                           const PGBuffer* embedding_table_kernel_buffer);

        // Copies embedding index properties from the embedding index buffer to the embedding table kernel buffer.
        void copy_embedding_index_properties(const PGBuffer* embedding_index_kernel_buffer);

        // Handles intermediate buffers which are lone nodes in graph, by creating a pipe and an appropriate input to
        // the pipe and connecting them all together.
        void handle_intermediate_buffers(PipeGraph& pipe_graph);

        // Connects pipe and its input buffer.
        void connect_pipe_with_input_buffer(PGPipe* pipe, NodeId input_buffer_id);

        // Connects pipe and its output padding buffer.
        void connect_pipe_with_output_padding_buffer(std::size_t scatter_index,
                                                     NodeId output_padding_buffer_id,
                                                     PGPipe* pipe);

        // Connects pipe and its output buffer.
        void connect_pipe_with_output_buffer(PGPipe* pipe, std::size_t scatter_index, NodeId output_buffer_id);

        // Optimization for special case of post TM pipes.
        void handle_post_tm_pipes_optimization(PipeGraph& pipe_graph);

        // Post TM connections can be optimized in a case where we have connections of type
        // (post TM scatter DRAM buffer) -> (single output pipe) -> (post TM relay buffer) -> (single input pipe).
        // In this case we can bypass the relay buffer and merge the pipes into one.
        bool can_do_post_tm_connections_optimization(const PGPipe* src_pipe);

        // Value indicating that for a given scatter index, pipe should read from regular inputs instead of the scatter
        // padding buffers.
        static constexpr unsigned int c_no_output_padding_buffer_id = 0;

        // Pipe inputs from scatter buffers, mapped by scatter buffer ID.
        std::unordered_map<NodeId, PGPipe::Input> m_scatter_buffer_pipe_inputs;

        // Buffers in the pipe graph, mapped by their ID.
        std::unordered_map<NodeId, PGBuffer*> m_buffers_per_id;
    };
}