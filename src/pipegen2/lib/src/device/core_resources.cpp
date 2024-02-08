// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/core_resources.h"

#include "device/core_resources_constants.h"
#include "device/l1_memory_allocation.h"
#include "device/stream_buffer_allocation.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"

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

CoreResources::~CoreResources() = default;

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

        throw OutOfCoreResourcesException(
            "Core " + m_core_physical_location.str() + " is out of data buffers memory (allocated " +
            std::to_string(allocated_space_in_bytes) + " bytes out of available " +
            std::to_string(total_available_space) + " bytes).\n" +
            "Allocated stream buffers:\n" +
            allocations_to_string(),
            m_core_physical_location,
            OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
            total_available_space - constants::unused_data_buffers_space_bytes,
            allocated_space_in_bytes - constants::unused_data_buffers_space_bytes);
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
        throw OutOfCoreResourcesException(
            "Out of available kernel inputs on core " + m_core_physical_location.str() + ". " +
            " Number of available kernel inputs per core is " +
            std::to_string(core_resources_constants::max_kernel_inputs_count) + ".",
            m_core_physical_location,
            OutOfCoreResourcesException::CoreResourceType::kKernelInputIndex,
            core_resources_constants::max_kernel_inputs_count);
    }

    return m_next_available_kernel_input_index++;
}

unsigned int CoreResources::allocate_kernel_output()
{
    if (m_next_available_kernel_output_index >= core_resources_constants::max_kernel_outputs_count)
    {
        throw OutOfCoreResourcesException(
            "Out of available kernel outputs on core " + m_core_physical_location.str() + ". " +
            " Number of available kernel outputs per core is " +
            std::to_string(core_resources_constants::max_kernel_outputs_count) + ".",
            m_core_physical_location,
            OutOfCoreResourcesException::CoreResourceType::kKernelOutputIndex,
            core_resources_constants::max_kernel_outputs_count);
    }

    return m_next_available_kernel_output_index++;
}

unsigned int CoreResources::get_multicast_streams_count() const
{
    return calculate_multicast_streams_count();
}

void CoreResources::track_stream_buffer_allocation(const StreamNode* stream_node, 
                                                   unsigned int allocated_buffer_size, 
                                                   unsigned int allocated_buffer_address) 
{
    std::unique_ptr<L1MemoryAllocation> stream_buffer_allocation = 
        std::make_unique<StreamBufferAllocation>(stream_node, allocated_buffer_size, allocated_buffer_address);
    m_memory_allocations.push_back(std::move(stream_buffer_allocation));
}

std::string CoreResources::allocations_to_string() const
{
    std::stringstream string_stream;
    for (const std::unique_ptr<L1MemoryAllocation>& memory_allocation : get_all_memory_allocations())
    {
        string_stream << memory_allocation->allocation_info() << "\n";
    }

    return string_stream.str();
}

} // namespace pipegen2