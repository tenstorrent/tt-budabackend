// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router/router_multichip_routing_algorithms.h"

#include "router.hpp"
#include "common/buda_soc_descriptor.h"
#include "common/tt_cluster_graph.hpp"
#include "common/multichip/multichip_routing.hpp"

using tt::ClusterGraph;

using router::pipe_t;
std::vector<std::pair<chip_id_t, chip_id_t>> find_shortest_path_chip_to_chip_route_ignoring_bandwidth(
    router::Router const& router, 
    const ClusterGraph &cluster_graph, 
    unique_id_t chip_to_chip_pipe_id) {


    using edge_descriptor_t = decltype(cluster_graph.graph.graph)::edge_descriptor; // aka edge_t - need to clean this up before pushing
    using vertex_descriptor_t = decltype(cluster_graph.graph.graph)::vertex_descriptor; // aka vertex_t - need to clean this up before pushing
    const auto &p = router.get_pipe(chip_to_chip_pipe_id);
    chip_id_t producer_chip = router.get_buffer(p.input_buffer_ids.at(0)).chip_location();
    chip_id_t consumer_chip = router.get_buffer(p.output_buffer_ids().at(0)).chip_location();

    bool producer_is_dram = router.is_queue_buffer(p.input_buffer_ids.at(0));    bool consumer_is_dram = router.is_queue_buffer(p.output_buffer_ids().at(0));
    bool is_multicast = p.output_buffer_ids().size() > 0;
    return tt::find_shortest_path_chip_to_chip_route_ignoring_bandwidth(
        router.get_soc_descriptor(producer_chip),
        &router.get_cluster_resource_model(),
        cluster_graph,
        producer_chip,
        consumer_chip,
        producer_is_dram,
        consumer_is_dram,
        is_multicast);
}


// Given a chip to chip unicast pipe ID, this function will compute the shortest path between the produce and consumer chips
// where the edge weights between all connected chips are 1. The graph connectivity is determined by the cluster graph.
// The function returns an integer which represents the number of hops between the producer and consumer chips.
// If the producer and consumer chips are not connected, the function returns -1.
// The function uses boost::dijkstra_shortest_paths to compute the shortest path.
int find_shortest_path_chip_to_chip_hop_count(
    router::Router const& router, 
    const ClusterGraph &cluster_graph, 
    unique_id_t chip_to_chip_pipe_id) {
    const auto &p = router.get_pipe(chip_to_chip_pipe_id);
    chip_id_t producer_chip = router.get_buffer(p.input_buffer_ids.at(0)).chip_location();
    chip_id_t consumer_chip = router.get_buffer(p.output_buffer_ids().at(0)).chip_location();

    return tt::find_shortest_path_chip_to_chip_hop_count(cluster_graph, producer_chip, consumer_chip);
}

