// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/nodes/dram_output_node.h"

#include "utils/logger.hpp"

namespace pipegen2
{
unsigned int DramOutputNode::get_untilized_output_tile_size() const
{
    return m_untilized_output_tile_dim_r * m_untilized_output_tile_dim_c;
}

unsigned int DramOutputNode::get_datum_size_bytes() const
{
    return m_tile_size / (m_untilized_output_tile_dim_r * m_untilized_output_tile_dim_c);
}

unsigned int DramOutputNode::get_datums_row_size_bytes() const { return m_tile_size / m_untilized_output_tile_dim_r; }

unsigned int DramOutputNode::get_stride_offset_bytes(const unsigned int stride_index) const
{
    log_assert(m_untilized_output, "Expecting call to get_stride_offset_bytes() only for untilized outputs");
    const unsigned int rows = m_untilized_output_full_r_dim / m_untilized_output_r_dim;
    const unsigned int cols = m_untilized_output_full_c_dim / m_untilized_output_c_dim;

    const unsigned stride_row = stride_index / cols;
    const unsigned stride_col = stride_index % cols;

    return stride_row * get_bytes_count_in_untilized_row_of_cores() +
           stride_col * get_bytes_count_in_untilized_row_for_single_producer();
}

unsigned int DramOutputNode::get_bytes_count_in_untilized_row_of_datums() const
{
    log_assert(
        m_untilized_output,
        "Expecting call to get_bytes_count_in_untilized_row_of_datums() only for untilized outputs");
    return m_untilized_output_tile_dim_c * m_untilized_output_full_c_dim * get_datum_size_bytes();
}

unsigned int DramOutputNode::get_bytes_count_in_untilized_row_of_tiles() const
{
    log_assert(
        m_untilized_output, "Expecting call to get_bytes_count_in_untilized_row_of_tiles() only for untilized outputs");
    return get_bytes_count_in_untilized_row_of_datums() * m_untilized_output_tile_dim_r;
}

unsigned int DramOutputNode::get_bytes_count_in_untilized_row_for_single_producer() const
{
    log_assert(
        m_untilized_output,
        "Expecting call to get_bytes_count_in_untilized_row_for_single_producer() only for untilized outputs");
    return m_untilized_output_tile_dim_c * m_untilized_output_c_dim * get_datum_size_bytes();
}

unsigned int DramOutputNode::get_bytes_count_in_untilized_row_of_cores() const
{
    log_assert(
        m_untilized_output, "Expecting call to get_bytes_count_in_untilized_row_of_cores() only for untilized outputs");
    return m_untilized_output_r_dim * m_untilized_output_full_c_dim * get_untilized_output_tile_size() *
           get_datum_size_bytes();
}

unsigned int DramOutputNode::get_bytes_count_in_untilized_output() const
{
    log_assert(
        m_untilized_output, "Expecting call to get_bytes_count_in_untilized_output() only for untilized outputs");
    return m_untilized_output_full_r_dim * m_untilized_output_full_c_dim * get_untilized_output_tile_size() *
           get_datum_size_bytes();
}
}  // namespace pipegen2