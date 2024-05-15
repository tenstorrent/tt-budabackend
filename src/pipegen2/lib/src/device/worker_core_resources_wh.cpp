// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources_wh.h"

#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "pipegen2_exceptions.h"
#include "utils/logger.hpp"

namespace pipegen2
{

WorkerCoreResourcesWH::WorkerCoreResourcesWH(const tt_cxy_pair& core_physical_location, 
                                             const tt_cxy_pair& core_logical_location) :
    WorkerCoreResources(core_physical_location, core_logical_location),
    m_next_available_gather_multicast_stream_id(
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_start)
{
}

StreamId WorkerCoreResourcesWH::get_next_available_gather_stream_id()
{
    return get_next_available_gather_multicast_stream_id();
}

StreamId WorkerCoreResourcesWH::get_next_available_multicast_stream_id()
{
    StreamId stream_id;

    do
    {
        stream_id = get_next_available_gather_multicast_stream_id();
    }
    // Check if it was already allocated for packer-multicast.
    while (m_allocated_stream_ids.find(stream_id) != m_allocated_stream_ids.end());

    return stream_id;
}

StreamId WorkerCoreResourcesWH::get_next_available_gather_multicast_stream_id()
{
    if (m_next_available_gather_multicast_stream_id >
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_end)
    {
        const unsigned int gather_mcast_streams_count = get_multicast_streams_count();
        throw OutOfCoreResourcesException(
            "Out of available gather / multicast streams on a worker core " + get_physical_location().str() +
            ". Available number of gather / multicast streams per WH worker core is " +
            std::to_string(gather_mcast_streams_count) + ".",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kGatherMulticastStreams,
            gather_mcast_streams_count);
    }

    return m_next_available_gather_multicast_stream_id++;
}

StreamId WorkerCoreResourcesWH::get_packer_multicast_stream_id(int operand_id)
{
    if (operand_id < worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start ||
        operand_id > worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_end)
    {
        throw IllegalCoreResourceAllocationException(
            "Trying to allocate packer-multicast stream on a worker core " + get_physical_location().str() +
            " with invalid operand ID: " + std::to_string(operand_id) + " . Valid operand ID range is between " +
            std::to_string(worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start) +
            " and " +
            std::to_string(worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_end) + ".",
            get_physical_location(),
            IllegalCoreResourceAllocationException::CoreResourceType::kPackerMulticastStreams);
    }

    return worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
           OperandStreamMap::get_output_index(operand_id);
}

unsigned int WorkerCoreResourcesWH::calculate_multicast_streams_count() const
{
    return worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
           worker_core_resources_wh_constants::gather_multicast_streams_id_range_start + 1;
}

unsigned int WorkerCoreResourcesWH::calculate_general_purpose_streams_count() const
{
    return get_extra_streams_id_range_end() - get_extra_streams_id_range_start() + 1;
}

} // namespace pipegen2