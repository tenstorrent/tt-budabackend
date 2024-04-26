// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/tt_cluster_graph.hpp"

#include "utils/logger.hpp"

#include <set>

namespace tt {
void ClusterGraph::populate_graph_from_cluster_description(const ClusterDescription &cluster_description) {
    log_debug(tt::LogRouter, "Cluster Graph");
    for (chip_id_t chip_id : cluster_description.get_all_chips()) {
        auto node_id = this->graph.add_vertex();
        this->graph.get_vertex(node_id).chip_id = chip_id;

        node_id_of_chip[chip_id] = node_id;
    }

    for (auto node_id : boost::make_iterator_range(boost::vertices(this->graph.graph))) {//this->graph.vertices_iterator_range()) {
        auto chip = this->graph.get_vertex(node_id).chip_id;

        // Support for clusters without eth. For clusters with eth, all chips should have eth.
        const auto &ethernet_connections = cluster_description.get_ethernet_connections();
        if (ethernet_connections.size() > 0){
            const auto &eth_connections = ethernet_connections.at(chip);
            std::set<chip_id_t> neighbours = {};
            for (const auto &[chan, neighbour_chip_and_chan] : eth_connections) {
                log_debug(tt::LogRouter, "chip {} chan {} -> chip {} chan{}", chip, chan, std::get<0>(neighbour_chip_and_chan), std::get<1>(neighbour_chip_and_chan));
                neighbours.insert(std::get<0>(neighbour_chip_and_chan));
            }

            for (chip_id_t id : neighbours) {
                this->graph.add_edge(node_id, node_id_of_chip.at(id));
            }
        }
    }
}

ClusterGraph::ClusterGraph(const ClusterDescription &cluster_description) :
    _cluster_description(cluster_description) 
{
    populate_graph_from_cluster_description(cluster_description);
}


std::unordered_set<chip_id_t> ClusterGraph::get_neighbours(chip_id_t chip_id) const {
    auto node_id = node_id_of_chip.at(chip_id);
    const auto &neighbour_vertices = boost::make_iterator_range(boost::adjacent_vertices(node_id, this->graph.graph));

    std::unordered_set<chip_id_t> neighbour_chips = {};   

    for (auto id : neighbour_vertices) {
        neighbour_chips.insert(this->graph.get_vertex(id).chip_id);
    }

    return neighbour_chips;
}

};