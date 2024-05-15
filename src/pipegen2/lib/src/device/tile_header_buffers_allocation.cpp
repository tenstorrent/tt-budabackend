#include "device/tile_header_buffers_allocation.h"

#include "device/resource_manager.h"
#include "device/worker_core_resources.h"
#include "device/ethernet_core_resources.h"

namespace pipegen2
{

void ChipWideTHBAllocationStrategy::allocate_tile_header_buffers(const THBAllocationInfo& thb_allocation_info)
{
    std::unordered_map<decltype(tt_cxy_pair::chip), std::set<unsigned int>> chip_to_tile_sizes_worker_cores;

    for (const auto& it : thb_allocation_info.core_to_msg_sizes)
    {
        if (m_worker_cores_resources.find(it.first) == m_worker_cores_resources.end())
        {
            // Skip this tile size for non-worker core.
            continue;
        }
        chip_to_tile_sizes_worker_cores[it.first.chip].insert(it.second.begin(), it.second.end());
    }

    for (const auto& worker_core_resources_it : m_worker_cores_resources)
    {
        if (thb_allocation_info.core_to_msg_sizes.find(worker_core_resources_it.first) ==
            thb_allocation_info.core_to_msg_sizes.end())
        {
            continue;
        }

        for (const auto& tile_size : chip_to_tile_sizes_worker_cores.at(worker_core_resources_it.first.chip))
        {
            worker_core_resources_it.second->allocate_tile_header_buffer(tile_size);
        }
    }

    for (const auto& eth_core_resources_it : m_ethernet_cores_resources)
    {
        const auto core_to_msg_sizes_it = thb_allocation_info.core_to_msg_sizes.find(eth_core_resources_it.first);
        if (core_to_msg_sizes_it == thb_allocation_info.core_to_msg_sizes.end())
        {
            continue;
        }

        for (const auto& tile_size : core_to_msg_sizes_it->second)
        {
            eth_core_resources_it.second->allocate_tile_header_buffer(tile_size);
        }
    }
}

} // namespace pipegen2