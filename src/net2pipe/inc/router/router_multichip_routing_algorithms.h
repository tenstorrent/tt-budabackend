// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "router_types.h"

#include <vector>

namespace router {
    class Router;
}
namespace tt {
    class ClusterGraph;
}
using tt::ClusterGraph;

using router::unique_id_t;

std::vector<std::pair<chip_id_t, chip_id_t>> find_shortest_path_chip_to_chip_route_ignoring_bandwidth(
    router::Router const& router, const ClusterGraph &cluster_graph, unique_id_t chip_to_chip_pipe_id);

int find_shortest_path_chip_to_chip_hop_count(
    router::Router const& router, const ClusterGraph &cluster_graph, unique_id_t chip_to_chip_pipe_id);