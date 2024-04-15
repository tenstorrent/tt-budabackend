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

    // Prefetch Post-TM multicast pipes can be decomposed into multiple unicast pipes, each prefetching input to one of 
    // the destination cores, removing the need to perform multicast on the epoch start.
    bool can_decompose_post_tm_multicast_pipe(const PGPipe* prefetch_post_tm_pipe);

    // Copy the attribute of one pipe to another.
    void copy_original_post_tm_pipe_attributes(const PGPipe* original_pipe, PGPipe* new_pipe);

    //Detach inputs to original_pipe from it and reconnect them as inputs to new_pipe.
    void detach_inputs(PGPipe* original_pipe, PGPipe* new_pipe);

    // Decomposes a single Post-TM multicast pipe into multiple unicast pipes, each prefetching input to one of the 
    // destination cores.
    void decompose_post_tm_multicast_pipe(PipeGraph& pipe_graph, 
                                          PGPipe* prefetch_post_tm_pipe, 
                                          std::vector<std::unique_ptr<PGPipe>>& pipes_to_add);
};

} // namespace pipegen2