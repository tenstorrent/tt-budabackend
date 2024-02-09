// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/join/dram_embedding_pipe.h"

#include <numeric>

#include "model/rational_graph/rational_graph.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    const DramEmbeddingTableInputNode* DramEmbeddingPipe::get_embedding_table() const
    {
        log_assert(m_inputs.size() > 1, "Expecting DramEmbeddingPipe to have at least 2 inputs");
        const RGBaseNode* input_node = get_inputs().front().get_non_virtual_node();

        const DramEmbeddingTableInputNode* dram_embedding_table =
            dynamic_cast<const DramEmbeddingTableInputNode*>(input_node);
        log_assert(dram_embedding_table, "Expected DramEmbeddingTableInputNode as first input to DramEmbedding pipe");

        return dram_embedding_table;
    }

    const DramEmbeddingIndexInputNode* DramEmbeddingPipe::get_embedding_index() const
    {
        const VirtualNode* embedding_index_v_node = get_embedding_index_v_node();

        const RGBasePipe* embedding_index_read_pipe = RationalGraph::get_chain_root_pipe(embedding_index_v_node);

        log_assert(embedding_index_read_pipe,
                   "Expecting to find embedding index read pipe as the input to the last input to DramEmbedding pipe");
        log_assert(embedding_index_read_pipe->get_inputs().size() == 1,
                   "Expecting embedding index read pipe to only have one input");

        const RGBaseNode* embedding_read_pipe_input_node = embedding_index_read_pipe->get_inputs()[0].get_node();
        const DramEmbeddingIndexInputNode* embedding_index =
            dynamic_cast<const DramEmbeddingIndexInputNode*>(embedding_read_pipe_input_node);
        log_assert(embedding_index,
                   "Expecting to find DramEmbeddingIndexInputNode as input to the dram embedding index read pipe");

        return embedding_index;
    }

    const VirtualNode* DramEmbeddingPipe::get_embedding_index_v_node() const
    {
        log_assert(m_inputs.size() > 1, "Expecting DramEmbeddingPipe to have at least 2 inputs");
        const RGBaseNode* last_input = m_inputs.back().get_node();
        const VirtualNode* embedding_index = dynamic_cast<const VirtualNode*>(last_input);
        log_assert(embedding_index, "Expected embedding index to be wrapped in virtual node");

        return embedding_index;
    }

    const std::vector<PipeInput>& DramEmbeddingPipe::get_inputs() const
    {
        if (m_inputs_without_embedding_index.empty())
        {
            log_assert(m_inputs.size() > 1, "Expecting DramEmbeddingPipe to have at least 2 inputs");
            m_inputs_without_embedding_index.assign(m_inputs.begin(), m_inputs.end() - 1);
        }
        return m_inputs_without_embedding_index;
    }

    int DramEmbeddingPipe::get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const
    {
        const DramEmbeddingIndexInputNode* embedding_index = get_embedding_index();
        const unsigned int embedding_indices_per_input = embedding_index->get_embedding_indices_per_input();

        const DramEmbeddingTableInputNode* dram_embedding_table = get_embedding_table();
        const unsigned int tile_dim_r = dram_embedding_table->get_untilized_input_tile_dim_r();

        return embedding_indices_per_input < tile_dim_r ? 1 : tile_dim_r;
    }

    int DramEmbeddingPipe::get_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const
    {
        const DramEmbeddingIndexInputNode* embedding_index = get_embedding_index();
        const unsigned int embedding_indices_per_input = embedding_index->get_embedding_indices_per_input();
        const unsigned int embedding_indices_per_tile = embedding_index->get_embedding_indices_per_tile();

        return embedding_indices_per_input > embedding_indices_per_tile ?
            std::gcd(embedding_indices_per_input, embedding_indices_per_tile) :
            embedding_indices_per_input;
    }
}