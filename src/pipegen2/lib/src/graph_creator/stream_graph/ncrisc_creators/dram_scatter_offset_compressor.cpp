// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/ncrisc_creators/dram_scatter_offset_compressor.h"

#include <numeric>
#include <optional>

namespace pipegen2
{
    DramScatterOffsetCompressor::DramScatterOffsetCompressor(const std::vector<unsigned long>& dram_scatter_offsets)
        : m_dram_scatter_offsets(dram_scatter_offsets)
    {
    }

    std::vector<unsigned long> DramScatterOffsetCompressor::compress_dram_scatter_offsets()
    {
        std::size_t pattern_start_index = 0;

        while (pattern_start_index < m_dram_scatter_offsets.size())
        {
            const ScatterPatternCompressionResult pattern_compression =
                get_max_pattern_compression(pattern_start_index);

            const std::size_t scatter_pattern_length =
                get_compression_scatter_pattern_length(pattern_compression, pattern_start_index);
            const std::size_t offsets_to_skip =
                get_compression_pattern_index_increment(pattern_compression, pattern_start_index);

            insert_scatter_pattern(pattern_start_index, scatter_pattern_length);
            if (pattern_compression.can_compress(c_minimum_compression_level))
            {
                insert_scatter_offset_loop(pattern_compression);
            }

            pattern_start_index += offsets_to_skip;
        }

        return m_compressed_scatter_offsets;
    }

    ScatterPatternCompressionResult DramScatterOffsetCompressor::get_max_pattern_compression(
        const std::size_t pattern_start_index)
    {
        // We can compress at most half of the interval of the scatter offsets.
        const std::size_t max_pattern_length = (m_dram_scatter_offsets.size() - pattern_start_index) / 2;

        // Initialize to no compression.
        ScatterPatternCompressionResult best_compression = ScatterPatternCompressionResult::no_compression();

        std::size_t pattern_length = c_min_scatter_pattern_length;
        for (; pattern_length <= max_pattern_length; ++pattern_length)
        {
            const ScatterPatternCompressionResult current_pattern_compression = get_scatter_pattern_compression(
                pattern_start_index, pattern_length);

            if (current_pattern_compression > best_compression)
            {
                best_compression = current_pattern_compression;
            }
        }

        return best_compression;
    }

    ScatterPatternCompressionResult DramScatterOffsetCompressor::get_scatter_pattern_compression(
        const std::size_t pattern_start_index,
        const std::size_t pattern_length)
    {
        // What is the max loop count that we can get with the current pattern length.
        // We subtract pattern length because the initial scatter pattern is not counted as a loop iteration.
        const unsigned long max_num_loops =
            (m_dram_scatter_offsets.size() - pattern_start_index - pattern_length) / pattern_length;

        unsigned long num_loops = 0;
        std::optional<unsigned long> increment;
        bool pattern_has_matching_increment = true;

        for (unsigned long loop_index = 0; loop_index < max_num_loops && pattern_has_matching_increment; ++loop_index)
        {
            // For each element of the pattern, check if we can match it in the current loop iteration
            for (unsigned long pattern_el_index = 0; pattern_el_index < pattern_length; ++pattern_el_index)
            {
                const std::size_t prev_offset_index =
                    pattern_start_index + pattern_el_index + loop_index * pattern_length;
                const std::size_t next_offset_index = prev_offset_index + pattern_length;

                const unsigned long prev_offset = m_dram_scatter_offsets[prev_offset_index];
                const unsigned long next_offset = m_dram_scatter_offsets[next_offset_index];

                const unsigned long current_el_increment = next_offset - prev_offset;

                if (!is_valid_scatter_offset_increment(current_el_increment))
                {
                    // Current pattern compression is discarded to prevent one buffer from spanning multiple
                    // dram buffers.
                    return ScatterPatternCompressionResult::no_compression();
                }
                else if (increment.has_value() && increment.value() != current_el_increment)
                {
                    // For this element, the increment is different than the increment for the rest of the elements
                    // in the scatter pattern, so we can't match any further.
                    pattern_has_matching_increment = false;
                    break;
                }
                else
                {
                    // This is the first increment computed, so intialize the common increment variable.
                    increment = current_el_increment;
                }
            }

            // All elements in the pattern matched, increment the loop counter and try to match the next one.
            if (pattern_has_matching_increment)
            {
                ++num_loops;
            }
        }

        return ScatterPatternCompressionResult(num_loops, increment.value_or(0), pattern_length);
    }

    bool DramScatterOffsetCompressor::is_valid_scatter_offset_increment(const unsigned long increment)
    {
        return increment <= c_bad_increment_threshold;
    }

    unsigned long DramScatterOffsetCompressor::encode_dram_scatter_offset_loop(
        const ScatterPatternCompressionResult& pattern_compression)
    {
        const unsigned long shifted_num_loops = (pattern_compression.get_num_loops() << 32);
        const std::size_t pattern_length = pattern_compression.get_pattern_length();

        return c_dram_io_is_scatter_loop_flag | shifted_num_loops | pattern_length;
    }

