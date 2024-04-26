// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources.h"

#include "l1_address_map.h"
#include "noc/noc_overlay_parameters.h"
#include "stream_io_map.h"

#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "pipegen2_exceptions.h"
#include "utils/logger.hpp"

namespace pipegen2
{

WorkerCoreResources::WorkerCoreResources(const tt_cxy_pair& core_physical_location, const tt_cxy_pair& core_logical_location) :
    CoreResources(core_physical_location,
                  core_logical_location,
                  static_cast<StreamId>(END_IO_STREAM + 1),
                  static_cast<StreamId>(NOC_NUM_STREAMS - 1),
                  l1_mem::address_map::DATA_BUFFER_SPACE_BASE,
                  l1_mem::address_map::MAX_SIZE)
{
}

StreamId WorkerCoreResources::allocate_packer_stream(int operand_id)
{
    if (!OperandStreamMap::is_output_operand(operand_id))
    {
        throw IllegalCoreResourceAllocationException(
            "Trying to allocate packer stream on a worker core " + get_physical_location().str() +
            " with invalid operand ID: " + std::to_string(operand_id) + ".",
            get_physical_location(),
            IllegalCoreResourceAllocationException::CoreResourceType::kPackerStreams);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        throw OutOfCoreResourcesException(
            "Trying to allocate packer stream on a worker core " + get_physical_location().str() +
            ", with operand ID: " + std::to_string(operand_id) + ", which was already allocated.",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kPackerStreams,
            1 /* available_core_resources */);
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_unpacker_stream(int operand_id)
{
    if (!OperandStreamMap::is_input_operand(operand_id))
    {
        throw IllegalCoreResourceAllocationException(
            "Trying to allocate unpacker stream on a worker core " + get_physical_location().str() +
            " with invalid operand ID: " + std::to_string(operand_id) + ".",
            get_physical_location(),
            IllegalCoreResourceAllocationException::CoreResourceType::kUnpackerStreams);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        throw OutOfCoreResourcesException(
            "Trying to allocate unpacker stream on a worker core " + get_physical_location().str() +
            ", with operand ID: " + std::to_string(operand_id) + ", which was already allocated.",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kUnpackerStreams,
            1 /* available_core_resources */);
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_intermed_stream(int operand_id)
{
    if (!OperandStreamMap::is_intermediate_operand(operand_id))
    {
        throw IllegalCoreResourceAllocationException(
            "Trying to allocate intermediate stream on a worker core " + get_physical_location().str() +
            ", with invalid operand ID: " + std::to_string(operand_id) + ", which was already allocated.",
            get_physical_location(),
            IllegalCoreResourceAllocationException::CoreResourceType::kIntermediateStreams);
    }

    StreamId stream_id = OperandStreamMap::get_operand_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        throw OutOfCoreResourcesException(
            "Trying to allocate intermediate stream on a worker core " + get_physical_location().str() +
            ", with operand ID: " + std::to_string(operand_id) + ", which was already allocated.",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kIntermediateStreams,
            1 /* available_core_resources */);
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::allocate_packer_multicast_stream(int operand_id)
{
    StreamId stream_id = get_packer_multicast_stream_id(operand_id);

    if (m_allocated_stream_ids.count(stream_id) > 0)
    {
        throw OutOfCoreResourcesException(
            "Trying to allocate packer-multicast stream on a worker core " + get_physical_location().str() +
            ", with operand ID: " + std::to_string(operand_id) + ", which was already allocated.",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kPackerMulticastStreams,
            1 /* available_core_resources */);
    }

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

StreamId WorkerCoreResources::get_next_available_general_purpose_stream_id()
{
    if (m_next_available_extra_stream_id > get_extra_streams_id_range_end())
    {
        const unsigned int available_extra_streams = calculate_general_purpose_streams_count();
        throw OutOfCoreResourcesException(
            "Out of available extra streams on a worker core " + get_physical_location().str() +
            ". Total number of available extra streams per worker core is " +
            std::to_string(available_extra_streams) + ".",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kGeneralPurposeStreams,
            available_extra_streams);
    }

    StreamId stream_id = m_next_available_extra_stream_id++;

    return stream_id;
}

unsigned int WorkerCoreResources::get_predefined_tile_header_buffer_addr() const
{
    return l1_mem::address_map::OVERLAY_BLOB_BASE -
           core_resources_constants::tile_header_buffer_allocation_cushion_bytes -
           TileHeaderBuffer::get_tile_header_buffer_size_bytes();
}
} // namespace pipegen2