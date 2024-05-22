// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/resources_allocators/tile_header_buffers_allocator.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "logger.hpp"

namespace pipegen2
{

void MulticastGroupFinder::unite_cores(const tt_cxy_pair& core1, const tt_cxy_pair& core2)
{
    tt_cxy_pair core1_parent_core = find_parent_core(core1);
    tt_cxy_pair core2_parent_core = find_parent_core(core2);

    if (core1_parent_core != core2_parent_core)
    {
        m_core_to_parent_core[core1_parent_core] = core2_parent_core;
    }
}

std::vector<std::unordered_set<tt_cxy_pair>> MulticastGroupFinder::find_grouped_cores()
{
    std::unordered_map<tt_cxy_pair, std::unordered_set<tt_cxy_pair>> grouped_cores_map;
    for (const auto& it : m_core_to_parent_core)
    {
        grouped_cores_map[find_parent_core(it.first)].insert(it.first);
    }

    std::vector<std::unordered_set<tt_cxy_pair>> grouped_cores;
    for (const auto& it : grouped_cores_map)
    {
        grouped_cores.emplace_back(it.second);
    }

    return grouped_cores;
}

tt_cxy_pair MulticastGroupFinder::find_parent_core(const tt_cxy_pair& core)
{
    auto core_to_parent_core_it = m_core_to_parent_core.find(core);
    if (core_to_parent_core_it == m_core_to_parent_core.end())
    {
        m_core_to_parent_core[core] = core;
        return core;
    }

    if (core_to_parent_core_it->second == core)
    {
        return core;
    }

    tt_cxy_pair parent_core = find_parent_core(core_to_parent_core_it->second);
    m_core_to_parent_core[core] = parent_core;

    return parent_core;
}

TileHeaderBuffersAllocator::TileHeaderBuffersAllocator(const ResourceManager* resource_manager) :
    m_resource_manager(resource_manager)
{
}

void TileHeaderBuffersAllocator::allocate_tile_header_buffers(
    const StreamGraphCollection* stream_graph_collection) const
{
    const std::vector<StreamNode*>& all_streams = stream_graph_collection->get_streams();

    allocate_extra_tile_header_buffers(all_streams);
    assign_tile_header_buffer_address(all_streams);
}

void TileHeaderBuffersAllocator::allocate_extra_tile_header_buffers(const std::vector<StreamNode*>& all_streams) const
{
    std::unordered_map<tt_cxy_pair, std::set<unsigned int>> core_to_tile_sizes;
    for (StreamNode* stream_node : all_streams)
    {
        core_to_tile_sizes[stream_node->get_physical_location()].insert(
            stream_node->get_base_config().get_msg_size().value());
    }

    std::vector<std::unordered_set<tt_cxy_pair>> grouped_cores = find_multicast_groups(all_streams);

    m_resource_manager->allocate_l1_tile_header_buffers(THBAllocationInfo{core_to_tile_sizes, grouped_cores});
}

std::vector<std::unordered_set<tt_cxy_pair>> TileHeaderBuffersAllocator::find_multicast_groups(
    const std::vector<StreamNode*>& all_streams) const
{
    MulticastGroupFinder multicast_group_finder;

    for (StreamNode* stream_node : all_streams)
    {
        if (!stream_node->is_multicast_stream())
        {
            continue;
        }

        const PhaseConfig& phase_config = stream_node->get_phase_configs().front();
        log_assert(
            phase_config.config.get_dest().has_value(),
            "Expecting multicast stream to have dest streams configured in all the phase configs");

        const std::vector<StreamNode*>& multicast_destinations = phase_config.config.get_dest().value();
        for (std::size_t i = 1; i < multicast_destinations.size(); ++i)
        {
            multicast_group_finder.unite_cores(
                multicast_destinations[i - 1]->get_physical_location(),
                multicast_destinations[i]->get_physical_location());
        }
    }

    return multicast_group_finder.find_grouped_cores();
}

void TileHeaderBuffersAllocator::assign_tile_header_buffer_address(const std::vector<StreamNode*>& all_streams) const
{
    for (StreamNode* stream_node : all_streams)
    {
        stream_node->get_base_config().set_msg_info_buf_addr(m_resource_manager->get_tile_header_buffer_address(
            stream_node->get_physical_location(), stream_node->get_base_config().get_msg_size().value()));
    }
}

}  // namespace pipegen2