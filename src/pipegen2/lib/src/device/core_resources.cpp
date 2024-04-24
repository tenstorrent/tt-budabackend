// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/core_resources.h"

#include "device/core_resources_constants.h"
#include "device/ethernet_core_resources.h"
#include "device/l1/l1_buffer.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

CoreResources::CoreResources(const tt_cxy_pair& core_physical_location,
                             const tt_cxy_pair& core_logical_location,
                             StreamId extra_streams_id_range_start,
                             StreamId extra_streams_id_range_end,
                             const unsigned int l1_data_buffers_space_start_address,
                             const unsigned int l1_data_buffers_space_end_address,
                             const unsigned int l1_predefined_tile_header_buffer_address) :
    m_core_physical_location(core_physical_location),
    m_core_logical_location(core_logical_location),
    c_extra_streams_id_range_start(extra_streams_id_range_start),
    c_extra_streams_id_range_end(extra_streams_id_range_end),
    m_next_available_extra_stream_id(extra_streams_id_range_start),
    m_next_available_kernel_input_index(0),
    m_next_available_kernel_output_index(0),
    m_l1_data_buffers_memory(
        std::make_unique<L1DataBuffersMemoryLayout>(
            core_physical_location,
            core_logical_location,
            l1_data_buffers_space_start_address,
            l1_data_buffers_space_end_address,
            l1_predefined_tile_header_buffer_address))
{
}

CoreResources::~CoreResources() = default;

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
            std::to_string(core_resources_constants::max_kernel_inputs_count) + ". ",
            m_core_physical_location,
            m_core_logical_location,
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
            m_core_logical_location,
            OutOfCoreResourcesException::CoreResourceType::kKernelOutputIndex,
            core_resources_constants::max_kernel_outputs_count);
    }

    return m_next_available_kernel_output_index++;
}

unsigned int CoreResources::get_multicast_streams_count() const
{
    return calculate_multicast_streams_count();
}

const L1Buffer* CoreResources::allocate_l1_tile_header_buffer(const unsigned int tile_size)
{
    return m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(tile_size);
}

const L1Buffer* CoreResources::allocate_l1_stream_buffer(const StreamNode* stream_node, const unsigned int buffer_size)
{
    return m_l1_data_buffers_memory->allocate_l1_stream_buffer(stream_node, buffer_size);
}

const L1Buffer* CoreResources::allocate_l1_ncrisc_fallback_buffer(const unsigned int buffer_size)
{
    return m_l1_data_buffers_memory->allocate_l1_ncrisc_fallback_buffer(buffer_size);
}

const L1Buffer* CoreResources::allocate_l1_extra_overlay_blob_space(const unsigned int total_blob_size,
                                                                    const bool is_ethernet_core)
{
    return m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(total_blob_size, is_ethernet_core);
}

void CoreResources::check_if_out_of_l1_data_buffers_memory() const
{
    m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory();
}

unsigned int CoreResources::get_tile_header_buffer_address(const unsigned int tile_size) const
{
    return m_l1_data_buffers_memory->get_tile_header_buffer_address(tile_size);
}

const std::string CoreResources::get_l1_memory_layout_info() const 
{ 
    std::stringstream string_stream;
    
    string_stream << "L1 memory usage breakdown for core:\n"
                  << "Physical location: " << m_core_physical_location.str() << "\n"
                  << "Netlist coordinates: "
                  << "(chip=" << m_core_logical_location.chip 
                  << ", r=" << m_core_logical_location.y
                  << ", c=" << m_core_logical_location.x << ")\n"
                  << "OP name: " << get_op_name() << "\n"
                  << m_l1_data_buffers_memory->get_allocation_info(); 
    
    return string_stream.str();
}

} // namespace pipegen2