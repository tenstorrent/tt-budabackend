// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tlb_config.hpp"
#include "epoch_q.h"

namespace tt::tlb_config {
#ifdef ARCH_WORMHOLE
std::int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location = std::find(std::cbegin(DEVICE_DATA.ETH_LOCATIONS), std::cend(DEVICE_DATA.ETH_LOCATIONS), target) != std::cend(DEVICE_DATA.ETH_LOCATIONS);
    bool is_tensix_location = std::find(std::cbegin(DEVICE_DATA.T6_X_LOCATIONS), std::cend(DEVICE_DATA.T6_X_LOCATIONS), target.x) != std::cend(DEVICE_DATA.T6_X_LOCATIONS) &&
                              std::find(std::cbegin(DEVICE_DATA.T6_Y_LOCATIONS), std::cend(DEVICE_DATA.T6_Y_LOCATIONS), target.y) != std::cend(DEVICE_DATA.T6_Y_LOCATIONS);
    // implementation migrated from wormhole.py in `src/t6ifc/t6py/packages/tenstorrent/chip/wormhole.py` from tensix repo (t6py-wormhole-bringup branch)
    
    // Special handling for DRAM TLBs : return a 2MB TLB pointing to the start of the Epoch Cmd Queue Table
    // The default 1MB TLB is not used for DRAM cores
    auto DRAM_TLB_IDX = std::find(DEVICE_DATA.DRAM_LOCATIONS.begin(), DEVICE_DATA.DRAM_LOCATIONS.end(), target);
    if (DRAM_TLB_IDX != DEVICE_DATA.DRAM_LOCATIONS.end()) {
        return EPOCH_CMD_QUEUE_TLBS.at(DRAM_TLB_IDX - DEVICE_DATA.DRAM_LOCATIONS.begin());
    }
    
    if (is_eth_location) {
        if (target.y == 6) {
            target.y = 1;
        }

        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        int flat_index = target.y * 8 + target.x;
        int tlb_index = flat_index;
        return tlb_index;

    } else if (is_tensix_location) {
        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        if (target.y >= 6) {
            target.y -= 1;
        }
        target.y -= 1;

        int flat_index = target.y * 8 + target.x;

        // All 80 get single 1MB TLB.
        int tlb_index = DEVICE_DATA.ETH_LOCATIONS.size() + flat_index;

        return tlb_index;
    } else {
        return -1;
    }
}
#endif

#ifdef ARCH_GRAYSKULL
std::int32_t get_static_tlb_index(tt_xy_pair target) {
    // Special handling for DRAM TLBs : return a 2MB TLB pointing to the start of the Epoch Cmd Queue Table
    // The default 1MB TLB is not used for DRAM cores
    auto DRAM_TLB_IDX = std::find(DEVICE_DATA.DRAM_LOCATIONS.begin(), DEVICE_DATA.DRAM_LOCATIONS.end(), target);
    if (DRAM_TLB_IDX != DEVICE_DATA.DRAM_LOCATIONS.end()) {
        return EPOCH_CMD_QUEUE_TLBS.at(DRAM_TLB_IDX - DEVICE_DATA.DRAM_LOCATIONS.begin());
    }
    int flat_index = target.y * DEVICE_DATA.GRID_SIZE_X + target.x;
    if (flat_index == 0) {
        return -1;
    }
    return flat_index;
}
#endif

void configure_static_tlbs(const std::uint32_t& chip, const buda_SocDescriptor& sdesc, std::shared_ptr<tt_device> device) {
    auto statically_mapped_cores = sdesc.workers;
    statically_mapped_cores.insert(statically_mapped_cores.end(), sdesc.ethernet_cores.begin(), sdesc.ethernet_cores.end()); 
    std::int32_t address = 0;
    
    // Setup static TLBs for all worker cores
    for(auto& core : statically_mapped_cores) {
        auto tlb_index = get_static_tlb_index(core);
        device -> configure_tlb(chip, core, tlb_index, address);
    }
    // Setup static TLBs for MMIO mapped data space
    uint64_t peer_dram_offset = DEVICE_DATA.DRAM_CHANNEL_0_PEER2PEER_REGION_START;
    for (uint32_t tlb_id = DYNAMIC_TLB_BASE_INDEX; tlb_id < DYNAMIC_TLB_BASE_INDEX + DYNAMIC_TLB_COUNT; tlb_id++) {
        device -> configure_tlb(chip, tt_xy_pair(DEVICE_DATA.DRAM_CHANNEL_0_X, DEVICE_DATA.DRAM_CHANNEL_0_Y), tlb_id, peer_dram_offset);
        // Align address space of 16MB TLB to 16MB boundary
        peer_dram_offset += DEVICE_DATA.DYNAMIC_TLB_16M_SIZE;
    }
    // Setup static Epoch command queue TLBs
    for(int i = 0; i < DEVICE_DATA.DRAM_LOCATIONS.size(); i++) {
        device -> configure_tlb(chip, DEVICE_DATA.DRAM_LOCATIONS[i],  EPOCH_CMD_QUEUE_TLBS[i], epoch_queue::get_epoch_alloc_queue_sync_addr());
        // Align address space to TLB size
    }
    device -> setup_core_to_tlb_map([] (tt_xy_pair core) {return get_static_tlb_index(core);});
}

// void activate_static_tlbs(std::shared_ptr<tt_device> device) {
//     device -> tlbs_init = true;
// }

std::unordered_map<std::string, std::int32_t> get_dynamic_tlb_config() {
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config;

    dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = MEM_SMALL_READ_WRITE_TLB;
    dynamic_tlb_config ["REG_TLB"] = DEVICE_DATA.REG_TLB;
    return dynamic_tlb_config;
}

} // namespace tt::tlb_config