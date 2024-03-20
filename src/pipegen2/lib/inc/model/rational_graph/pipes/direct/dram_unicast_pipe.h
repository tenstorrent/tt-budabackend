// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "direct_pipe.h"
#include "model/rational_graph/pipes/ncrisc_reader_pipe_interface.h"

namespace pipegen2
{
    class DramUnicastPipe : public DirectPipe, public INcriscReaderPipe
    {
    public:
        DramUnicastPipe(RGPipeProperties&& rg_pipe_properties,
                        const std::vector<int>& dram_input_total_readers,
                        const std::vector<int>& dram_input_reader_index,
                        const unsigned int max_dram_input_buffer_size_tiles,
                        const tt_cxy_pair& physical_location) :
            DirectPipe(RGPipeType::DramUnicast, std::move(rg_pipe_properties), physical_location),
            m_dram_input_total_readers(dram_input_total_readers),
            m_dram_input_reader_index(dram_input_reader_index),
            m_max_dram_input_buffer_size_tiles(max_dram_input_buffer_size_tiles)
        {
        }

        const std::vector<int>& get_dram_input_total_readers() const { return m_dram_input_total_readers; }

        const std::vector<int>& get_dram_input_reader_index() const { return m_dram_input_reader_index; }

        unsigned int get_max_dram_input_buffer_size_tiles() const { return m_max_dram_input_buffer_size_tiles; }

        std::vector<tt_cxy_pair> get_ncrisc_reader_streams_locations() const override
        {
            // One stream and one NCRISC config will be allocated at unpacker's location.
            return { get_output_node()->get_physical_location() };
        }

    private:
        // Total pipe readers for every input in this pipe. Contains duplicates as it has one entry for every element in
        // pipe inputs which itself contains duplicates.
        std::vector<int> m_dram_input_total_readers;

        // This pipe's reader index for every input that may be read by other pipes. If an input is read only by single
        // pipe, its reader index will be 0.
        std::vector<int> m_dram_input_reader_index;

        // Max amount of L1 space pipegen is allowed to use for scaling the input dram buffers of this pipe.
        unsigned int m_max_dram_input_buffer_size_tiles;
    };
}