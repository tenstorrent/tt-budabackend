#include "device/l1/tile_header_buffers_allocation.h"

#include "device/ethernet_core_resources.h"
#include "device/resource_manager.h"
#include "device/worker_core_resources.h"

namespace pipegen2 {

void ChipWideTHBAllocationStrategy::allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) {
    std::unordered_map<decltype(tt_cxy_pair::chip), std::set<unsigned int>> chip_to_tile_sizes_worker_cores;

    for (const auto& it : thb_allocation_info.core_to_tile_sizes) {
        if (m_worker_cores_resources.find(it.first) == m_worker_cores_resources.end()) {
            // Skip this tile size for non-worker core.
            continue;
        }
        chip_to_tile_sizes_worker_cores[it.first.chip].insert(it.second.begin(), it.second.end());
    }

    // By using std::set we make sure that first tile size in loop is the smallest one, and it will be allocated at a
    // predefined address, while others, if they exist, will be allocated in extra space allocated in L1.
    for (const auto& worker_core_resources_it : m_worker_cores_resources) {
        if (thb_allocation_info.core_to_tile_sizes.find(worker_core_resources_it.first) ==
            thb_allocation_info.core_to_tile_sizes.end()) {
            continue;
        }

        for (const auto& tile_size : chip_to_tile_sizes_worker_cores.at(worker_core_resources_it.first.chip)) {
            worker_core_resources_it.second->allocate_l1_tile_header_buffer(tile_size);
        }
    }

    for (const auto& eth_core_resources_it : m_ethernet_cores_resources) {
        const auto core_to_tile_sizes_it = thb_allocation_info.core_to_tile_sizes.find(eth_core_resources_it.first);
        if (core_to_tile_sizes_it == thb_allocation_info.core_to_tile_sizes.end()) {
            continue;
        }

        for (const auto& tile_size : core_to_tile_sizes_it->second) {
            eth_core_resources_it.second->allocate_l1_tile_header_buffer(tile_size);
        }
    }
}

}  // namespace pipegen2