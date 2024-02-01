// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/ethernet_core_resources.h"

#include "eth_l1_address_map.h"

#include "device/core_resources_constants.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

EthernetCoreResources::EthernetCoreResources(const tt_cxy_pair& core_physical_location) :
    CoreResources(core_physical_location,
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
            ERROR("EthernetCoreResources: Out of available ethernet streams on core " << get_physical_location().str());
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
        ERROR("EthernetCoreResources: Out of available streams used for gather and multicast on core "
              << get_physical_location().str());
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
        ERROR("EthernetCoreResources: Out of available extra streams on core " << get_physical_location().str());
    }

    return m_next_available_extra_stream_id++;
}

unsigned int EthernetCoreResources::calculate_multicast_streams_count() const
{
    return ethernet_core_resources_constants::gather_multicast_streams_id_range_end -
           ethernet_core_resources_constants::gather_multicast_streams_id_range_start + 1;
}

} // namespace pipegen2