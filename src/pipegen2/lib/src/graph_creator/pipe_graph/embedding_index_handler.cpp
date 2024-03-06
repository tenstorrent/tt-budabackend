// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/pipe_graph/embedding_index_handler.h"


#include "graph_creator/pipe_graph/pipe_graph_creator_internal.h"
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pipe_graph.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

void EmbeddingIndexHandler::handle(PipeGraph& pipe_graph)
{
    for (std::unique_ptr<PGPipe>& pipe : pipe_graph.get_pipes())
    {
        const PGBuffer* input_buffer = pipe->get_inputs()[0].get_buffer();
        if (!input_buffer->is_embedding_table())
        {
            continue;
        }
        log_assert(pipe->get_output_buffers().size() == 1, "Embedding table pipe should have only one output");
        const PGBuffer* embedding_table_kernel_buffer = pipe->get_output_buffers()[0][0];
        const PGBuffer* embedding_index_kernel_buffer = find_embedding_index_kernel_buffer(
            pipe_graph, embedding_table_kernel_buffer);
        log_assert(embedding_index_kernel_buffer != nullptr,
                    "Could not find embedding index kernel buffer for embedding table kernel buffer");
        pipe_graph_creator_internal::connect_pipe_with_input_buffer(pipe.get(), 
                                                                    embedding_index_kernel_buffer->get_id(),
                                                                    *m_pipe_graph_info);
        copy_embedding_index_properties(embedding_index_kernel_buffer);
    }
}

void EmbeddingIndexHandler::copy_embedding_index_properties(const PGBuffer* embedding_index_kernel_buffer)
{
    const PGPipe* embedding_index_pipe = embedding_index_kernel_buffer->get_input_pipe();
    log_assert(embedding_index_pipe != nullptr,
                "Could not find embedding index pipe for embedding index kernel buffer");
    log_assert(embedding_index_pipe->get_inputs().size() == 1,
                "Embedding index pipe should have only one input");
    const PGBuffer* embedding_index_dram_buffer = embedding_index_pipe->get_inputs()[0].get_buffer();

    log_assert(m_pipe_graph_info->buffer_with_id_exists(embedding_index_kernel_buffer->get_id()),
                "Could not find embedding index kernel buffer in buffers map");
    PGBuffer* embedding_index_kernel_buffer_non_const = m_pipe_graph_info->get_pg_buffer_by_node_id(
        embedding_index_kernel_buffer->get_id());
    embedding_index_kernel_buffer_non_const->set_embedding_index(1);
    embedding_index_kernel_buffer_non_const->set_embedding_indices_per_input(
        embedding_index_dram_buffer->get_embedding_indices_per_input());
    embedding_index_kernel_buffer_non_const->set_embedding_indices_per_tile(
        embedding_index_dram_buffer->get_embedding_indices_per_tile());
}

const PGBuffer* EmbeddingIndexHandler::find_embedding_index_kernel_buffer(
    PipeGraph& pipe_graph,
    const PGBuffer* embedding_table_kernel_buffer)
{
    // Go over buffers in pipe_graph and find buffer on same location as embedding_table_kernel_buffer but with
    // different id and that is input_operand
    for (std::unique_ptr<PGBuffer>& buffer : pipe_graph.get_buffers())
    {
        if (buffer->get_id() == embedding_table_kernel_buffer->get_id())
        {
            continue;
        }

        if (buffer->get_logical_location() == embedding_table_kernel_buffer->get_logical_location() &&
            buffer->is_input_operand())
        {
            return buffer.get();
        }
    }

    return nullptr;
}

} // namespace pipegen2