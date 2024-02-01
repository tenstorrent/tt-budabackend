// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources_wh.h"

#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

WorkerCoreResourcesWH::WorkerCoreResourcesWH(const tt_cxy_pair& core_physical_location) :
    WorkerCoreResources(core_physical_location),
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
        ERROR("WorkerCoreResourcesWH: Out of available multicast streams on core " << get_physical_location().str());
    }

    return m_next_available_gather_multicast_stream_id++;
}

StreamId WorkerCoreResourcesWH::get_packer_multicast_stream_id(int operand_id)
{
    StreamId stream_id = worker_core_resources_wh_constants::gather_multicast_streams_id_range_end - 
                         OperandStreamMap::get_output_index(operand_id);

    ASSERT(stream_id >= worker_core_resources_wh_constants::gather_multicast_streams_id_range_start,
           "Trying to allocate packer-multicast stream which falls out of the allowed multicast range");

    return stream_id;
}

unsigned int WorkerCoreResourcesWH::calculate_multicast_streams_count() const
{
    return worker_core_resources_wh_constants::gather_multicast_streams_id_range_end - 
           worker_core_resources_wh_constants::gather_multicast_streams_id_range_start + 1;
}

} // namespace pipegen2