// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "model/typedefs.h"

namespace pipegen2
{
    class StreamNode;
    class PCIeStreamingNode;

    // TODO add header comments and setters/getters
    class NcriscConfig
    {
    public:
        NcriscConfig() = default;

        // Checks if config is used to read or write to active DRAM or PCIe buffer. Active buffers are all but DRAM 
        // prefetch inputs and DRAM intermediate outputs.
        bool is_accessing_active_dram_or_pcie_buffer() const
        { 
            return (dram_input || dram_output) && (dram_io || dram_streaming); 
        }

        std::uint64_t dram_buf_noc_addr;

        unsigned int dram_buf_size_bytes;

        unsigned int dram_buf_size_q_slots;

        unsigned int dram_buf_size_tiles;

        unsigned int msg_size;

        unsigned int num_msgs;

        std::optional<PrefetchType> prefetch_type;

        std::optional<bool> dram_input;

        std::optional<bool> dram_output;

        std::optional<bool> dram_io;

        std::optional<bool> dram_ram;

        std::optional<bool> dram_padding;

        std::optional<unsigned int> dram_scatter_chunk_size_tiles;

        std::optional<std::vector<std::uint64_t>> dram_scatter_offsets;

        std::optional<unsigned int> dram_scatter_offsets_full_size;

        std::optional<unsigned int> dram_buf_read_chunk_size_tiles;

        std::optional<unsigned int> dram_buf_write_chunk_size_tiles;

        std::optional<unsigned int> dram_buf_write_row_chunk_size_bytes;

        std::optional<bool> dram_streaming;

        std::optional<bool> dram_streaming_downstream;

        std::optional<const PCIeStreamingNode*> pcie_streaming_node;

        std::optional<StreamNode*> dram_streaming_dest;

        std::optional<unsigned int> reader_index;

        std::optional<unsigned int> total_readers;
    };
}