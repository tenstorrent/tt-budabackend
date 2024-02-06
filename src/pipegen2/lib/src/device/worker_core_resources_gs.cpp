// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources_gs.h"

#include "device/core_resources_constants.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

WorkerCoreResourcesGS::WorkerCoreResourcesGS(const tt_cxy_pair& core_physical_location) :
    WorkerCoreResources(core_physical_location),
    m_next_available_gather_multicast_stream_id(
        worker_core_resources_gs_constants::gather_multicast_streams_id_range_start)
{
}

StreamId WorkerCoreResourcesGS::get_next_available_gather_stream_id()
{
    return get_next_available_gather_multicast_stream_id();
}

StreamId WorkerCoreResourcesGS::get_next_available_multicast_stream_id()
{
    return get_next_available_gather_multicast_stream_id();
}

StreamId WorkerCoreResourcesGS::get_next_available_gather_multicast_stream_id()
{
    if (m_next_available_gather_multicast_stream_id >
        worker_core_resources_gs_constants::gather_multicast_streams_id_range_end)
    {
        ERROR("WorkerCoreResourcesGS: Out of available streams used for gather and multicast on core "
              << get_physical_location().str());
    }

    return m_next_available_gather_multicast_stream_id++;
}

StreamId WorkerCoreResourcesGS::get_packer_multicast_stream_id(int operand_id)
{
    ERROR("WorkerCoreResourcesGS: Can't allocate packer-multicast streams on Grayskull");
}

unsigned int WorkerCoreResourcesGS::calculate_multicast_streams_count() const
{
    return worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
           worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1;
}

} // namespace pipegen2