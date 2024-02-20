// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/resource_manager.h"

#include "common/buda_soc_descriptor.h"
#include "dram_address_map.h"
#include "eth_l1_address_map.h"
#include "l1_address_map.h"
#include "logger.hpp"

#include "device/core_resources.h"
#include "device/ethernet_core_resources.h"
#include "device/ncrisc_resources_checker.h"
#include "device/resource_manager_internal.h"
#include "device/worker_core_resources.h"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_location_utils.h"

namespace pipegen2
{

ResourceManager::ResourceManager(std::unique_ptr<SoCInfo> soc_info) : m_soc_info(std::move(soc_info))
    {
        for (ChipId chip_id : m_soc_info->get_chip_ids())
        {
            for (const tt_cxy_pair& core_physical_location : m_soc_info->get_worker_cores_physical_locations(chip_id))
            {
                const tt_cxy_pair core_logical_location = 
                    convert_physical_core_to_logical(core_physical_location, m_soc_info->get_soc_decriptors());
                m_worker_cores_resources.emplace(
                    core_physical_location,
                    resource_manager_internal::create_worker_core_resources(m_soc_info->get_device_arch(), 
                                                                            core_physical_location, 
                                                                            core_logical_location));
            }

            for (const tt_cxy_pair& core_physical_location : m_soc_info->get_ethernet_cores_physical_locations(chip_id))
            {
                m_ethernet_cores_resources.emplace(
                    core_physical_location,
                    resource_manager_internal::create_ethernet_core_resources(m_soc_info->get_device_arch(), 
                                                                              core_physical_location));
            }
        }
    }

ResourceManager::~ResourceManager() = default;

WorkerCoreResources* ResourceManager::get_worker_core_resources(const tt_cxy_pair& core_physical_location) const
{
    auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
    log_assert(
        worker_core_resources_it != m_worker_cores_resources.end(),
        "ResourceManager: There is no worker core at a location: {}", core_physical_location.str());

    return worker_core_resources_it->second.get();
}

EthernetCoreResources* ResourceManager::get_ethernet_core_resources(const tt_cxy_pair& core_physical_location) const
{
    auto eth_core_resources_it = m_ethernet_cores_resources.find(core_physical_location);
    log_assert(
        eth_core_resources_it != m_ethernet_cores_resources.end(),
        "ResourceManager: There is no ethernet core at a location: {}", core_physical_location.str());

    return eth_core_resources_it->second.get();
}

CoreResources* ResourceManager::get_core_resources(const tt_cxy_pair& core_physical_location) const
{
    auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
    if (worker_core_resources_it == m_worker_cores_resources.end())
    {
        auto eth_core_resources_it = m_ethernet_cores_resources.find(core_physical_location);
        log_assert(
            eth_core_resources_it != m_ethernet_cores_resources.end(),
            "ResourceManager: There is no routing core at a location: {}", core_physical_location.str());

        return eth_core_resources_it->second.get();
    }

    return worker_core_resources_it->second.get();
}

StreamId ResourceManager::allocate_packer_stream(const tt_cxy_pair& core_physical_location, int operand_id)
{
    if (is_ethernet_core(core_physical_location))
    {
        return get_ethernet_core_resources(core_physical_location)->allocate_general_purpose_stream();
    }

    return get_worker_core_resources(core_physical_location)->allocate_packer_stream(operand_id);
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

StreamId ResourceManager::allocate_packer_multicast_stream(
    const tt_cxy_pair& worker_core_physical_location, int operand_id)
{
    return get_worker_core_resources(worker_core_physical_location)->allocate_packer_multicast_stream(operand_id);
}

StreamId ResourceManager::allocate_general_purpose_stream(const tt_cxy_pair& core_physical_location)
{
    return get_core_resources(core_physical_location)->allocate_general_purpose_stream();
}

StreamId ResourceManager::allocate_ethernet_stream(const tt_cxy_pair& eth_core_physical_location)
{
    return get_ethernet_core_resources(eth_core_physical_location)->allocate_ethernet_stream();
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

    for (const auto& eth_it : m_ethernet_cores_resources)
    {
        const auto core_to_msg_sizes_it = core_to_msg_sizes.find(eth_it.first);
        const unsigned int num_extra_tile_headers = core_to_msg_sizes_it != core_to_msg_sizes.end() ?
            core_to_msg_sizes_it->second.size() - 1 : 0;

        eth_it.second->allocate_l1_extra_tile_headers_space(num_extra_tile_headers);
    }
}

unsigned int ResourceManager::allocate_l1_extra_overlay_blob_space(
    const tt_cxy_pair& core_physical_location, unsigned int blob_size_in_bytes)
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

unsigned int ResourceManager::allocate_core_l1_data_buffer(
    const tt_cxy_pair& core_physical_location, unsigned int size_in_bytes) const
{
    return get_core_resources(core_physical_location)->allocate_l1_data_buffer(size_in_bytes);
}

unsigned int ResourceManager::allocate_kernel_input(const tt_cxy_pair& core_physical_location)
{
    return get_core_resources(core_physical_location)->allocate_kernel_input();
}

unsigned int ResourceManager::allocate_core_l1_stream_buffer(const StreamNode* stream_node, 
                                                             const unsigned int size_in_bytes) const
{
    unsigned int allocated_buffer_address = allocate_core_l1_data_buffer(stream_node->get_physical_location(), size_in_bytes);
    track_l1_stream_buffer_allocation_on_core(stream_node, size_in_bytes, allocated_buffer_address);

    return allocated_buffer_address;
}

unsigned int ResourceManager::allocate_kernel_output(const tt_cxy_pair& core_physical_location)
{
    return get_core_resources(core_physical_location)->allocate_kernel_output();
}

unsigned int ResourceManager::get_multicast_streams_count(const tt_cxy_pair& core_physical_location) const
{
    return get_core_resources(core_physical_location)->get_multicast_streams_count();
}

void ResourceManager::track_l1_stream_buffer_allocation_on_core(const StreamNode* stream_node, 
                                                                const unsigned int buffer_size,  
                                                                const unsigned int buffer_address) const
{
    get_core_resources(stream_node->get_physical_location())->track_stream_buffer_allocation(
        stream_node, buffer_size, buffer_address);
}

void ResourceManager::check_if_out_of_l1_data_buffers_memory(const tt_cxy_pair& core_physical_location) const
{
    return get_core_resources(core_physical_location)->check_if_out_of_l1_data_buffers_memory();
}


const std::vector<std::unique_ptr<L1MemoryAllocation>>& ResourceManager::get_l1_memory_allocations(
    const tt_cxy_pair& core_physical_location) const
{
    return get_core_resources(core_physical_location)->get_all_memory_allocations();
}

void ResourceManager::validate_rational_graph_resources(const RationalGraph* rational_graph) const
{
    // For now, we only check NCRISC resources usage.
    NcriscResourcesChecker ncrisc_resources_checker;
    ncrisc_resources_checker.check(rational_graph);
}

std::vector<const CoreResources*> ResourceManager::get_all_worker_core_resources() const
{
    std::vector<const CoreResources*> core_resources_vector;
    for (const auto& worker_it : m_worker_cores_resources)
    {
        core_resources_vector.push_back(worker_it.second.get());
    }
    
    return core_resources_vector;
}

} // namespace pipegen2
