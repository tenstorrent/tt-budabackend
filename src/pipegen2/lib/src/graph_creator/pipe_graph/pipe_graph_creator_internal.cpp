// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/pipe_graph_creator_internal.h"

#include "pipegen2_exceptions.h"

namespace pipegen2
{
namespace pipe_graph_creator_internal
{

void connect_pipe_with_input_buffer(PGPipe* pipe, NodeId input_buffer_id, PipeGraphInfo& pipe_graph_info)
{
    if (!pipe_graph_info.buffer_with_id_exists(input_buffer_id))
    {
        throw InvalidPipeGraphSpecificationException(
            "Pipe with id " + std::to_string(pipe->get_id()) + " has inexistent input buffer with id " +
                std::to_string(input_buffer_id),
            pipe->get_mcast_core_logical_locations().at(0));
    }

    PGBuffer* pg_buffer = pipe_graph_info.get_pg_buffer_by_node_id(input_buffer_id);

    pg_buffer->add_output_pipe(pipe);

    if (!pipe_graph_info.scatter_buffer_pipe_input_exists(input_buffer_id))
    {
        pipe->add_input_buffer(pg_buffer);
    }
    else
    {
        pipe->add_input(pipe_graph_info.get_scatter_buffer_pipe_input(input_buffer_id));
    }
}

void connect_pipe_with_output_padding_buffer(
    std::size_t scatter_index, NodeId output_padding_buffer_id, PGPipe* pipe, PipeGraphInfo& pipe_graph_info)
{
    if (!pipe_graph_info.buffer_with_id_exists(output_padding_buffer_id))
    {
        throw InvalidPipeGraphSpecificationException(
            "Pipe with id " + std::to_string(pipe->get_id()) + " has inexistent output padding buffer with id " +
                std::to_string(output_padding_buffer_id),
            pipe->get_mcast_core_logical_locations().at(scatter_index));
    }
    PGBuffer* pg_buffer = pipe_graph_info.get_pg_buffer_by_node_id(output_padding_buffer_id);
    pipe->add_output_padding_buffer(pg_buffer, scatter_index / pipe->get_consumer_repeat());
    pg_buffer->add_output_pipe(pipe);
}

void connect_pipe_with_output_buffer(
    PGPipe* pipe, std::size_t scatter_index, NodeId output_buffer_id, PipeGraphInfo& pipe_graph_info)
{
    if (!pipe_graph_info.buffer_with_id_exists(output_buffer_id))
    {
        throw InvalidPipeGraphSpecificationException(
            "Pipe with id " + std::to_string(pipe->get_id()) + " has inexistent output buffer with id " +
                std::to_string(output_buffer_id),
            pipe->get_mcast_core_logical_locations().at(scatter_index));
    }

    PGBuffer* pg_buffer = pipe_graph_info.get_pg_buffer_by_node_id(output_buffer_id);
    pg_buffer->set_input_pipe(pipe);
    pipe->add_output_buffer(pg_buffer, scatter_index / pipe->get_consumer_repeat());
}

}  // namespace pipe_graph_creator_internal
}  // namespace pipegen2