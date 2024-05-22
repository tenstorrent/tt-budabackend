// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dram_unicast_pipe.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"

namespace pipegen2
{
class DramTilizerPipe : public DramUnicastPipe
{
public:
    DramTilizerPipe(
        RGPipeProperties&& rg_pipe_properties,
        const std::vector<int>& dram_input_total_readers,
        const std::vector<int>& dram_input_reader_index,
        const unsigned int max_dram_input_buffer_size_tiles,
        const tt_cxy_pair& physical_location,
        unsigned int tilize_row_col_offset) :
        DramUnicastPipe(
            std::move(rg_pipe_properties),
            dram_input_total_readers,
            dram_input_reader_index,
            max_dram_input_buffer_size_tiles,
            physical_location),
        m_tilize_row_col_offset(tilize_row_col_offset)
    {
        m_pipe_type = RGPipeType::DramTilizer;
    }

    // Returns the embedding table input.
    const DramEmbeddingTableInputNode* get_embedding_table() const;

    // Returns the minimum number of tiles to transfer in one chunk.
    unsigned int get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const override;

    unsigned int get_tilize_row_col_offset() const { return m_tilize_row_col_offset; }

private:
    // TODO: Math on this in net2pipe is pretty unclear, figure out what this represents.
    unsigned int m_tilize_row_col_offset;
};
}  // namespace pipegen2