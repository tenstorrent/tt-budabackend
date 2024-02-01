// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/property_map/function_property_map.hpp>
#include <map>
#include <unordered_map>

#include "common/buda_soc_descriptor.h"
#include "common/tt_cluster_graph.hpp"
#include "common/tt_core_resource_tracker.hpp"
#include "utils/logger.hpp"

namespace tt {

constexpr int EDGE_WEIGHT_INFINITY = std::numeric_limits<int>::max();

static bool implementable_on_ethernet_link(
    CoreResources const &producer_core_resources,
    CoreResources const &consumer_core_resources,
    bool producer_is_dram,
    bool consumer_is_dram,
    bool is_multicast) {
    // TODO: Update to directly query some router function so we don't have any form of duplicate implementation
    bool has_enough_producer_eth_streams = producer_core_resources.has_available_ethernet_stream_slots(1);
    bool has_enough_consumer_eth_streams = consumer_core_resources.has_available_ethernet_stream_slots(1);
    bool has_enough_producer_dram_reader_streams =
        producer_is_dram ? producer_core_resources.has_available_input_from_dram_slot() : true;
    bool has_enough_consumer_dram_writer_streams =
        consumer_is_dram ? consumer_core_resources.has_available_output_to_dram_slots(1) : true;
    // bool has_enough_consumer_multicast_streams =
    //     is_multicast ? consumer_core_resources.has_available_multicast_stream_slots() : true;

    bool link_resources_sufficient = has_enough_producer_eth_streams && has_enough_consumer_eth_streams &&
                                     has_enough_producer_dram_reader_streams && has_enough_consumer_dram_writer_streams;

    if (!link_resources_sufficient) {
        log_trace(tt::LogRouter, "Invalid edge because insufficient {}", std::string("") + std::string(!has_enough_producer_eth_streams ? " producer eth streams," : "") + std::string(!has_enough_consumer_eth_streams ? " consumer eth streams," : "") + std::string(!has_enough_producer_dram_reader_streams ? " dram reader streams," : "") + std::string(!has_enough_consumer_dram_writer_streams ? " dram writer streams," : ""));
    }

    return link_resources_sufficient;
}

static int compute_edge_weight_min_eth_stream_usage(
    tt_SocDescriptor const &soc_descriptor,
    ClusterResourceModel const *resource_model,
    ClusterGraph const &cluster_graph,
    chip_id_t producer_chip,
    chip_id_t consumer_chip,
    bool producer_is_dram,
    bool consumer_is_dram,
    bool is_mcast) {
    // Somewhat arbitrary cost function. 1 hops costs 8 (derived from # eth streams per core)
    // and add 1 to the cost for each used eth stream on the least used core
    // If no streams available, set the cost to max
    const auto &cd = cluster_graph.cluster_description();
    const auto &conected_channel_pairs =
        cd.get_directly_connected_ethernet_channels_between_chips(producer_chip, consumer_chip);
    auto min_eth_link_stream_use = EDGE_WEIGHT_INFINITY;
    log_trace(tt::LogRouter, "Computing edge weight between chip {} -> chip {}", producer_chip, consumer_chip);
    for (const auto &[producer_chan, consumer_chan] : conected_channel_pairs) {
        auto producer_core = soc_descriptor.ethernet_cores.at(producer_chan);
        auto consumer_core = soc_descriptor.ethernet_cores.at(consumer_chan);
        const auto &producer_core_resources = *resource_model->get_core_resources(tt_cxy_pair(producer_chip, producer_core));
        const auto &consumer_core_resources = *resource_model->get_core_resources(tt_cxy_pair(consumer_chip, consumer_core));

        bool link_resources_sufficient = implementable_on_ethernet_link(producer_core_resources, consumer_core_resources, producer_is_dram, consumer_is_dram, is_mcast);

        if (link_resources_sufficient) {
            auto channel_pair_usage = std::max(
                    // These should realistically be the same value for eth stream count in this case
                    producer_core_resources.get_used_ethernet_stream_count(),
                    consumer_core_resources.get_used_ethernet_stream_count()
                ) + producer_core_resources.get_max_num_ethernet_streams_total(); // Add the unit "cost" to traverse an unused link

            log_trace(tt::LogRouter, "\tweight between channels {} and {}: {}", producer_chan, consumer_chan, channel_pair_usage);
            min_eth_link_stream_use = std::min(min_eth_link_stream_use, channel_pair_usage);
        }
    }

    log_trace(tt::LogRouter, "Edge weight chip {} -> chip {}: {}", producer_chip, consumer_chip, min_eth_link_stream_use);

    return min_eth_link_stream_use;
}

std::vector<std::pair<chip_id_t, chip_id_t>> find_shortest_path_chip_to_chip_route_ignoring_bandwidth(
    tt_SocDescriptor const &soc_descriptor,
    ClusterResourceModel const *resource_model,
    ClusterGraph const &cluster_graph,
    chip_id_t producer_chip,
    chip_id_t consumer_chip,
    bool producer_is_dram,
    bool consumer_is_dram,
    bool is_multicast) {
    using edge_descriptor_t =
        typename decltype(cluster_graph.graph
                              .graph)::edge_descriptor;  // aka edge_t - need to clean this up before pushing
    using vertex_descriptor_t =
        typename decltype(cluster_graph.graph
                              .graph)::vertex_descriptor;  // aka vertex_t - need to clean this up before pushing
    auto compute_edge_weight_function = [&](edge_descriptor_t ed) -> int {
        chip_id_t producer_chip = cluster_graph.graph.edge_source_node(ed).chip_id;
        chip_id_t consumer_chip = cluster_graph.graph.edge_target_node(ed).chip_id;
        log_assert(producer_chip != consumer_chip, "Producer and consumer chip should not be identical");
        int edge_weight = compute_edge_weight_min_eth_stream_usage(
            soc_descriptor,
            resource_model,
            cluster_graph,
            producer_chip,
            consumer_chip,
            producer_is_dram,
            consumer_is_dram,
            is_multicast);
        log_trace(tt::LogAlways, "Edge weight between {} and {} is {}", producer_chip, consumer_chip, edge_weight);
        log_trace(tt::LogAlways, "Neighbours of {} in cluster graph:", producer_chip);
        for (const auto n : cluster_graph.get_neighbours(producer_chip)) {
            (void)n; // remove unused variable warning in release builds
            log_trace(tt::LogAlways, "\t{}", n);
        }
        return edge_weight;
    };

    auto edge_weights_map = boost::make_function_property_map<edge_descriptor_t, int>(compute_edge_weight_function);


    /* Compute the shortest path */
    auto start = cluster_graph.node_id_of_chip.at(producer_chip);
    auto predecessors = std::vector<vertex_descriptor_t>(cluster_graph.graph.num_vertices());
    auto distance_map = std::vector<int>(cluster_graph.graph.num_vertices());
    auto color_map = std::vector<boost::default_color_type>(cluster_graph.graph.num_vertices());
    boost::typed_identity_property_map<vertex_descriptor_t> vid;
    log_assert(EDGE_WEIGHT_INFINITY == std::numeric_limits<int>::max(), "Expected EDGE_WEIGHT_INFINITY to be {}", std::numeric_limits<int>::max());
    
    boost::dijkstra_shortest_paths(
        cluster_graph.graph.graph, 
        start,
        weight_map(edge_weights_map).                                                        // Boost specific way to allow the user to specify various traits of the boost graph
            distance_inf<int>(EDGE_WEIGHT_INFINITY).                                         // algorithm, rather than havign to specify values for every argument that is used by
            predecessor_map(boost::make_iterator_property_map(predecessors.begin(), vid)).   // it (there are a lot) - including the default values you don't care about
            distance_combine(boost::closed_plus<int>())                                      // need to set this one - otherwise default is to plus<T> which will overflow
    );
    
    /* Populate the output with the computed shortest path */
    std::unordered_set<vertex_descriptor_t> touched_vertices = {}; // For debug/hang detection
    std::vector<std::pair<chip_id_t,chip_id_t>> route = {};
    auto current_node = cluster_graph.node_id_of_chip.at(consumer_chip);
    while(current_node != start) {
        auto producer = predecessors[current_node]; 
        if (touched_vertices.find(current_node) != touched_vertices.end()) {
            log_error("Failed to find viable route from chip {} to {}. Out of ethernet resources somewhere between the two or no such connectivity exists", producer_chip, consumer_chip);
            return {};
        }
        touched_vertices.insert(current_node);
        route.insert(route.begin(), std::make_pair(cluster_graph.graph.get_vertex(producer).chip_id, cluster_graph.graph.get_vertex(current_node).chip_id));
        current_node = producer;
    }
    log_assert(route.size() > 0, "Expected positive route size");

    return route;
}

int find_shortest_path_chip_to_chip_hop_count(
    const ClusterGraph &cluster_graph, chip_id_t producer_chip, chip_id_t consumer_chip) {
    using edge_descriptor_t =
        typename decltype(cluster_graph.graph
                              .graph)::edge_descriptor;  // aka edge_t - need to clean this up before pushing
    using vertex_descriptor_t =
        typename decltype(cluster_graph.graph
                              .graph)::vertex_descriptor;  // aka vertex_t - need to clean this up before pushing
    auto compute_edge_weight_function = [&](edge_descriptor_t ed) -> int { return 1; };

    auto edge_weights_map = boost::make_function_property_map<edge_descriptor_t, int>(compute_edge_weight_function);

    /* Compute the shortest path */
    auto start = cluster_graph.node_id_of_chip.at(producer_chip);
    auto predecessors = std::vector<vertex_descriptor_t>(cluster_graph.graph.num_vertices());
    auto distance_map = std::vector<int>(cluster_graph.graph.num_vertices());
    auto color_map = std::vector<boost::default_color_type>(cluster_graph.graph.num_vertices());
    boost::typed_identity_property_map<vertex_descriptor_t> vid;
    log_assert(EDGE_WEIGHT_INFINITY == std::numeric_limits<int>::max(), "Expected EDGE_WEIGHT_INFINITY to be {}", std::numeric_limits<int>::max());

    boost::dijkstra_shortest_paths(
        cluster_graph.graph.graph,
        start,
        weight_map(edge_weights_map)
            .  // Boost specific way to allow the user to specify various traits of the boost graph
        distance_inf<int>(EDGE_WEIGHT_INFINITY)
            .  // algorithm, rather than havign to specify values for every argument that is used by
        predecessor_map(boost::make_iterator_property_map(predecessors.begin(), vid))
            .  // it (there are a lot) - including the default values you don't care about
        distance_combine(
            boost::closed_plus<int>())  // need to set this one - otherwise default is to plus<T> which will overflow
    );

    /* Populate the output with the computed shortest path */
    std::unordered_set<vertex_descriptor_t> touched_vertices = {};  // For debug/hang detection
    auto current_node = cluster_graph.node_id_of_chip.at(consumer_chip);
    int length = 0;
    while (current_node != start) {
        auto producer = predecessors[current_node];
        if (touched_vertices.find(current_node) != touched_vertices.end()) {
            log_error(
                "Failed to find viable route from chip {} to {}. Out of ethernet resources somewhere between the two "
                "or no such connectivity exists",
                producer_chip,
                consumer_chip);
            return {};
        }
        touched_vertices.insert(current_node);
        current_node = producer;
        length++;
    }

    return length;
}

};  // namespace tt