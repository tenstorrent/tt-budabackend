// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "graph_creator/pipe_graph/pipe_graph_handler.h"
#include "graph_creator/pipe_graph/pipe_graph_info.h"

namespace pipegen2
{

class PostTMPipeHandler : public IPipeGraphHandler
{
public:
    // Handle the embbeding indexes in a given pipe graph.
    void handle(PipeGraph& pipe_graph) override;

private:
    // Post TM connections can be optimized in a case where we have connections of type
    // (post TM scatter DRAM buffer) -> (single output pipe) -> (post TM relay buffer) -> (single input pipe).
    // In this case we can bypass the relay buffer and merge the pipes into one.
    bool can_do_post_tm_connections_optimization(const PGPipe* src_pipe);
};

} // namespace pipegen2