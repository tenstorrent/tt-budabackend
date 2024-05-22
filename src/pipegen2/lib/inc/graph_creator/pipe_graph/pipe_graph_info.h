// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>

#include "model/pipe_graph/pg_pipe.h"

namespace pipegen2
{

class PipeGraphInfo
{
public:
    // Adds a PGBuffer with its assosicatied NodeId to the m_buffers_per_id map.
    void add_buffer_with_id(NodeId buffer_id, PGBuffer* pg_buffer);

    // Adds a scatter pipe input of a specific PGBuffer to the scatter inputs map, with a specific offset.
    void add_scatter_buffer_pipe_input(
        NodeId node_id, const std::unique_ptr<PGBuffer>& pg_buffer, const unsigned int offset);

    // Checks if a PGBuffer with a specific id exists.
    bool buffer_with_id_exists(NodeId buffer_id) const;

    // Checks if a scatter pipe input with a specific id exists.
    bool scatter_buffer_pipe_input_exists(NodeId buffer_id) const;

    // Gets a PGBuffer object by id, will throw if there is no PgBuffer with that specific id.
    PGBuffer* get_pg_buffer_by_node_id(NodeId buffer_id) const;

    // Gets a PGPipe::Input object from a given id, will throw if there is no input with that specific id.
    PGPipe::Input& get_scatter_buffer_pipe_input(NodeId buffer_id);

private:
    // Pipe inputs from scatter buffers, mapped by scatter buffer ID.
    std::unordered_map<NodeId, PGPipe::Input> m_scatter_buffer_pipe_inputs;

    // Buffers in the pipe graph, mapped by their ID.
    std::unordered_map<NodeId, PGBuffer*> m_buffers_per_id;
};

}  // namespace pipegen2