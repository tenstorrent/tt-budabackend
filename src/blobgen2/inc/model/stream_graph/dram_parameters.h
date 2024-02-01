// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include "model/rational_graph/base_rg_pipe.h"
#include "model/rational_graph/dram_input_node.h"

namespace pipegen2
{
    class DramParameters
    {
    public:
        using UPtr = std::unique_ptr<DramParameters>;

        // TODO expand this class with params for dram write.
        unsigned int get_read_chunk_size_tiles(const RGBasePipe* rg_pipe) const
        {
            // TODO assert this pipe exists in the map.
            return m_read_chunk_size_tiles.at(rg_pipe);
        }

        unsigned int get_reader_index(const RGBasePipe* rg_pipe, const DramInputNode* dram_input_node) const
        {
            // TODO assert this pair exists in the map.
            return m_pipe_dram_input_reader_index.at({rg_pipe, dram_input_node});
        }

        void set_pipe_read_chunk_size_tiles(const RGBasePipe* pipe, unsigned int chunk_size_tiles)
        {
            m_read_chunk_size_tiles.emplace(pipe, chunk_size_tiles);
        }

        void set_reader_index(const RGBasePipe* rg_pipe, const DramInputNode* dram_input_node,
                              unsigned int reader_index)
        {
            m_pipe_dram_input_reader_index.emplace(std::piecewise_construct,
                                                   std::forward_as_tuple(rg_pipe, dram_input_node),
                                                   std::forward_as_tuple(reader_index));
        }

    private:
        // Map that keeps read chunk size for every pipe in a rational graph that reads from dram.
        std::unordered_map<const RGBasePipe*, unsigned int> m_read_chunk_size_tiles;

        // Maps that keeps reader index for every pipe and every dram input that pipe reads from.
        // Note: using ordered map to avoid hashing pairs. Alternatively use boost hash_combine.
        std::map<std::pair<const RGBasePipe*, const DramInputNode*>, unsigned int> m_pipe_dram_input_reader_index;
    };
}