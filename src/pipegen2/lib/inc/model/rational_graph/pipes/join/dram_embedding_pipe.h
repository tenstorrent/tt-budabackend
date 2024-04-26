// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dram_gather_pipe.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/dram_embedding_index_input_node.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/nodes/virtual_node.h"

namespace pipegen2
{
    class DramEmbeddingPipe : public DramGatherPipe
    {
    public:
        DramEmbeddingPipe(RGPipeProperties&& rg_pipe_properties,
                          const std::vector<int>& dram_input_total_readers,
                          const std::vector<int>& dram_input_reader_index,
                          const unsigned int max_dram_input_buffer_size_tiles,
                          const tt_cxy_pair& physical_location) :
            DramGatherPipe(std::move(rg_pipe_properties),
                           dram_input_total_readers,
                           dram_input_reader_index,
                           max_dram_input_buffer_size_tiles,
                           physical_location)
        {
            m_pipe_type = RGPipeType::DramEmbedding;
        }

        // Returns the embedding table input.
        const DramEmbeddingTableInputNode* get_embedding_table() const;

        // Returns the embedding index input.
        const DramEmbeddingIndexInputNode* get_embedding_index() const;

        // Returns the embedding index input.
        const VirtualNode* get_embedding_index_v_node() const;

        // Returns all inputs, excluding the embedding index (last input).
        const std::vector<PipeInput>& get_inputs() const override;

        // Returns the minimum number of tiles to transfer in one chunk. It is derived from the number of embedding
        // indices per input.
        int get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const override;

        // Returns the greatest possible number of tiles to transfer that is divisible by the number of embedding
        // indices per input. Also, if the number of embedding input indices per input is greater than the number of
        // embedding indices per tile, then returns the GCD of the two numbers.
        int get_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const override;

    private:
        // Pipe inputs without the embedding index input.
        mutable std::vector<PipeInput> m_inputs_without_embedding_index;
    };
}