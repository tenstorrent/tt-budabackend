// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/resource_manager.h"
#include "model/rational_graph/rational_graph.h"
// clang-format on

namespace pipegen2
{
class GatherOptimizationManager
{
public:
    // Counts number of gather/multicast streams each pipe in given rational graphs will allocate, and in case when
    // too many are allocated on some core replaces excess Gather pipes with DormantRelay pipes.
    void process_rational_graphs(
        const ResourceManager* resource_manager, std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

private:
    // Counts allocations for multicast streams on each core.
    void count_multicast_streams(const std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

    // Counts number of gather streams each pipe in given rational graphs will allocate, and in case when too many
    // are allocated on some core replaces excess Gather pipes with DormantRelay pipes.
    void process_gather_streams(
        const ResourceManager* resource_manager, std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

    // Checks if Gather pipe will allocate an additional gather stream on core, and if that allocation crosses the
    // gather/multicast streams limit, in which case the pipe is replaced by DormantRelay pipe.
    void process_gather_streams(
        const ResourceManager* resource_manager,
        const std::size_t pipe_index,
        std::unique_ptr<RationalGraph>& rational_graph);

    // Holds number of gather/multicast streams allocated per core.
    std::unordered_map<tt_cxy_pair, unsigned int> m_num_gather_mcast_streams_per_core;
};
}  // namespace pipegen2