// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/core_resources.h"

#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "pipegen2_constants.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

CoreResources::CoreResources(const tt_cxy_pair& core_physical_location,
                             StreamId extra_streams_id_range_start,
                             StreamId extra_streams_id_range_end,
                             int l1_data_buffers_space_start_address,
                             int l1_data_buffers_space_end_address) :
    m_core_physical_location(core_physical_location),
    c_extra_streams_id_range_start(extra_streams_id_range_start),
    c_extra_streams_id_range_end(extra_streams_id_range_end),
    c_l1_data_buffers_space_start_address(l1_data_buffers_space_start_address),
    c_l1_data_buffers_space_end_address(l1_data_buffers_space_end_address),
    m_next_available_extra_stream_id(extra_streams_id_range_start),
    m_l1_extra_tile_headers_space(0),
    m_l1_extra_overlay_blob_space(0),
    m_l1_current_data_buffers_space_address(l1_data_buffers_space_end_address -
                                            constants::unused_data_buffers_space_bytes),
    m_next_available_kernel_input_index(0),
    m_next_available_kernel_output_index(0)
{
}

void CoreResources::allocate_l1_extra_tile_headers_space(unsigned int num_extra_tile_headers)
{
    m_l1_extra_tile_headers_space =
        num_extra_tile_headers * constants::tile_header_size_bytes * constants::general_max_num_tiles_per_phase;

    // Move data buffer space address to make space for extra tile headers.
    allocate_l1_data_buffer(m_l1_extra_tile_headers_space);
}

void CoreResources::allocate_l1_extra_overlay_blob_space(unsigned int size_in_bytes)
{
    m_l1_extra_overlay_blob_space = size_in_bytes;
}

unsigned int CoreResources::allocate_l1_data_buffer(unsigned int size_in_bytes)
{
    m_l1_current_data_buffers_space_address -= size_in_bytes;

    check_if_out_of_l1_data_buffers_memory();

    return m_l1_current_data_buffers_space_address;
}

void CoreResources::check_if_out_of_l1_data_buffers_memory()
{
    if (m_l1_current_data_buffers_space_address <
        c_l1_data_buffers_space_start_address + m_l1_extra_overlay_blob_space)
    {
        const int allocated_space_in_bytes =
            c_l1_data_buffers_space_end_address - m_l1_current_data_buffers_space_address;
        const int total_available_space =
            c_l1_data_buffers_space_end_address -
            (c_l1_data_buffers_space_start_address + m_l1_extra_overlay_blob_space);

        ERROR("CoreResources: core " << m_core_physical_location.str() <<
              " is out of data buffers memory (allocated " << allocated_space_in_bytes <<
              " bytes out of available " << total_available_space << " bytes)");
    }
}

StreamId CoreResources::allocate_gather_stream()
{
    StreamId stream_id = get_next_available_gather_stream_id();

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId CoreResources::allocate_multicast_stream()
{
    StreamId stream_id = get_next_available_multicast_stream_id();

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId CoreResources::allocate_general_purpose_stream()
{
    StreamId stream_id = get_next_available_general_purpose_stream_id();

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

unsigned int CoreResources::allocate_kernel_input()
{
    if (m_next_available_kernel_input_index >= core_resources_constants::max_kernel_inputs_count)
    {
        ERROR("CoreResources: Out of available kernel inputs on core " << m_core_physical_location.str());
    }

    return m_next_available_kernel_input_index++;
}

unsigned int CoreResources::allocate_kernel_output()
{
    if (m_next_available_kernel_output_index >= core_resources_constants::max_kernel_outputs_count)
    {
        ERROR("CoreResources: Out of available kernel outputs on core " << m_core_physical_location.str());
    }

    return m_next_available_kernel_output_index++;
}

unsigned int CoreResources::get_multicast_streams_count() const
{
    return calculate_multicast_streams_count();
}

} // namespace pipegen2