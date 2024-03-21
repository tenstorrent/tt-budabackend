// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/worker_core_resources_gs.h"

#include "device/core_resources_constants.h"
#include "pipegen2_exceptions.h"
#include "utils/logger.hpp"

namespace pipegen2
{

WorkerCoreResourcesGS::WorkerCoreResourcesGS(const tt_cxy_pair& core_physical_location, 
                                             const tt_cxy_pair& core_logical_location) :
    WorkerCoreResources(core_physical_location, core_logical_location),
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
        const unsigned int gather_mcast_streams_count = get_multicast_streams_count();
        throw OutOfCoreResourcesException(
            "Out of available gather / multicast streams on a worker core " + get_physical_location().str() +
            ". Available number of gather / multicast streams per GS worker core is " +
            std::to_string(gather_mcast_streams_count) + ".",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kGatherMulticastStreams,
            gather_mcast_streams_count);
    }

    return m_next_available_gather_multicast_stream_id++;
}

StreamId WorkerCoreResourcesGS::get_packer_multicast_stream_id(int operand_id)
{
    throw IllegalCoreResourceAllocationException(
        "Can't allocate packer-multicast streams on Grayskull since that feature is not supported due to FW code size "
        "limitations Grayskull has.",
        get_physical_location(),
        IllegalCoreResourceAllocationException::CoreResourceType::kPackerMulticastStreams);
}

unsigned int WorkerCoreResourcesGS::calculate_multicast_streams_count() const
{
    return worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
           worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1;
}

unsigned int WorkerCoreResourcesGS::calculate_general_purpose_streams_count() const
{
    return get_extra_streams_id_range_end() - get_extra_streams_id_range_start() + 1;
}

} // namespace pipegen2