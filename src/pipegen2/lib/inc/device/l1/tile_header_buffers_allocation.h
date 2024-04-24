#pragma once

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "device/tt_xy_pair.h"
#include "utils/logger.hpp"

namespace pipegen2 {

class EthernetCoreResources;
class WorkerCoreResources;

// Structure that contains information about tile header buffers allocation.
struct THBAllocationInfo {
    // Map of core to set of tile sizes that need to be allocated for that core. Note that using set keeps tile sizes
    // sorted from smallest to largest.
    std::unordered_map<tt_cxy_pair, std::set<unsigned int>> core_to_tile_sizes;

    // Vector of group of cores such that each core in a group must have the same tile header buffers allocated and
    // in the same order.
    std::vector<std::unordered_set<tt_cxy_pair>> grouped_cores;
};

// Interface for tile header buffers allocation strategy.
class THBAllocationStrategy {
   public:
    THBAllocationStrategy(
        std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>>& worker_cores_resources,
        std::unordered_map<tt_cxy_pair, std::unique_ptr<EthernetCoreResources>>& ethernet_cores_resources)
        : m_worker_cores_resources(worker_cores_resources), m_ethernet_cores_resources(ethernet_cores_resources) {}

    virtual ~THBAllocationStrategy() = default;

    // Allocates tile header buffers for all cores based on the given allocation info.
    virtual void allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) = 0;

   protected:
    std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>>& m_worker_cores_resources;

    std::unordered_map<tt_cxy_pair, std::unique_ptr<EthernetCoreResources>>& m_ethernet_cores_resources;
};

// Allocation strategy that allocates tile header buffers so that each worker core has tile header buffers for all
// tile sizes that are present globally on the chip. Ethernet cores have tile header buffers for all tile sizes
// that are present on that core. This is a default allocation strategy.
class ChipWideTHBAllocationStrategy : public THBAllocationStrategy {
   public:
    ChipWideTHBAllocationStrategy(
        std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>>& worker_cores_resources,
        std::unordered_map<tt_cxy_pair, std::unique_ptr<EthernetCoreResources>>& ethernet_cores_resources)
        : THBAllocationStrategy(worker_cores_resources, ethernet_cores_resources) {}

    void allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) override;
};

// TODO: implement better allocation strategy that will not allocate tile header buffers for all tile sizes that are
// present globally, but only for those that are present on a given core.
class CoreIndependentTHBAllocationStrategy : public THBAllocationStrategy {
   public:
    CoreIndependentTHBAllocationStrategy(
        std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCoreResources>>& worker_cores_resources,
        std::unordered_map<tt_cxy_pair, std::unique_ptr<EthernetCoreResources>>& ethernet_cores_resources)
        : THBAllocationStrategy(worker_cores_resources, ethernet_cores_resources) {}

    void allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) override {
        log_assert(false, "CoreIndependentTHBAllocationStrategy not implemented yet");
    }
};

}  // namespace pipegen2