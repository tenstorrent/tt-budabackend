// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/resource_manager.h"

#include "dram_address_map.h"
#include "eth_l1_address_map.h"
#include "l1_address_map.h"

#include "device/ethernet_core_resources.h"
#include "device/worker_core_resources.h"
#include "device/worker_core_resources_gs.h"
#include "device/worker_core_resources_wh.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    ResourceManager::ResourceManager(std::unique_ptr<SoCInfo> soc_info) : m_soc_info(std::move(soc_info))
    {
        for (ChipId chip_id : m_soc_info->get_chip_ids())
        {
            for (const tt_cxy_pair& core_physical_location : m_soc_info->get_worker_cores_physical_locations(chip_id))
            {
                m_worker_cores_resources.emplace(
                    core_physical_location,
                    create_worker_core_resources(m_soc_info->get_device_arch(), core_physical_location));
            }
            for (const tt_cxy_pair& core_physical_location : m_soc_info->get_eth_cores_physical_locations(chip_id))
            {
                m_eth_cores_resources.emplace(
                    core_physical_location,
                    create_eth_core_resources(m_soc_info->get_device_arch(), core_physical_location));
            }
        }
    }

    ResourceManager::~ResourceManager() = default;

    std::unique_ptr<WorkerCoreResources> ResourceManager::create_worker_core_resources(
        tt::ARCH device_arch,
        const tt_cxy_pair& core_physical_location)
    {
        switch (device_arch)
        {
        case tt::ARCH::GRAYSKULL:
            return std::make_unique<WorkerCoreResourcesGS>(core_physical_location);
        case tt::ARCH::WORMHOLE:
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WorkerCoreResourcesWH>(core_physical_location);
        default:
            ERROR("ResourceManager: Unsupported device architecture");
        }
    }

    std::unique_ptr<EthernetCoreResources> ResourceManager::create_eth_core_resources(
        tt::ARCH device_arch,
        const tt_cxy_pair& core_physical_location)
    {
        if (device_arch == tt::ARCH::GRAYSKULL)
        {
            ERROR("ResourceManager: Unexpected architecture for ethernet core");
        }
        return std::make_unique<EthernetCoreResources>(core_physical_location);
    }

    WorkerCoreResources* ResourceManager::get_worker_core_resources(const tt_cxy_pair& core_physical_location) const
    {
        auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
        if (worker_core_resources_it == m_worker_cores_resources.end())
        {
            ERROR("ResourceManager: There is no worker core at a location: " << core_physical_location.str());
        }

        return worker_core_resources_it->second.get();
    }

    EthernetCoreResources* ResourceManager::get_eth_core_resources(const tt_cxy_pair& core_physical_location) const
    {
        auto eth_core_resources_it = m_eth_cores_resources.find(core_physical_location);
        if (eth_core_resources_it == m_eth_cores_resources.end())
        {
            ERROR("ResourceManager: There is no ethernet core at a location: " << core_physical_location.str());
        }

        return eth_core_resources_it->second.get();
    }

    CoreResources* ResourceManager::get_core_resources(const tt_cxy_pair& core_physical_location) const
    {
        auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
        if (worker_core_resources_it == m_worker_cores_resources.end())
        {
            auto eth_core_resources_it = m_eth_cores_resources.find(core_physical_location);
            if (eth_core_resources_it == m_eth_cores_resources.end())
            {
                ERROR("ResourceManager: There is no routing core at a location: " << core_physical_location.str());
            }
            return eth_core_resources_it->second.get();
        }

        return worker_core_resources_it->second.get();
    }

    StreamId ResourceManager::allocate_packer_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id)
    {
        if (is_ethernet_core(worker_core_physical_location))
        {
            return get_eth_core_resources(worker_core_physical_location)->allocate_general_purpose_stream();
        }

        return get_worker_core_resources(worker_core_physical_location)->allocate_packer_stream(operand_id);
    }

    StreamId ResourceManager::allocate_unpacker_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id)
    {
        return get_worker_core_resources(worker_core_physical_location)->allocate_unpacker_stream(operand_id);
    }

    StreamId ResourceManager::allocate_intermed_stream(const tt_cxy_pair& worker_core_physical_location, int operand_id)
    {
        return get_worker_core_resources(worker_core_physical_location)->allocate_intermed_stream(operand_id);
    }

    StreamId ResourceManager::allocate_gather_stream(const tt_cxy_pair& core_physical_location)
    {
        return get_core_resources(core_physical_location)->allocate_gather_stream();
    }

    StreamId ResourceManager::allocate_multicast_stream(const tt_cxy_pair& core_physical_location)
    {
        return get_core_resources(core_physical_location)->allocate_multicast_stream();
    }

    StreamId ResourceManager::allocate_packer_multicast_stream(const tt_cxy_pair& worker_core_physical_location,
                                                               int operand_id)
    {
        return get_worker_core_resources(worker_core_physical_location)->allocate_packer_multicast_stream(operand_id);
    }

    StreamId ResourceManager::allocate_general_purpose_stream(const tt_cxy_pair& core_physical_location)
    {
        return get_core_resources(core_physical_location)->allocate_general_purpose_stream();
    }

    StreamId ResourceManager::allocate_ethernet_stream(const tt_cxy_pair& eth_core_physical_location)
    {
        return get_eth_core_resources(eth_core_physical_location)->allocate_ethernet_stream();
    }

    void ResourceManager::allocate_l1_extra_tile_headers_space(
        const std::unordered_map<tt_cxy_pair, std::unordered_set<unsigned int>>& core_to_msg_sizes)
    {
        // Allocate extra space for tile headers on each core. For Ethernet cores, we allocate space in isolation for
        // each core. However, for worker cores, we allocate same amount of space for all cores on a chip just to keep
        // their L1 memory layout consistent. This is mostly to be able to allocate same destination addresses for
        // multicast streams.
        // TODO: Investigate if we can optimize worker core tile header buffer allocation to save some memory. This
        // could lead significant L1 memory savings as for each extra tile header we allocate 32KB of space today.
        std::unordered_set<unsigned int> all_different_tile_headers;
        for (const auto& it : core_to_msg_sizes)
        {
            all_different_tile_headers.insert(it.second.begin(), it.second.end());
        }

        for (const auto& worker_it : m_worker_cores_resources)
        {
            worker_it.second->allocate_l1_extra_tile_headers_space(all_different_tile_headers.size() - 1);
        }

        for (const auto& eth_it : m_eth_cores_resources)
        {
            const auto core_to_msg_sizes_it = core_to_msg_sizes.find(eth_it.first);
            const unsigned int num_extra_tile_headers = core_to_msg_sizes_it != core_to_msg_sizes.end() ?
                core_to_msg_sizes_it->second.size() - 1 : 0;

            eth_it.second->allocate_l1_extra_tile_headers_space(num_extra_tile_headers);
        }
    }

    unsigned int ResourceManager::allocate_l1_extra_overlay_blob_space(const tt_cxy_pair& core_physical_location,
                                                                       unsigned int blob_size_in_bytes)
    {
        const bool is_ethernet_core = this->is_ethernet_core(core_physical_location);
        unsigned int min_blob_size = is_ethernet_core ?
            eth_l1_mem::address_map::OVERLAY_BLOB_SIZE : l1_mem::address_map::OVERLAY_BLOB_SIZE;

        if (blob_size_in_bytes == 0)
        {
            // If blob size is not specified, then we check for blob size in address map. It considers environment
            // variable TT_BACKEND_OVERLAY_MAX_EXTRA_BLOB_SIZE.
            blob_size_in_bytes = is_ethernet_core ?
                eth_l1_mem::address_map::OVERLAY_FULL_BLOB_SIZE() : dram_mem::address_map::OVERLAY_FULL_BLOB_SIZE();
        }

        if (blob_size_in_bytes <= min_blob_size)
        {
            // No need to allocate extra space.
            return 0;
        }

        // Calculate difference and trim it to 4KB boundary.
        unsigned int extra_blob_size_in_bytes = (blob_size_in_bytes - min_blob_size) & 0xFFFFF000;
        get_core_resources(core_physical_location)->allocate_l1_extra_overlay_blob_space(extra_blob_size_in_bytes);

        return extra_blob_size_in_bytes;
    }

    unsigned int ResourceManager::allocate_core_l1_data_buffer(const tt_cxy_pair& core_physical_location,
                                                               unsigned int size_in_bytes)
    {
        return get_core_resources(core_physical_location)->allocate_l1_data_buffer(size_in_bytes);
    }

    unsigned int ResourceManager::allocate_kernel_input(const tt_cxy_pair& core_physical_location)
    {
        return get_core_resources(core_physical_location)->allocate_kernel_input();
    }

    unsigned int ResourceManager::allocate_kernel_output(const tt_cxy_pair& core_physical_location)
    {
        return get_core_resources(core_physical_location)->allocate_kernel_output();
    }

    unsigned int ResourceManager::get_multicast_streams_count(const tt_cxy_pair& core_physical_location) const
    {
        return get_core_resources(core_physical_location)->get_multicast_streams_count();
    }
}