    void DramScatterOffsetCompressor::insert_scatter_offset_loop(
        const ScatterPatternCompressionResult& pattern_compression)
    {
        m_compressed_scatter_offsets.push_back(encode_dram_scatter_offset_loop(pattern_compression));
        m_compressed_scatter_offsets.push_back(pattern_compression.get_increment());
    }

    void DramScatterOffsetCompressor::insert_scatter_pattern(const std::size_t pattern_start_index,
                                                             const std::size_t pattern_length)
    {
        for (std::size_t i = 0; i < pattern_length; ++i)
        {
            m_compressed_scatter_offsets.push_back(m_dram_scatter_offsets[pattern_start_index + i]);
        }
    }

    std::size_t DramScatterOffsetCompressor::get_compression_scatter_pattern_length(
        const ScatterPatternCompressionResult& pattern_compression,
        const std::size_t pattern_start_index)
    {
        if (pattern_compression.can_compress(c_minimum_compression_level))
        {
            return pattern_compression.get_pattern_length();
        }

        // If the pattern cannot be compressed that means that all offsets, starting from the pattern index,
        // have to be inserted in the resulting scatter offsets list.
        return m_dram_scatter_offsets.size() - pattern_start_index;
    }

    unsigned long DramScatterOffsetCompressor::get_compression_pattern_index_increment(
        const ScatterPatternCompressionResult& pattern_compression,
        const std::size_t pattern_start_index)
    {
        if (pattern_compression.can_compress(c_minimum_compression_level))
        {
            // Number of loops is incremented by 1 to account for the original pattern.
            return (pattern_compression.get_num_loops() + 1) * pattern_compression.get_pattern_length();
        }

        // If the pattern cannot be compressed that means that all offsets, starting from the pattern index,
        // can be skipped.
        return m_dram_scatter_offsets.size() - pattern_start_index;
    }

    std::vector<unsigned long> DramScatterOffsetCompressor::compress_dram_scatter_offsets_for_tilizer(
        const unsigned int c_dim_size,
        const unsigned int tilize_mblock_n_loop_num_rows)
    {
        std::size_t pattern_start_index = 0;

        while (pattern_start_index < m_dram_scatter_offsets.size())
        {
            std::size_t pattern_end_index = find_arithmetic_progression_pattern_end_index(
                pattern_start_index, c_dim_size, tilize_mblock_n_loop_num_rows);

            if (pattern_end_index - pattern_start_index + 1 >= c_min_scatter_pattern_length_for_tilizer)
            {
                m_compressed_scatter_offsets.push_back(m_dram_scatter_offsets[pattern_start_index]);
                m_compressed_scatter_offsets.push_back(m_dram_scatter_offsets[pattern_start_index + 1]);

                std::size_t pattern_extra_count = pattern_end_index - pattern_start_index - 1;
                unsigned long special_offset = c_dram_io_is_scatter_loop_for_tilizer_flag | pattern_extra_count;
                m_compressed_scatter_offsets.push_back(special_offset);
            }
            else
            {
                for (std::size_t i = pattern_start_index; i <= pattern_end_index; ++i)
                {
                    m_compressed_scatter_offsets.push_back(m_dram_scatter_offsets[i]);
                }
            }

            pattern_start_index = pattern_end_index + 1;
        }

        return m_compressed_scatter_offsets;
    }

    std::size_t DramScatterOffsetCompressor::find_arithmetic_progression_pattern_end_index(
        std::size_t pattern_start_index,
        const unsigned int c_dim_size,
        const unsigned int tilize_mblock_n_loop_num_rows)
    {
        if (pattern_start_index + c_min_scatter_pattern_length_for_tilizer > m_dram_scatter_offsets.size())
        {
            // No need to search further since we won't find the pattern of required length.
            return m_dram_scatter_offsets.size() - 1;
        }

        // In order to fit bits for pattern extra elements count into special offsets flag.
        std::size_t max_pattern_length = c_dram_io_is_scatter_loop_for_tilizer_flag + 1;
        // TODO: Add explanation for this condition.
        if (c_dim_size > 1)
        {
            max_pattern_length = std::min(max_pattern_length, (std::size_t)tilize_mblock_n_loop_num_rows);
        }
        const std::size_t max_pattern_end_index =
            std::min(m_dram_scatter_offsets.size() - 1, pattern_start_index + max_pattern_length - 1);

        const unsigned long pattern_diff =
            m_dram_scatter_offsets[pattern_start_index + 1] - m_dram_scatter_offsets[pattern_start_index];

        std::size_t pattern_end_index = pattern_start_index + 1;
        while (pattern_end_index + 1 <= max_pattern_end_index &&
               m_dram_scatter_offsets[pattern_end_index + 1] - m_dram_scatter_offsets[pattern_end_index] ==
               pattern_diff)
        {
            ++pattern_end_index;
        }

        return pattern_end_index;
    }
}
