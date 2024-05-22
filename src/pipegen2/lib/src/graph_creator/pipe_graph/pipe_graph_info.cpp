// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/pipe_graph_info.h"

#include "pipegen2_exceptions.h"

namespace pipegen2
{

void PipeGraphInfo::add_buffer_with_id(NodeId buffer_id, PGBuffer* pg_buffer)
{
    if (m_buffers_per_id.find(buffer_id) != m_buffers_per_id.end())
    {
        throw InvalidPipeGraphSpecificationException(
            "Buffer with id " + std::to_string(buffer_id) + " already exists", pg_buffer->get_logical_location());
    }

    m_buffers_per_id.emplace(buffer_id, pg_buffer);
}

void PipeGraphInfo::add_scatter_buffer_pipe_input(
    NodeId node_id, const std::unique_ptr<PGBuffer>& pg_buffer, const unsigned int offset)
{
    m_scatter_buffer_pipe_inputs.emplace(node_id, PGPipe::Input(pg_buffer.get(), offset));
}

bool PipeGraphInfo::scatter_buffer_pipe_input_exists(NodeId buffer_id) const
{
    return m_scatter_buffer_pipe_inputs.find(buffer_id) != m_scatter_buffer_pipe_inputs.end();
}

PGBuffer* PipeGraphInfo::get_pg_buffer_by_node_id(NodeId buffer_id) const { return m_buffers_per_id.at(buffer_id); }

PGPipe::Input& PipeGraphInfo::get_scatter_buffer_pipe_input(NodeId buffer_id)
{
    return m_scatter_buffer_pipe_inputs.at(buffer_id);
}

bool PipeGraphInfo::buffer_with_id_exists(NodeId buffer_id) const
{
    return m_buffers_per_id.find(buffer_id) != m_buffers_per_id.end();
}

}  // namespace pipegen2