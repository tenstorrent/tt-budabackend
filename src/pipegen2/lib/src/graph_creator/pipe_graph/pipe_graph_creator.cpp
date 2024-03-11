// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/pipe_graph_creator.h"
#include <memory>


#include "graph_creator/pipe_graph/e2e_queue_handler.h"
#include "graph_creator/pipe_graph/embedding_index_handler.h"
#include "graph_creator/pipe_graph/intermediate_buffer_handler.h"
#include "graph_creator/pipe_graph/pipe_graph_creator_internal.h"
#include "graph_creator/pipe_graph/post_tm_pipe_handler.h"
#include "io/pipe_graph_parser.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

std::unique_ptr<PipeGraph> PipeGraphCreator::create_pipe_graph(const std::string& pipegen_yaml_path)
{
    std::unique_ptr<PipeGraph> pipe_graph = std::make_unique<PipeGraph>();

    PipeGraphParser::parse_graph(*pipe_graph, pipegen_yaml_path);

    create_pipe_inputs_for_scatter_buffers(*pipe_graph);
    populate_buffers_map(*pipe_graph);
    find_pipe_graph_nodes_connections(*pipe_graph);

    // Populate the handler vector and do the necessary modifications of the created pipe graph.
    patch_deficiencies(*pipe_graph);

    return pipe_graph;
}

void PipeGraphCreator::create_pipe_inputs_for_scatter_buffers(const PipeGraph& pipe_graph)
{
    for (const std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
    {
        if (!buffer->is_scatter())
        {
            continue;
        }

        m_pipe_graph_info.add_scatter_buffer_pipe_input(buffer->get_id(), buffer, 0 /* offset */);
        for (unsigned int i = 1; i < buffer->get_replicate_count(); ++i)
        {
            unsigned int offset = i * buffer->get_scatter_gather_num_tiles();
            NodeId id = buffer->get_id() + offset;
            m_pipe_graph_info.add_scatter_buffer_pipe_input(id, buffer, offset);
            m_pipe_graph_info.add_buffer_with_id(id, buffer.get());
        }
    }
}

void PipeGraphCreator::populate_buffers_map(PipeGraph& pipe_graph)
{
    for (std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
    {
        m_pipe_graph_info.add_buffer_with_id(buffer->get_id(), buffer.get());
    }
}

void PipeGraphCreator::find_pipe_graph_nodes_connections(PipeGraph& pipe_graph)
{
    for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        for (NodeId buffer_id : pipe->get_input_buffers_ids())
        {
            pipe_graph_creator_internal::connect_pipe_with_input_buffer(pipe.get(), buffer_id, m_pipe_graph_info);
        }

        const std::vector<NodeId>& output_padding_buffers_ids = pipe->get_output_padding_buffers_ids();
        for (std::size_t scatter_index = 0; scatter_index < output_padding_buffers_ids.size();
                scatter_index += pipe->get_consumer_repeat())
        {
            const NodeId buffer_id = output_padding_buffers_ids[scatter_index];
            if (buffer_id == PipeGraph::c_no_output_padding_buffer_id)
            {
                continue;
            }
            pipe_graph_creator_internal::connect_pipe_with_output_padding_buffer(scatter_index, 
                                                                                 buffer_id, 
                                                                                 pipe.get(), 
                                                                                 m_pipe_graph_info);
        }

        const std::vector<std::vector<NodeId>>& output_buffer_ids = pipe->get_output_buffers_ids();
        for (std::size_t scatter_index = 0; scatter_index < output_buffer_ids.size();
                scatter_index += pipe->get_consumer_repeat())
        {
            for (NodeId buffer_id : output_buffer_ids[scatter_index])
            {
                pipe_graph_creator_internal::connect_pipe_with_output_buffer(pipe.get(), 
                                                                             scatter_index, 
                                                                             buffer_id, 
                                                                             m_pipe_graph_info);
            }
        }
    }
}

void PipeGraphCreator::patch_deficiencies(PipeGraph& pipe_graph)
{   
    std::vector<std::unique_ptr<IPipeGraphHandler>> pipe_graph_handlers;
    pipe_graph_handlers.push_back(std::move(
        std::make_unique<EmbeddingIndexHandler>(EmbeddingIndexHandler(&m_pipe_graph_info))));
    pipe_graph_handlers.push_back(std::move(
        std::make_unique<IntermediateBufferHandler>(IntermediateBufferHandler(&m_pipe_graph_info))));
    pipe_graph_handlers.push_back(std::move(
        std::make_unique<E2EQueueHandler>(E2EQueueHandler())));
    pipe_graph_handlers.push_back(std::move(
        std::make_unique<PostTMPipeHandler>(PostTMPipeHandler())));

    for (const std::unique_ptr<IPipeGraphHandler>& pipe_graph_handler : pipe_graph_handlers)
    {
        pipe_graph_handler->handle(pipe_graph);
    }
}

} // namespace pipegen2
