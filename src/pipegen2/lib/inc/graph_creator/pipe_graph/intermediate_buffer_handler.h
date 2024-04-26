// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "graph_creator/pipe_graph/pipe_graph_handler.h"
#include "graph_creator/pipe_graph/pipe_graph_info.h"
#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

class IntermediateBufferHandler : public IPipeGraphHandler
{
public:
    IntermediateBufferHandler(PipeGraphInfo* pipe_graph_info) : m_pipe_graph_info(pipe_graph_info) 
    {
    }

    // Handle the creation of intermediate buffers in a given pipe graph.
    void handle(PipeGraph& pipe_graph) override;

private:
    // A utility object which keeps track of all the helper maps necessary for PipeGraph handling.
    PipeGraphInfo* m_pipe_graph_info;
};

} // namespace pipegen2