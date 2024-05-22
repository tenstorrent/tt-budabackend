// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "graph_creator/pipe_graph/pipe_graph_info.h"
#include "model/pipe_graph/pg_pipe.h"

namespace pipegen2
{

namespace pipe_graph_creator_internal
{

// Connects pipe and its input buffer. Updates the pipe graph info.
void connect_pipe_with_input_buffer(PGPipe* pipe, NodeId input_buffer_id, PipeGraphInfo& pipe_graph_info);

// Connects pipe and its output padding buffer. Updates the pipe graph info.
void connect_pipe_with_output_padding_buffer(
    std::size_t scatter_index, NodeId output_padding_buffer_id, PGPipe* pipe, PipeGraphInfo& pipe_graph_info);

// Connects pipe and its output buffer. Updates the pipe graph info.
void connect_pipe_with_output_buffer(
    PGPipe* pipe, std::size_t scatter_index, NodeId output_buffer_id, PipeGraphInfo& pipe_graph_info);

}  // namespace pipe_graph_creator_internal
}  // namespace pipegen2