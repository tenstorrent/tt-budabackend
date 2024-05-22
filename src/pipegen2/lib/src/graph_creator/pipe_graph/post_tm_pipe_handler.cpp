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
    std::vector<std::unique_ptr<PGPipe>> pipes_to_add;

    for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        if (!can_do_post_tm_connections_optimization(pipe.get()))
        {
            continue;
        }

        PGPipe* prefetch_pipe = pipe.get();
        PGBuffer* relay_buffer = prefetch_pipe->get_single_output_buffer();
        PGPipe* relay_pipe = relay_buffer->get_single_output_pipe();

        // Detach input from one pipe to another.
        detach_inputs(prefetch_pipe, relay_pipe);

        // Take some particular attribute values from prefetch_pipe.
        copy_original_post_tm_pipe_attributes(prefetch_pipe, relay_pipe);

        // prefetch_pipe and relay_buffer remain pointing to their inputs/outputs, but they will be removed from pipe
        // graph so no need to detach them explicitly above.
        pipes_to_remove.insert(prefetch_pipe);
        buffers_to_remove.insert(relay_buffer);

        if (can_decompose_post_tm_multicast_pipe(relay_pipe))
        {
            decompose_post_tm_multicast_pipe(pipe_graph, relay_pipe, pipes_to_add);

            // Remove the decomposed pipe.
            pipes_to_remove.insert(relay_pipe);
        }
    }

    // Remove all source pipes and relay buffers from graph that were optimized away in previous optimizations.
    pipe_graph.remove_pipes(pipes_to_remove);
    pipe_graph.remove_buffers(buffers_to_remove);

    // Add the pipes that were newly formed.
    pipe_graph.add_pipes(pipes_to_add);
}

bool PostTMPipeHandler::can_decompose_post_tm_multicast_pipe(const PGPipe* prefetch_post_tm_pipe)
{
    if (prefetch_post_tm_pipe->has_single_output() || !prefetch_post_tm_pipe->is_dram_prefetch_post_tm())
    {
        return false;
    }

    return true;
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

    // Relay buffer can be omitted for the Post TM multicast case since there is no need for multicast as the tiles can
    // directly be prefetched to all destination cores.
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

void PostTMPipeHandler::copy_original_post_tm_pipe_attributes(const PGPipe* original_pipe, PGPipe* new_pipe)
{
    new_pipe->set_pipe_periodic_repeat(original_pipe->get_pipe_periodic_repeat());
    new_pipe->set_incoming_noc_id(original_pipe->get_incoming_noc_id());
    new_pipe->set_incoming_noc_vc(original_pipe->get_incoming_noc_vc());
    new_pipe->set_dram_pipe_total_readers(original_pipe->get_dram_pipe_total_readers());
    new_pipe->set_dram_pipe_reader_index(original_pipe->get_dram_pipe_reader_index());
}

void PostTMPipeHandler::detach_inputs(PGPipe* original_pipe, PGPipe* new_pipe)
{
    // Detach dest pipe from its inputs.
    new_pipe->remove_all_inputs();

    std::unordered_set<PGBuffer*> replaced_buffers;
    for (PGPipe::Input& src_pipe_input : original_pipe->get_inputs())
    {
        PGBuffer* src_pipe_input_buffer = src_pipe_input.get_buffer();

        if (replaced_buffers.find(src_pipe_input_buffer) == replaced_buffers.end())
        {
            if (src_pipe_input_buffer->has_output_pipe_id(original_pipe->get_id()))
            {
                src_pipe_input_buffer->replace_output_pipe(original_pipe, new_pipe);
            }
            new_pipe->add_input_buffer_id(src_pipe_input_buffer->get_id());
            replaced_buffers.insert(src_pipe_input_buffer);
        }
        new_pipe->add_input_buffer(src_pipe_input_buffer, src_pipe_input.get_offset());
    }
}

void PostTMPipeHandler::decompose_post_tm_multicast_pipe(
    PipeGraph& pipe_graph, PGPipe* prefetch_post_tm_pipe, std::vector<std::unique_ptr<PGPipe>>& pipes_to_add)
{
    // Make a new post TM prefetch pipe for every output buffer.
    for (unsigned int output_buffer_index = 0;
         output_buffer_index < prefetch_post_tm_pipe->get_output_buffers()[0].size();
         ++output_buffer_index)
    {
        PGBuffer* output_buffer = prefetch_post_tm_pipe->get_output_buffers()[0][output_buffer_index];
        std::unique_ptr<PGPipe> new_post_tm_prefetch_pipe =
            std::make_unique<PGPipe>(prefetch_post_tm_pipe->get_id() + output_buffer_index + 1);
        new_post_tm_prefetch_pipe->add_output_buffer_id(output_buffer->get_id());
        new_post_tm_prefetch_pipe->add_output_buffer(output_buffer, 0 /* scatter_index */);
        detach_inputs(prefetch_post_tm_pipe, new_post_tm_prefetch_pipe.get());
        copy_original_post_tm_pipe_attributes(prefetch_post_tm_pipe, new_post_tm_prefetch_pipe.get());
        new_post_tm_prefetch_pipe->add_mcast_core_logical_location(
            tt_cxy_pair((output_buffer->get_logical_location())));
        pipes_to_add.push_back(std::move(new_post_tm_prefetch_pipe));
    }
}

}  // namespace pipegen2