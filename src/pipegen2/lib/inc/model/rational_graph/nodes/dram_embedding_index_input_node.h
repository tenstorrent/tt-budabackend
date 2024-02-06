// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dram_input_node.h"

namespace pipegen2
{
    class DramEmbeddingIndexInputNode : public DramInputNode
    {
    public:
        DramEmbeddingIndexInputNode(NodeId node_id,
                                    const tt_cxy_pair& physical_location,
                                    unsigned int size_tiles,
                                    unsigned int tile_size,
                                    unsigned int num_epoch_tiles,
                                    unsigned int tiles_per_input,
                                    unsigned int operand_id,
                                    unsigned int num_scatter_chunks,
                                    unsigned int scatter_gather_num_tiles,
                                    unsigned int num_queue_slots,
                                    bool is_ram,
                                    unsigned long dram_address,
                                    unsigned int channel,
                                    unsigned int subchannel,
                                    bool is_remote_io,
                                    unsigned int embedding_indices_per_input,
                                    unsigned int embedding_indices_per_tile,
                                    DramInputNodeType dram_buffer_type) :
            DramInputNode(node_id, physical_location, size_tiles, tile_size, num_epoch_tiles, tiles_per_input,
                          operand_id, num_scatter_chunks, scatter_gather_num_tiles, num_queue_slots, is_ram,
                          dram_address, channel, subchannel, is_remote_io, dram_buffer_type, false),
            m_embedding_indices_per_input(embedding_indices_per_input),
            m_embedding_indices_per_tile(embedding_indices_per_tile)
        {
            m_node_type = RGNodeType::DramEmbeddingIndexInput;
        }

        // Returns how many indices are read from a single input.
        unsigned int get_embedding_indices_per_input() const { return m_embedding_indices_per_input; }

        // Returns how many indices are stored in a single tile.
        unsigned int get_embedding_indices_per_tile() const { return m_embedding_indices_per_tile; }

    private:
        // How many indices are read from a single input.
        unsigned int m_embedding_indices_per_input;

        // How many indices are stored in a single tile.
        unsigned int m_embedding_indices_per_tile;
    };
}