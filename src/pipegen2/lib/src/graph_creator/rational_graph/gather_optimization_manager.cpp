// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/rational_graph/gather_optimization_manager.h"

#include "model/rational_graph/pipes/fork/dram_multicast_pipe.h"
#include "model/rational_graph/pipes/fork/multicast_pipe.h"
#include "model/rational_graph/pipes/fork/pcie_multicast_pipe.h"
#include "model/rational_graph/pipes/join/dormant_relay_pipe.h"
#include "model/rational_graph/pipes/join/gather_pipe.h"

namespace pipegen2
{
void GatherOptimizationManager::process_rational_graphs(
    const ResourceManager* resource_manager, std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
{
    count_multicast_streams(rational_graphs);
    process_gather_streams(resource_manager, rational_graphs);
}

void GatherOptimizationManager::count_multicast_streams(
    const std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
{
    for (const std::unique_ptr<RationalGraph>& rational_graph : rational_graphs)
    {
        for (const std::unique_ptr<RGBasePipe>& pipe : rational_graph->get_pipes())
        {
            if (dynamic_cast<const MulticastPipe*>(pipe.get()) == nullptr &&
                dynamic_cast<const DramMulticastPipe*>(pipe.get()) == nullptr &&
                dynamic_cast<const PCIeMulticastPipe*>(pipe.get()) == nullptr)
            {
                continue;
            }

            m_num_gather_mcast_streams_per_core[pipe->get_physical_location()]++;
        }
    }
}

void GatherOptimizationManager::process_gather_streams(
    const ResourceManager* resource_manager, std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
{
    for (std::unique_ptr<RationalGraph>& rational_graph : rational_graphs)
    {
        for (std::size_t i = 0; i < rational_graph->get_pipes().size(); ++i)
        {
            process_gather_streams(resource_manager, i, rational_graph);
        }
    }
}

void GatherOptimizationManager::process_gather_streams(
    const ResourceManager* resource_manager,
    const std::size_t pipe_index,
    std::unique_ptr<RationalGraph>& rational_graph)
{
    const RGBasePipe* pipe = rational_graph->get_pipes()[pipe_index].get();
    if (dynamic_cast<const GatherPipe*>(pipe) == nullptr)
    {
        return;
    }

    const tt_cxy_pair& pipe_location = pipe->get_physical_location();

    // If there is a Multicast pipe afterwards and they are on the same location, the same stream will be reused for
    // gather-multicast so we don't have to count it. We also don't have to check for union pipe in between because
    // when there is union we always create DormantRelay pipe instead of Gather.
    const RGBasePipe* multicast_pipe = pipe->get_output_node()->get_output_pipe();
    if (dynamic_cast<const MulticastPipe*>(multicast_pipe) != nullptr &&
        pipe_location == multicast_pipe->get_physical_location())
    {
        return;
    }

    auto it = m_num_gather_mcast_streams_per_core.find(pipe_location);
    if (it == m_num_gather_mcast_streams_per_core.end())
    {
        m_num_gather_mcast_streams_per_core.emplace(pipe_location, 1);
        return;
    }

    if (it->second < resource_manager->get_multicast_streams_count(pipe_location))
    {
        ++it->second;
    }
    else
    {
        // TODO: As a potential optimization we could choose which gather pipes to replace instead of replacing
        //       those which come after limit is exhausted.
        std::unique_ptr<RGBasePipe> dormant_relay_pipe =
            std::make_unique<DormantRelayPipe>(RGPipeProperties(pipe->get_pipe_properties()), pipe_location);
        rational_graph->replace_pipe(pipe_index, std::move(dormant_relay_pipe));
    }
}
}  // namespace pipegen2