// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/resource_manager.h"

#include "device/core_resources.h"
#include "device/ethernet_core_resources.h"
#include "device/l1/l1_buffer.h"
#include "device/ncrisc_resources_checker.h"
#include "device/resource_manager_internal.h"
#include "device/worker_core_resources.h"
#include "logger.hpp"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_location_utils.h"

namespace pipegen2 {

ResourceManager::ResourceManager(std::unique_ptr<SoCInfo> soc_info)
    : m_soc_info(std::move(soc_info)),
      // This is default allocation strategy. In future we can add more fine-grained strategies.
      m_thb_allocation_strategy(
          std::make_unique<ChipWideTHBAllocationStrategy>(m_worker_cores_resources, m_ethernet_cores_resources)) {
    for (ChipId chip_id : m_soc_info->get_chip_ids()) {
        for (const tt_cxy_pair& core_physical_location : m_soc_info->get_worker_cores_physical_locations(chip_id)) {
            const tt_cxy_pair core_logical_location =
                convert_physical_core_to_logical(core_physical_location, m_soc_info->get_soc_decriptors());
            m_worker_cores_resources.emplace(
                core_physical_location,
                resource_manager_internal::create_worker_core_resources(m_soc_info->get_device_arch(),
                                                                        core_physical_location, core_logical_location));
        }

        for (const tt_cxy_pair& core_physical_location : m_soc_info->get_ethernet_cores_physical_locations(chip_id)) {
            m_ethernet_cores_resources.emplace(core_physical_location,
                                               resource_manager_internal::create_ethernet_core_resources(
                                                   m_soc_info->get_device_arch(), core_physical_location));
        }
    }
}

ResourceManager::~ResourceManager() = default;

WorkerCoreResources* ResourceManager::get_worker_core_resources(const tt_cxy_pair& core_physical_location) const {
    auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
    log_assert(worker_core_resources_it != m_worker_cores_resources.end(),
               "ResourceManager: There is no worker core at a location: {}", core_physical_location.str());

    return worker_core_resources_it->second.get();
}

EthernetCoreResources* ResourceManager::get_ethernet_core_resources(const tt_cxy_pair& core_physical_location) const {
    auto eth_core_resources_it = m_ethernet_cores_resources.find(core_physical_location);
    log_assert(eth_core_resources_it != m_ethernet_cores_resources.end(),
               "ResourceManager: There is no ethernet core at a location: {}", core_physical_location.str());

    return eth_core_resources_it->second.get();
}

CoreResources* ResourceManager::get_core_resources(const tt_cxy_pair& core_physical_location) const {
    auto worker_core_resources_it = m_worker_cores_resources.find(core_physical_location);
    if (worker_core_resources_it == m_worker_cores_resources.end()) {
        auto eth_core_resources_it = m_ethernet_cores_resources.find(core_physical_location);
        log_assert(eth_core_resources_it != m_ethernet_cores_resources.end(),
                   "ResourceManager: There is no routing core at a location: {}", core_physical_location.str());

        return eth_core_resources_it->second.get();
    }

    return worker_core_resources_it->second.get();
}

StreamId ResourceManager::allocate_packer_stream(const tt_cxy_pair& core_physical_location, int operand_id) const {
    if (is_ethernet_core(core_physical_location)) {
        return get_ethernet_core_resources(core_physical_location)->allocate_general_purpose_stream();
    }

    return get_worker_core_resources(core_physical_location)->allocate_packer_stream(operand_id);
}

StreamId ResourceManager::allocate_unpacker_stream(const tt_cxy_pair& worker_core_physical_location,
                                                   int operand_id) const {
    return get_worker_core_resources(worker_core_physical_location)->allocate_unpacker_stream(operand_id);
}

StreamId ResourceManager::allocate_intermed_stream(const tt_cxy_pair& worker_core_physical_location,
                                                   int operand_id) const {
    return get_worker_core_resources(worker_core_physical_location)->allocate_intermed_stream(operand_id);
}

StreamId ResourceManager::allocate_gather_stream(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->allocate_gather_stream();
}

StreamId ResourceManager::allocate_multicast_stream(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->allocate_multicast_stream();
}

StreamId ResourceManager::allocate_packer_multicast_stream(const tt_cxy_pair& worker_core_physical_location,
                                                           int operand_id) const {
    return get_worker_core_resources(worker_core_physical_location)->allocate_packer_multicast_stream(operand_id);
}

StreamId ResourceManager::allocate_general_purpose_stream(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->allocate_general_purpose_stream();
}

StreamId ResourceManager::allocate_ethernet_stream(const tt_cxy_pair& eth_core_physical_location) const {
    return get_ethernet_core_resources(eth_core_physical_location)->allocate_ethernet_stream();
}

void ResourceManager::allocate_l1_tile_header_buffers(const THBAllocationInfo& thb_allocation_info) const {
    m_thb_allocation_strategy->allocate_l1_tile_header_buffers(thb_allocation_info);
}

const L1Buffer* ResourceManager::allocate_l1_stream_buffer(const StreamNode* stream_node,
                                                           const unsigned int buffer_size) const {
    return get_core_resources(stream_node->get_physical_location())
        ->allocate_l1_stream_buffer(stream_node, buffer_size);
}

const L1Buffer* ResourceManager::allocate_l1_ncrisc_fallback_buffer(const tt_cxy_pair& core_physical_location,
                                                                    const unsigned int buffer_size) const {
    return get_core_resources(core_physical_location)->allocate_l1_ncrisc_fallback_buffer(buffer_size);
}

const L1Buffer* ResourceManager::allocate_l1_extra_overlay_blob_space(const tt_cxy_pair& core_physical_location,
                                                                      const unsigned int total_blob_size) const {
    return get_core_resources(core_physical_location)
        ->allocate_l1_extra_overlay_blob_space(total_blob_size, is_ethernet_core(core_physical_location));
}

unsigned int ResourceManager::allocate_kernel_input(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->allocate_kernel_input();
}

unsigned int ResourceManager::allocate_kernel_output(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->allocate_kernel_output();
}

unsigned int ResourceManager::get_multicast_streams_count(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->get_multicast_streams_count();
}

void ResourceManager::check_if_out_of_l1_data_buffers_memory(const tt_cxy_pair& core_physical_location) const {
    return get_core_resources(core_physical_location)->check_if_out_of_l1_data_buffers_memory();
}

void ResourceManager::validate_rational_graph_resources(const RationalGraph* rational_graph) const {
    // For now, we only check NCRISC resources usage.
    NcriscResourcesChecker ncrisc_resources_checker;
    ncrisc_resources_checker.check(rational_graph);
}

void ResourceManager::set_core_resource_op_name(const tt_cxy_pair& core_location, const std::string op_name) const {
    get_core_resources(core_location)->set_op_name(op_name);
}

unsigned int ResourceManager::get_tile_header_buffer_address(const tt_cxy_pair& core_physical_location,
                                                             const unsigned int tile_size) const {
    return get_core_resources(core_physical_location)->get_tile_header_buffer_address(tile_size);
}

}  // namespace pipegen2
