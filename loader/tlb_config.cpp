// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tlb_config.hpp"

#include "device/blackhole_implementation.h"
#include "device/grayskull_implementation.h"
#include "device/wormhole_implementation.h"
#include "epoch_q.h"

namespace tt::tlb_config {

namespace grayskull {
const std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS = {
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 2,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 3,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 4,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 5,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 6,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 7,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 8,
    tt::umd::grayskull::TLB_BASE_INDEX_2M + 9};
static constexpr uint32_t DYNAMIC_TLB_COUNT = 16;
static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB = tt::umd::grayskull::TLB_BASE_INDEX_2M + 1;
static constexpr unsigned int DYNAMIC_TLB_BASE_INDEX = tt::umd::grayskull::MEM_LARGE_READ_TLB + 1;

std::int32_t get_static_tlb_index(tt_xy_pair target) {
    // Special handling for DRAM TLBs : return a 2MB TLB pointing to the start of the Epoch Cmd Queue Table
    // The default 1MB TLB is not used for DRAM cores
    auto DRAM_TLB_IDX =
        std::find(tt::umd::grayskull::DRAM_LOCATIONS.begin(), tt::umd::grayskull::DRAM_LOCATIONS.end(), target);
    if (DRAM_TLB_IDX != tt::umd::grayskull::DRAM_LOCATIONS.end()) {
        return EPOCH_CMD_QUEUE_TLBS.at(DRAM_TLB_IDX - tt::umd::grayskull::DRAM_LOCATIONS.begin());
    }
    int flat_index = target.y * tt::umd::grayskull::GRID_SIZE_X + target.x;
    if (flat_index == 0) {
        return -1;
    }
    return flat_index;
}

}  // namespace grayskull

namespace wormhole {

const std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS = {
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 96,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 97,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 98,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 99,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 100,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 101,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 102,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 103,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 104,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 105,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 106,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 107,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 108,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 109,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 110,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 111,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 112,
    tt::umd::wormhole::TLB_BASE_INDEX_1M + 113};
static constexpr uint32_t DYNAMIC_TLB_COUNT = 16;
static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB = tt::umd::wormhole::TLB_BASE_INDEX_2M + 1;
static constexpr uint32_t DYNAMIC_TLB_BASE_INDEX = tt::umd::wormhole::MEM_LARGE_READ_TLB + 1;

std::int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location =
        std::find(std::cbegin(tt::umd::wormhole::ETH_LOCATIONS), std::cend(tt::umd::wormhole::ETH_LOCATIONS), target) !=
        std::cend(tt::umd::wormhole::ETH_LOCATIONS);
    bool is_tensix_location =
        std::find(
            std::cbegin(tt::umd::wormhole::T6_X_LOCATIONS), std::cend(tt::umd::wormhole::T6_X_LOCATIONS), target.x) !=
            std::cend(tt::umd::wormhole::T6_X_LOCATIONS) &&
        std::find(
            std::cbegin(tt::umd::wormhole::T6_Y_LOCATIONS), std::cend(tt::umd::wormhole::T6_Y_LOCATIONS), target.y) !=
            std::cend(tt::umd::wormhole::T6_Y_LOCATIONS);
    // implementation migrated from wormhole.py in `src/t6ifc/t6py/packages/tenstorrent/chip/wormhole.py` from tensix
    // repo (t6py-wormhole-bringup branch)

    // Special handling for DRAM TLBs : return a 2MB TLB pointing to the start of the Epoch Cmd Queue Table
    // The default 1MB TLB is not used for DRAM cores
    auto DRAM_TLB_IDX =
        std::find(tt::umd::wormhole::DRAM_LOCATIONS.begin(), tt::umd::wormhole::DRAM_LOCATIONS.end(), target);
    if (DRAM_TLB_IDX != tt::umd::wormhole::DRAM_LOCATIONS.end()) {
        return EPOCH_CMD_QUEUE_TLBS.at(DRAM_TLB_IDX - tt::umd::wormhole::DRAM_LOCATIONS.begin());
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
        int tlb_index = tt::umd::wormhole::ETH_LOCATIONS.size() + flat_index;

        return tlb_index;
    } else {
        return -1;
    }
}

}  // namespace wormhole

