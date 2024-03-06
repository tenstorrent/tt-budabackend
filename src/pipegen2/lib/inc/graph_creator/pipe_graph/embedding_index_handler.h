// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "graph_creator/pipe_graph/pipe_graph_handler.h"
#include "graph_creator/pipe_graph/pipe_graph_info.h"

namespace pipegen2
{

class PGBuffer;

class EmbeddingIndexHandler : public IPipeGraphHandler
{
public:
    EmbeddingIndexHandler(PipeGraphInfo* pipe_graph_info) : m_pipe_graph_info(pipe_graph_info) 
    {
    }

    // Handle the embbeding indexes in a given pipe graph.
    void handle(PipeGraph& pipe_graph) override;

private:
    // Copies embedding index properties from the embedding index buffer to the embedding table kernel buffer.
    void copy_embedding_index_properties(const PGBuffer* embedding_index_kernel_buffer);

    // Finds embedding index buffer that is on the same core as the embedding table kernel buffer.
    const PGBuffer* find_embedding_index_kernel_buffer(PipeGraph& pipe_graph,
                                                       const PGBuffer* embedding_table_kernel_buffer);

    // A utility object which keeps track of all the helper maps necessary for PipeGraph handling.
    PipeGraphInfo* m_pipe_graph_info;
};

} // namespace pipegen2