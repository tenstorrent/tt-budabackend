// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/post_tm_pipe_handler.h"

#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

void PostTMPipeHandler::handle(PipeGraph& pipe_graph)
{
    // If graph is eligible for optimization, some buffers and pipes will be removed from it.
    std::unordered_set<PGPipe*> pipes_to_remove;
    std::unordered_set<PGBuffer*> buffers_to_remove;

    for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        if (!can_do_post_tm_connections_optimization(pipe.get()))
        {
            continue;
        }

        PGPipe* src_pipe = pipe.get();
        PGBuffer* relay_buffer = src_pipe->get_single_output_buffer();
        PGPipe* dest_pipe = relay_buffer->get_single_output_pipe();

        // Detach dest pipe from its inputs.
        dest_pipe->remove_all_inputs();

        std::unordered_set<PGBuffer*> replaced_buffers;

        // Detach inputs to src_pipe from it and reconnect them as inputs to dest_pipe.
        for (PGPipe::Input& src_pipe_input : src_pipe->get_inputs())
        {
            PGBuffer* src_pipe_input_buffer = src_pipe_input.get_buffer();

            if (replaced_buffers.find(src_pipe_input_buffer) == replaced_buffers.end())
            {
                src_pipe_input_buffer->replace_output_pipe(src_pipe, dest_pipe);
                dest_pipe->add_input_buffer_id(src_pipe_input_buffer->get_id());
                replaced_buffers.insert(src_pipe_input_buffer);
            }
            dest_pipe->add_input_buffer(src_pipe_input_buffer, src_pipe_input.get_offset());
        }

        // Take some particular attribute values from src_pipe.
        dest_pipe->set_pipe_periodic_repeat(src_pipe->get_pipe_periodic_repeat());
        dest_pipe->set_incoming_noc_id(src_pipe->get_incoming_noc_id());
        dest_pipe->set_incoming_noc_vc(src_pipe->get_incoming_noc_vc());
        dest_pipe->set_dram_pipe_total_readers(src_pipe->get_dram_pipe_total_readers());
        dest_pipe->set_dram_pipe_reader_index(src_pipe->get_dram_pipe_reader_index());

        // src_pipe and relay_buffer remain pointing to their inputs/outputs, but they will be removed from pipe
        // graph so no need to detach them explicitly above.
        pipes_to_remove.insert(src_pipe);
        buffers_to_remove.insert(relay_buffer);
    }

    // Remove all source pipes and relay buffers from graph that were optimized away in previous optimization.
    pipe_graph.remove_pipes(pipes_to_remove);
    pipe_graph.remove_buffers(buffers_to_remove);
}

bool PostTMPipeHandler::can_do_post_tm_connections_optimization(const PGPipe* src_pipe)
{
    // First pipe in connection has to have a single output and scatter DRAM prefetch post TM inputs.
    if (!src_pipe->has_single_output() || !src_pipe->is_dram_prefetch_post_tm())
    {
        return false;
    }

    // Buffer in between pipes has to be post TM relay non-forked buffer.
    const PGBuffer* middle_buffer = src_pipe->get_single_output_buffer();

    if (!middle_buffer->has_single_output() || !middle_buffer->is_post_tm_relay_buf())
    {
        return false;
    }

    // Second pipe in connection has to have only one input.
    const PGPipe* dest_pipe = middle_buffer->get_single_output_pipe();

    if (!dest_pipe->has_single_input())
    {
        return false;
    }

    return true;
}

} // namespace pipegen2