// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/e2e_queue_handler.h"

namespace pipegen2
{

void E2EQueueHandler::handle(PipeGraph& pipe_graph)
{
    // Create separate buffers for output to DRAM.
    for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        if (!pipe->has_single_output())
        {
            continue;
        }

        PGBuffer* e2e_buffer = pipe->get_single_output_buffer();
        if (!e2e_buffer->is_end_to_end_queue())
        {
            continue;
        }

        std::unique_ptr<PGBuffer> e2e_buffer_replica_for_dram_write = std::make_unique<PGBuffer>(*e2e_buffer);
        // This is okay, net2pipe is spacing out these IDs by couple of bits.
        e2e_buffer_replica_for_dram_write->set_id(e2e_buffer->get_id() + 1);

        // Connect pipe to e2e replica buffer and decouple e2e buffer and the pipe.
        e2e_buffer_replica_for_dram_write->set_input_pipe(pipe.get());
        pipe->replace_output_buffer(e2e_buffer, e2e_buffer_replica_for_dram_write.get());
        e2e_buffer->set_input_pipe(nullptr);

        pipe_graph.add_buffer(std::move(e2e_buffer_replica_for_dram_write));
    }
}

} // namespace pipegen2