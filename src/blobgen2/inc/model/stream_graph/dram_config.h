// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <string>
namespace pipegen2
{
    class DramConfig
    {
    public:
        using Ptr = std::shared_ptr<DramConfig>;
        using UPtr = std::unique_ptr<DramConfig>;

        DramConfig() = default;

        // Unique buffer identificator of a stream's dram blob.
        unsigned int buffer_id;

        // TODO add comments and setters/getters
        uint64_t dram_buf_noc_addr;

        uint32_t dram_buf_size_bytes;

        uint32_t dram_buf_size_q_slots;

        uint32_t dram_buf_size_tiles;

        uint32_t msg_size;

        uint32_t num_msgs;

        std::optional<bool> dram_input;

        std::optional<bool> dram_output;

        std::optional<bool> dram_io;

        std::optional<bool> dram_ram;

        std::optional<uint32_t> dram_scatter_chunk_size_tiles;

        std::optional<std::vector<uint64_t>> dram_scatter_offsets;

        std::optional<uint32_t> dram_buf_read_chunk_size_tiles;

        std::optional<uint32_t> dram_buf_write_chunk_size_tiles;

        std::optional<uint32_t> dram_buf_write_row_chunk_size_bytes;

        std::optional<bool> dram_streaming;

        std::optional<std::string> dram_streaming_dest;

        std::optional<uint32_t> reader_index;

        std::optional<uint32_t> total_readers;
    };
}