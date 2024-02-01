// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/tt_core_resource_tracker.hpp"
#include "common/base_types.hpp"

#include <vector>

class tt_SocDescriptor;

namespace tt {

class ClusterGraph;

std::vector<std::pair<chip_id_t, chip_id_t>> find_shortest_path_chip_to_chip_route_ignoring_bandwidth(
    tt_SocDescriptor const &soc_descriptor,
    ClusterResourceModel const *resource_model,
    const ClusterGraph &cluster_graph,
    chip_id_t producer_chip,
    chip_id_t consumer_chip,
    bool producer_is_dram,
    bool consumer_is_dram,
    bool is_multicast);

int find_shortest_path_chip_to_chip_hop_count(
    const ClusterGraph &cluster_graph, chip_id_t producer_chip, chip_id_t consumer_chip);

}; // namespace tt