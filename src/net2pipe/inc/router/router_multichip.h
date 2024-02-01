// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "router/router_passes.h"
#include "router_types.h"

namespace router {

using ethernet_channel_t = int;
struct ethernet_link_t {
    std::array<ethernet_channel_t, 2> endpoint_channels;
};

bool is_chip_to_chip_pipe(const pipe_t &pipe, const router::Router &router);

std::vector<unique_id_t> collect_chip_to_chip_pipes(const router::Router &router);
std::unordered_set<std::pair<std::string,std::string>> get_chip_to_chip_pipe_ops(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids);
std::vector<std::pair<chip_id_t, chip_id_t>> generate_chip_to_chip_route_for_pipe(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t chip_to_chip_pipe_id);

}; // namespace router