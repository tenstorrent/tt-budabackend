// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/intermediate_buffer_handler.h"

#include "graph_creator/pipe_graph/pipe_graph_creator_internal.h"
#include "pipegen2_constants.h"

namespace pipegen2
{

void IntermediateBufferHandler::handle(PipeGraph& pipe_graph)
{
    // In order to fit intermediate buffers into our logic, we need to create a dummy intermed buffer which will be
    // input to a dummy pipe which has the real intermediate buffer as output. This way logic revolves around pipes
    // and data flow, not around lone nodes.
    std::vector<std::unique_ptr<PGBuffer>> newly_created_buffers;

    for (const std::unique_ptr<PGBuffer>& buf : pipe_graph.get_buffers())
    {
        if (!buf->has_no_input() || !buf->has_no_outputs())
        {
            continue;
        }
        // Create dummy input buffer as a clone of this intermediate buf.
        const PGBuffer* pipe_output_buf = buf.get();
        std::unique_ptr<PGBuffer> pipe_input_buf = std::make_unique<PGBuffer>(*pipe_output_buf);
        // TODO not the best way to uniquely identify this buffer. For now it has to be that way until
        // net2pipe is fixed.
        pipe_input_buf->set_id(pipe_output_buf->get_id() - 2);
        m_pipe_graph_info->add_buffer_with_id(pipe_input_buf->get_id(), pipe_input_buf.get());

        // Create dummy pipe to connect the clone and original buf.
        // TODO not the best way to uniquely identify this pipe. For now it has to be that way until net2pipe
        // is fixed.
        std::unique_ptr<PGPipe> pipe = std::make_unique<PGPipe>(pipe_output_buf->get_id() - 1);
        // TODO temporary fix as a way to indicate this is a dummy pipe with invalid location. Planning to move
        // all of this to RG creator.
        pipe->add_mcast_core_logical_location(tt_cxy_pair(0 /* chip_id */,
                                                            constants::unmapped_logical_location,
                                                            constants::unmapped_logical_location));
        pipe->add_input_buffer_id(pipe_input_buf->get_id());
        pipe_graph_creator_internal::connect_pipe_with_input_buffer(pipe.get(), 
                                                                    pipe_input_buf->get_id(), 
                                                                    *m_pipe_graph_info);
        pipe->add_output_buffer_id(pipe_output_buf->get_id());
        pipe_graph_creator_internal::connect_pipe_with_output_buffer(pipe.get(), 
                                                                        0 /* scatter_index */, 
                                                                        pipe_output_buf->get_id(),
                                                                        *m_pipe_graph_info);

        const PGBuffer* shared_buf = pipe_graph.get_shared_output_buffer(buf->get_id());
        if (shared_buf != nullptr)
        {
            // In case this intermediate buffer has an associated (packer) buffer, we also make it an input to
            // this dummy pipe in order to be able to reuse packer stream for intermed stream we create.
            buf->set_shared_space_buffer_id(shared_buf->get_id());
            pipe_input_buf->set_shared_space_buffer_id(shared_buf->get_id());
            pipe->add_input_buffer_id(shared_buf->get_id());
            pipe_graph_creator_internal::connect_pipe_with_input_buffer(pipe.get(), 
                                                                        shared_buf->get_id(), 
                                                                        *m_pipe_graph_info);
        }

        newly_created_buffers.push_back(std::move(pipe_input_buf));
        pipe_graph.add_pipe(std::move(pipe));
    }

    for (std::unique_ptr<PGBuffer>& buf : newly_created_buffers)
    {
        pipe_graph.add_buffer(std::move(buf));
    }
}

} // namespace pipegen2