// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace pipegen2
{
    class ScatterPatternCompressionResult
    {
    public:
        ScatterPatternCompressionResult(std::uint64_t num_loops, std::uint64_t increment, std::size_t pattern_length)
            : m_num_loops(num_loops), m_increment(increment), m_pattern_length(pattern_length)
        {
        }

        static ScatterPatternCompressionResult no_compression()
        {
            return ScatterPatternCompressionResult(0, 0, 0);
        }

        // Decides if the current compression is better than the other compression by comparing their compression
        // levels. If the compression levels are the same, the better compression is the one with larger pattern.
        bool operator > (const ScatterPatternCompressionResult& other) const
        {
            const std::uint64_t my_compression_level = get_compression_level();
            const std::uint64_t other_compression_level = other.get_compression_level();

            if (my_compression_level == other_compression_level)
            {
                // If the number of offsets saved is the same, favor compressions with larger scatter pattern.
                return m_pattern_length > other.m_pattern_length;
            }

            return my_compression_level > other_compression_level;
        }

        // Returns compression level for a given scater pattern in terms on the number of offsets which won't
        // have to be stored in the resulting scatter offsets list.
        std::size_t get_compression_level() const
        {
            std::size_t compression_level = 0;
            if (has_compression())
            {
                compression_level = m_num_loops * m_pattern_length;
            }
            return compression_level;
        }

        std::uint64_t get_num_loops() const
        {
            return m_num_loops;
        }

        std::size_t get_pattern_length() const
        {
            return m_pattern_length;
        }

        std::uint64_t get_increment() const
        {
            return m_increment;
        }

        bool can_compress(const std::size_t min_compression_level) const
        {
            const std::size_t compression_level = get_compression_level();
            return compression_level >= min_compression_level;
        }

    private:
        bool has_compression() const
        {
            return m_num_loops > 0;
        }

        // Number of times the scatter pattern can be repeated.
        std::uint64_t m_num_loops;

        // Value by which to increment all the elements in the scatter pattern to get to the next loop iteration.
        std::uint64_t m_increment;

        // Length of the scatter pattern.
        std::size_t m_pattern_length;
    };

    class DramScatterOffsetCompressor
    {
    public:
        DramScatterOffsetCompressor(const std::vector<std::uint64_t>& dram_scatter_offsets);

        // Compresses DRAM scatter offsets.
        std::vector<std::uint64_t> compress_dram_scatter_offsets();

        // Compresses DRAM scatter offsets for tilizer op dram input. Applies simpler compression algorithm which only
        // finds groups of consecutive evenly spaced offsets (arithmetic progression) and compresses each such group.
        std::vector<std::uint64_t> compress_dram_scatter_offsets_for_tilizer(
            const unsigned int c_dim_size,
            const unsigned int tilize_mblock_n_loop_num_rows);

    private:
        // Computes maximum possible compression for a scatter pattern starting at a given index.
        ScatterPatternCompressionResult get_max_pattern_compression(const std::size_t pattern_start_index);

        // Computes compression info for a scatter pattern of a given length starting at a given index.
        ScatterPatternCompressionResult get_scatter_pattern_compression(const std::size_t pattern_start_index,
                                                                        const std::size_t pattern_length);

        // Checks if increment can fit in 32 bits.
        bool is_valid_scatter_offset_increment(const std::uint64_t increment);

        // Returns encoded scatter offset loop magic number for a given scatter compression.
        std::uint64_t encode_dram_scatter_offset_loop(const ScatterPatternCompressionResult& scatter_compression);

        // Inserts scatter offset loop magic number and increment in the resulting scatter offset list.
        void insert_scatter_offset_loop(const ScatterPatternCompressionResult& scatter_compression);

        // Inserts scatter pattern of a given size in the resulting scatter offset list.
        void insert_scatter_pattern(const std::size_t pattern_start_index, const std::size_t pattern_length);

        // Returns the length of the scatter pattern starting at a given index for a given compression level.
        std::size_t get_compression_scatter_pattern_length(
            const ScatterPatternCompressionResult& scatter_compression,
            const std::size_t pattern_start_index);

        // Returns the number of offsets which can be skipped if a given compression is a applied to a pattern
        // starting at a given index.
        std::uint64_t get_compression_pattern_index_increment(
            const ScatterPatternCompressionResult& scatter_compression,
            const std::size_t pattern_start_index);

        // Finds ending index of arithmetic progression pattern starting from the given index.
        std::size_t find_arithmetic_progression_pattern_end_index(
            std::size_t pattern_start_index,
            const unsigned int c_dim_size,
            const unsigned int tilize_mblock_n_loop_num_rows);

        // Flag indicating that the scatter offset is special in a way that it encodes scatter loop.
        static constexpr std::uint64_t c_dram_io_is_scatter_loop_flag = 0x8000000000000000ULL;

        // Flag indicating that the scatter offset is special in a way that it encodes scatter loop for tilizer.
        static constexpr std::uint64_t c_dram_io_is_scatter_loop_for_tilizer_flag = 0x0000000080000000ULL;

        // Minimum memory space saved in terms of offsets for a compression to be valid.
        static constexpr std::size_t c_minimum_compression_level = 3;

        // Minimum length of pattern for a compression to be valid.
        static constexpr std::size_t c_min_scatter_pattern_length = 8;

        // Minimum length of pattern for a tilizer offsets compression to be valid.
        // TODO: This can actually be 4 because compression for tilizer takes only 3 elements, two offsets and special
        //       offset denoting loop and count. Keeping same number as pipegen1 for now because changing it would also
        //       require changes in firmware.
        static constexpr std::size_t c_min_scatter_pattern_length_for_tilizer = 5;

        // Max allowed increment - max 32 bit value. This constraint is the result of the 32 bit DRAM address
        // space on Grayskull.
        static constexpr std::uint64_t c_bad_increment_threshold = std::numeric_limits<unsigned int>::max();

        // Original scatter offsets, without compressions.
        const std::vector<std::uint64_t>& m_dram_scatter_offsets;

        // Resulting, compressed scatter offsets.
        std::vector<std::uint64_t> m_compressed_scatter_offsets;
    };
}