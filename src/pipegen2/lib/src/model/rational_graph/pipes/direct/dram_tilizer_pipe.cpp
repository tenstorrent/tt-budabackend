// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/direct/dram_tilizer_pipe.h"

#include "utils/logger.hpp"

namespace pipegen2
{
    const DramEmbeddingTableInputNode* DramTilizerPipe::get_embedding_table() const
    {
        log_assert(m_inputs.size() > 0, "Expecting DramTilizerPipe to have at least 1 input");
        const RGBaseNode* input_node = get_inputs().front().get_non_virtual_node();

        const DramEmbeddingTableInputNode* dram_embedding_table =
            dynamic_cast<const DramEmbeddingTableInputNode*>(input_node);
        log_assert(dram_embedding_table, "Expected DramEmbeddingTableInputNode as first input to DramTilizer pipe");

        return dram_embedding_table;
    }

    unsigned int DramTilizerPipe::get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const
    {
        const DramEmbeddingTableInputNode* dram_embedding_table = get_embedding_table();

        return dram_embedding_table->get_untilized_input_tile_dim_r();
    }
}