namespace blackhole {

const std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS = {};
static constexpr uint32_t DYNAMIC_TLB_COUNT = 16;
static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB = tt::umd::blackhole::MEM_SMALL_READ_WRITE_TLB;
static constexpr uint32_t DYNAMIC_TLB_BASE_INDEX = tt::umd::blackhole::DYNAMIC_TLB_BASE_INDEX;
static constexpr uint32_t TENSIX_STATIC_TLB_START = 38;
static constexpr uint32_t NUM_PORTS_PER_DRAM_CHANNEL = 3;
static constexpr uint32_t NUM_DRAM_CHANNELS = 8;

std::int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location =
        std::find(
            std::cbegin(tt::umd::blackhole::ETH_LOCATIONS), std::cend(tt::umd::blackhole::ETH_LOCATIONS), target) !=
        std::cend(tt::umd::blackhole::ETH_LOCATIONS);
    bool is_tensix_location =
        std::find(
            std::cbegin(tt::umd::blackhole::T6_X_LOCATIONS), std::cend(tt::umd::blackhole::T6_X_LOCATIONS), target.x) !=
            std::cend(tt::umd::blackhole::T6_X_LOCATIONS) &&
        std::find(
            std::cbegin(tt::umd::blackhole::T6_Y_LOCATIONS), std::cend(tt::umd::blackhole::T6_Y_LOCATIONS), target.y) !=
            std::cend(tt::umd::blackhole::T6_Y_LOCATIONS);
    // implementation migrated from blackhole.py in `src/t6ifc/t6py/packages/tenstorrent/chip/blackhole.py` from tensix
    // repo (t6py-blackhole-bringup branch)

    auto DRAM_TLB_IDX =
        std::find(tt::umd::blackhole::DRAM_LOCATIONS.begin(), tt::umd::blackhole::DRAM_LOCATIONS.end(), target);
    if (DRAM_TLB_IDX != tt::umd::blackhole::DRAM_LOCATIONS.end()) {
        auto dram_index = DRAM_TLB_IDX - tt::umd::blackhole::DRAM_LOCATIONS.begin();
        // We have 3 ports per DRAM channel so we divide index by 3 to map
        // all the channels of the same core to the same TLB
        return tt::umd::blackhole::TLB_BASE_INDEX_4G + (dram_index / NUM_PORTS_PER_DRAM_CHANNEL);
    }

    if (is_eth_location) {
        // TODO(pjanevski): fix calculation of tlb index for eth cores
        // once we can bringup Blackhole with eth
        return -1;
    } else if (is_tensix_location) {
        // BH worker cores are starting from x = 1, y = 2
        target.y-=2;
        target.x--;

        if (target.x >= 8) {
            target.x -= 2;
        }

        int flat_index = target.y * 14 + target.x;

        int tlb_index = TENSIX_STATIC_TLB_START + flat_index;

        return tlb_index;
    }

    return -1;
}

// Returns first port of dram channel passed as the argument.
// This core will be used for configuring 4GB TLB.
tt_xy_pair ddr_to_noc0(unsigned i) {
    return tt::umd::blackhole::DRAM_LOCATIONS[NUM_PORTS_PER_DRAM_CHANNEL * i];
}

}  // namespace blackhole

