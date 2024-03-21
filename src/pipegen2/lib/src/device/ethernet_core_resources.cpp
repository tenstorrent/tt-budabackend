// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/ethernet_core_resources.h"

#include "eth_l1_address_map.h"

#include "device/core_resources_constants.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"
#include "utils/logger.hpp"

namespace pipegen2
{

EthernetCoreResources::EthernetCoreResources(const tt_cxy_pair& core_physical_location) :
    CoreResources(core_physical_location,
                  core_physical_location /* logical location */,
                  ethernet_core_resources_constants::ethernet_stream_id_range_end + 1,
                  ethernet_core_resources_constants::ethernet_core_num_noc_streams - 1 /* extra_streams_id_range_end */,
                  eth_l1_mem::address_map::DATA_BUFFER_SPACE_BASE,
                  eth_l1_mem::address_map::MAX_SIZE),
    m_next_available_ethernet_stream_id(ethernet_core_resources_constants::ethernet_stream_id_range_start),
    m_next_available_gather_multicast_stream_id(
        ethernet_core_resources_constants::gather_multicast_streams_id_range_start)
{
}

StreamId EthernetCoreResources::allocate_ethernet_stream()
{
    StreamId stream_id = get_next_available_ethernet_stream_id();

    m_allocated_stream_ids.insert(stream_id);

    return stream_id;
}

unsigned int EthernetCoreResources::calculate_ethernet_transfers_capable_streams_count() const
{
    return calculate_ethernet_streams_count() + calculate_multicast_streams_count();
}

StreamId EthernetCoreResources::get_next_available_ethernet_stream_id()
{
    if (m_next_available_ethernet_stream_id > ethernet_core_resources_constants::ethernet_stream_id_range_end)
    {
        // Gather/multicast streams are also capable of doing ethernet transfers, so try using them.
        log_debug(tt::LogPipegen2,
                  "Out of available ethernet streams between {} and {} on core {}, starting allocating from {}",
                  ethernet_core_resources_constants::ethernet_stream_id_range_start,
                  ethernet_core_resources_constants::ethernet_stream_id_range_end,
                  get_physical_location().str(),
                  m_next_available_gather_multicast_stream_id);

        // If gather/multicast pool gets exhausted, catch the exception and rethrow it with proper message to
        // indicate lack of ethernet streams.
        try
        {
            return get_next_available_gather_multicast_stream_id();
        }
        catch (const std::exception& ex)
        {
            const unsigned int streams_capable_of_eth_transfers = calculate_ethernet_transfers_capable_streams_count();
            throw OutOfCoreResourcesException(
                "Out of available ethernet streams on an ethernet core " + get_physical_location().str() + ". " +
                "Available number of streams capable of doing ethernet transfers per ethernet core is " +
                std::to_string(streams_capable_of_eth_transfers) + ".",
                get_physical_location(),
                get_logical_location(),
                OutOfCoreResourcesException::CoreResourceType::kEthernetStreams,
                streams_capable_of_eth_transfers);
        }
    }

    return m_next_available_ethernet_stream_id++;
}

StreamId EthernetCoreResources::get_next_available_gather_stream_id()
{
    return get_next_available_gather_multicast_stream_id();
}

StreamId EthernetCoreResources::get_next_available_multicast_stream_id()
{
    return get_next_available_gather_multicast_stream_id();
}

StreamId EthernetCoreResources::get_next_available_gather_multicast_stream_id()
{
    if (m_next_available_gather_multicast_stream_id >
        ethernet_core_resources_constants::gather_multicast_streams_id_range_end)
    {
        const unsigned int gather_mcast_streams_count = get_multicast_streams_count();
        throw OutOfCoreResourcesException(
            "Out of available gather / multicast streams on an ethernet core " + get_physical_location().str() +
            ". Available number of gather / multicast streams per ethernet core is " +
            std::to_string(gather_mcast_streams_count) + ".",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kGatherMulticastStreams,
            gather_mcast_streams_count);
    }

    return m_next_available_gather_multicast_stream_id++;
}

StreamId EthernetCoreResources::get_next_available_general_purpose_stream_id()
{
    if (m_next_available_extra_stream_id > get_extra_streams_id_range_end())
    {
        log_debug(tt::LogPipegen2,
                  "Out of available extra streams on core {}, starting allocating from {}",
                  get_physical_location().str(),
                  ethernet_core_resources_constants::gather_multicast_streams_id_range_end + 1);

        // Out of dedicated extra streams. Start allocating after gather multicast streams range.
        m_next_available_extra_stream_id = ethernet_core_resources_constants::gather_multicast_streams_id_range_end + 1;
    }
    else if (m_next_available_extra_stream_id == ethernet_core_resources_constants::ethernet_stream_id_range_start)
    {
        // There are not enough extra streams, we entered the ethernet streams range.
        const unsigned int extra_streams_count = calculate_general_purpose_streams_count();
        throw OutOfCoreResourcesException(
            "Out of available extra streams on an ethernet core " + get_physical_location().str() + ". " +
            "Available number of extra (general purpose) streams per ethernet core is " +
            std::to_string(extra_streams_count) + ".",
            get_physical_location(),
            get_logical_location(),
            OutOfCoreResourcesException::CoreResourceType::kGeneralPurposeStreams,
            extra_streams_count);
    }

    return m_next_available_extra_stream_id++;
}

unsigned int EthernetCoreResources::calculate_multicast_streams_count() const
{
    return ethernet_core_resources_constants::gather_multicast_streams_id_range_end -
           ethernet_core_resources_constants::gather_multicast_streams_id_range_start + 1;
}

unsigned int EthernetCoreResources::calculate_ethernet_streams_count() const
{
    return ethernet_core_resources_constants::ethernet_stream_id_range_end -
           ethernet_core_resources_constants::ethernet_stream_id_range_start + 1;
}

unsigned int EthernetCoreResources::calculate_general_purpose_streams_count() const
{
    const unsigned int extra_streams_count = get_extra_streams_id_range_end() - get_extra_streams_id_range_start() + 1;
    const unsigned int additional_extra_streams =
        (ethernet_core_resources_constants::ethernet_stream_id_range_start - 1) -
        (ethernet_core_resources_constants::gather_multicast_streams_id_range_end + 1) + 1;

    return extra_streams_count + additional_extra_streams;
}

unsigned int EthernetCoreResources::get_predefined_tile_header_buffer_addr() const
{
    return eth_l1_mem::address_map::OVERLAY_BLOB_BASE -
           core_resources_constants::tile_header_buffer_allocation_cushion_bytes -
           TileHeaderBuffer::get_tile_header_buffer_size_bytes();
}
} // namespace pipegen2