// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>

#include "device/tt_xy_pair.h"

struct epoch_t;
struct epoch_stream_info_t;
struct scatter_pipe_state_t;
struct scatter_pipe_blob_t;
struct epoch_stream_dram_io_info_t;
struct dram_io_state_t;
struct dram_io_scatter_state_t;
union tt_uint64_t;

namespace blobgen2
{

// This class is used to manage one section of the overlay blob.
// It holds helper functions for appending various types of data to the blob.
// Main content that this class produces is a vector of bytes that will be written to the output binary file, and the
// address at which this section will be written to L1 memory.
class BlobSection
{
public:
    BlobSection(uint32_t address) : m_address(address) {}

    // Don't allow copying around.
    // Move constructor has to be explicitly defined when copy constructor is deleted.
    BlobSection(BlobSection&) = delete;
    BlobSection(BlobSection&&) = default;

    // Implementations of adding each of the epoch structs.
    void add_epoch_t(const epoch_t* epoch);
    void add_epoch_stream_info_t(const epoch_stream_info_t* epoch_stream_info);
    void add_scatter_pipe_states(const scatter_pipe_state_t* scatter_pipe_state_arr, const size_t size);
    void add_scatter_pipe_blobs(const scatter_pipe_blob_t* scatter_pipe_blob_arr, const size_t size);
    void add_epoch_stream_dram_io_info_t(const epoch_stream_dram_io_info_t* epoch_stream_info);
    void add_dram_io_state_t(const dram_io_state_t* dram_io_state);
    void add_dram_io_scatter_state_t(const dram_io_scatter_state_t* dram_io_scatter_state);
    void add_dram_io_scatter_offsets(const tt_uint64_t* scatter_offsets, const size_t size, const bool hw_tilize);

    // Append functions for all the basic types needed.
    void append(const uint8_t byte);
    void append(const uint16_t word);
    void append(const uint32_t dword);
    void append(const tt_uint64_t& qword);

    // Append a debug message to current location in blob.
    void append_debug(const std::string debug_str);

    // Append functions for arrays.
    template <typename T, std::size_t N>
    void append(const T (&arr)[N])
    {
        for (int i = 0; i < N; i++)
        {
            append(arr[i]);
        }
    }

    // For each of the append functions add a variant which appends a debug string.
    template <typename T>
    void append(const T& byte, const std::string name)
    {
        append_debug("|(" + std::to_string(sizeof(T) * 8) + "b) " + name);
        append(byte);
    }

    // Variant of append for arrays which appends a debug string.
    template <typename T, std::size_t N>
    void append(const T (&arr)[N], const std::string name)
    {
        for (int i = 0; i < N; i++)
        {
            append(arr[i], name + "[" + std::to_string(i) + "]");
        }
    }

    // Get current size in double words (4 bytes each).
    size_t dw_size() const { return size() / sizeof(uint32_t); }

    // Get current size in bytes.
    size_t size() const { return m_data.size(); }

    // Used only for intermediate buffers.
    // Could be that this can be refactored out.
    template <typename T>
    void pop()
    {
        for (int i = 0; i < sizeof(T) / sizeof(decltype(m_data)::value_type); i++)
        {
            m_data.pop_back();
        }
    }

    // Merge other blob section into this one. Requires moving the other blob section into this function, and it is not
    // intended to leave the other blob section usable.
    // Optional map function which maps each 4-byte combinations.
    void merge(BlobSection&& other, std::function<uint32_t(uint32_t)> map_func = nullptr);

    // Almost all epoch structures that we write to the blob must be aligned to NOC_ADDRESS_ALIGNMENT, since they're
    // fetched from DRAM, and there is a known issue with unaligned addresses. This function is called after each of the
    // structures is added to the blob.
    void pad_to_noc_alignment();

    // Print out the blob section to the output stream.
    // If debug is set to true, will produce non-functioning output, but will greatly aid in debugging.
    void print_out(std::ostream& os, const bool dump_debug_info) const;

private:
    // Used exclusively for merging two blob sections.
    uint32_t get_dw_at(const int ind);

    // Starting address of the blob section, which will be written to the output binary as a location.
    uint32_t m_address;

    // Holds vector of bytes for this section, that will be written to output binary file.
    std::vector<uint8_t> m_data;

    // Holds debugging strings for each of the bytes (more precisely, a string per 4 bytes).
    // Printed only for debugging.
    std::vector<std::string> m_debug_data;
};

struct BlobData
{
    std::vector<BlobSection> blob_sections;

    void print_out(
        const std::string output_dir,
        const tt_cxy_pair& core_location,
        const int epoch_num,
        const bool dump_debug_info) const;
};

}  // namespace blobgen2