void configure_static_tlbs(
    tt::ARCH arch, const std::uint32_t& chip, const buda_SocDescriptor& sdesc, std::shared_ptr<tt_device> device) {
    using get_static_tlb_index_ptr = std::int32_t (*)(tt_xy_pair);
    get_static_tlb_index_ptr get_static_tlb_index;
    uint32_t DRAM_CHANNEL_0_PEER2PEER_REGION_START, DYNAMIC_TLB_BASE_INDEX, DYNAMIC_TLB_COUNT, DRAM_CHANNEL_0_X,
        DRAM_CHANNEL_0_Y, DYNAMIC_TLB_16M_SIZE, DYNAMIC_TLB_2M_SIZE;
    std::vector<tt_xy_pair> DRAM_LOCATIONS;
    std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS;

    if (arch == tt::ARCH::GRAYSKULL) {
        get_static_tlb_index = grayskull::get_static_tlb_index;
        DRAM_CHANNEL_0_PEER2PEER_REGION_START = tt::umd::grayskull::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
        DYNAMIC_TLB_BASE_INDEX = grayskull::DYNAMIC_TLB_BASE_INDEX;
        DYNAMIC_TLB_COUNT = grayskull::DYNAMIC_TLB_COUNT;
        DRAM_CHANNEL_0_X = tt::umd::grayskull::DRAM_CHANNEL_0_X;
        DRAM_CHANNEL_0_Y = tt::umd::grayskull::DRAM_CHANNEL_0_Y;
        DYNAMIC_TLB_16M_SIZE = tt::umd::grayskull::DYNAMIC_TLB_16M_SIZE;
        DRAM_LOCATIONS.assign(tt::umd::grayskull::DRAM_LOCATIONS.begin(), tt::umd::grayskull::DRAM_LOCATIONS.end());
        EPOCH_CMD_QUEUE_TLBS = grayskull::EPOCH_CMD_QUEUE_TLBS;
        DYNAMIC_TLB_2M_SIZE = 0;
    } else if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        get_static_tlb_index = wormhole::get_static_tlb_index;
        DRAM_CHANNEL_0_PEER2PEER_REGION_START = tt::umd::wormhole::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
        DYNAMIC_TLB_BASE_INDEX = wormhole::DYNAMIC_TLB_BASE_INDEX;
        DYNAMIC_TLB_COUNT = wormhole::DYNAMIC_TLB_COUNT;
        DRAM_CHANNEL_0_X = tt::umd::wormhole::DRAM_CHANNEL_0_X;
        DRAM_CHANNEL_0_Y = tt::umd::wormhole::DRAM_CHANNEL_0_Y;
        DYNAMIC_TLB_16M_SIZE = tt::umd::wormhole::DYNAMIC_TLB_16M_SIZE;
        DRAM_LOCATIONS.assign(tt::umd::wormhole::DRAM_LOCATIONS.begin(), tt::umd::wormhole::DRAM_LOCATIONS.end());
        EPOCH_CMD_QUEUE_TLBS = wormhole::EPOCH_CMD_QUEUE_TLBS;
        DYNAMIC_TLB_2M_SIZE = 0;
    } else if (arch == tt::ARCH::BLACKHOLE) {
        get_static_tlb_index = blackhole::get_static_tlb_index;
        DRAM_CHANNEL_0_PEER2PEER_REGION_START = tt::umd::blackhole::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
        DYNAMIC_TLB_BASE_INDEX = blackhole::DYNAMIC_TLB_BASE_INDEX;
        DYNAMIC_TLB_COUNT = blackhole::DYNAMIC_TLB_COUNT;
        DRAM_CHANNEL_0_X = tt::umd::blackhole::DRAM_CHANNEL_0_X;
        DRAM_CHANNEL_0_Y = tt::umd::blackhole::DRAM_CHANNEL_0_Y;
        DYNAMIC_TLB_2M_SIZE = tt::umd::blackhole::DYNAMIC_TLB_2M_SIZE;
        DRAM_LOCATIONS.assign(tt::umd::blackhole::DRAM_LOCATIONS.begin(), tt::umd::blackhole::DRAM_LOCATIONS.end());
        EPOCH_CMD_QUEUE_TLBS = blackhole::EPOCH_CMD_QUEUE_TLBS;
        DYNAMIC_TLB_16M_SIZE = 0;
    } else {
        throw std::runtime_error("Unsupported architecture");
    }

    if (arch != tt::ARCH::BLACKHOLE) {

        auto statically_mapped_cores = sdesc.workers;
        statically_mapped_cores.insert(
            statically_mapped_cores.end(), sdesc.ethernet_cores.begin(), sdesc.ethernet_cores.end());
        std::int32_t address = 0;

        // Setup static TLBs for all worker cores
        for (auto& core : statically_mapped_cores) {
            auto tlb_index = get_static_tlb_index(core);
            device->configure_tlb(chip, core, tlb_index, address, TLB_DATA::Posted);
        }
        // Setup static TLBs for MMIO mapped data space
        uint64_t peer_dram_offset = DRAM_CHANNEL_0_PEER2PEER_REGION_START;
        for (uint32_t tlb_id = DYNAMIC_TLB_BASE_INDEX; tlb_id < DYNAMIC_TLB_BASE_INDEX + DYNAMIC_TLB_COUNT; tlb_id++) {
            device->configure_tlb(
                chip, tt_xy_pair(DRAM_CHANNEL_0_X, DRAM_CHANNEL_0_Y), tlb_id, peer_dram_offset, TLB_DATA::Posted);
            // Align address space of 16MB TLB to 16MB boundary
            peer_dram_offset += DYNAMIC_TLB_16M_SIZE;
        }
        // Setup static Epoch command queue TLBs
        for (int i = 0; i < DRAM_LOCATIONS.size(); i++) {
            device->configure_tlb(
                chip,
                DRAM_LOCATIONS[i],
                EPOCH_CMD_QUEUE_TLBS[i],
                epoch_queue::get_epoch_alloc_queue_sync_addr(),
                TLB_DATA::Posted);
            // Align address space to TLB size
        }
        device->setup_core_to_tlb_map([get_static_tlb_index](tt_xy_pair core) { return get_static_tlb_index(core); });
    } else {
        
        auto worker_cores = sdesc.workers;
        std::int32_t address = 0;

        // Setup static TLBs for all worker cores
        for (auto& core : worker_cores) {
            auto tlb_index = get_static_tlb_index(core);
            device->configure_tlb(chip, core, tlb_index, address, TLB_DATA::Posted);
        }

        // Setup static 4GB tlbs for DRAM cores
        std::uint32_t dram_addr = 0;
        for (std::uint32_t dram_channel = 0; dram_channel < blackhole::NUM_DRAM_CHANNELS; dram_channel++) {
            tt_xy_pair dram_core = blackhole::ddr_to_noc0(dram_channel);
            auto tlb_index = tt::umd::blackhole::TLB_COUNT_2M + dram_channel;
            device->configure_tlb(chip, dram_core, tlb_index, dram_addr, TLB_DATA::Posted);
        }

        device->setup_core_to_tlb_map([get_static_tlb_index](tt_xy_pair core) { return get_static_tlb_index(core); });
    }

}

std::unordered_map<std::string, std::int32_t> get_dynamic_tlb_config(tt::ARCH arch) {
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config;

    if (arch == tt::ARCH::GRAYSKULL) {
        dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = grayskull::MEM_SMALL_READ_WRITE_TLB;
        dynamic_tlb_config["REG_TLB"] = tt::umd::grayskull::REG_TLB;
    } else if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = wormhole::MEM_SMALL_READ_WRITE_TLB;
        dynamic_tlb_config["REG_TLB"] = tt::umd::wormhole::REG_TLB;
    } else if (arch == tt::ARCH::BLACKHOLE) {
        dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = blackhole::MEM_SMALL_READ_WRITE_TLB;
        dynamic_tlb_config["REG_TLB"] = tt::umd::blackhole::REG_TLB;
    } else {
        throw std::runtime_error("Unsupported architecture");
    }
    return dynamic_tlb_config;
}

}  // namespace tt::tlb_config
