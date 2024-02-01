// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/tt_cluster_descriptor.h"
#include "common/base_types.hpp"
#include "common/tt_digraph_base.h"

#include <vector>
#include <tuple>

using ClusterDescription = tt_ClusterDescriptor;

struct cluster_graph_node_t {
  chip_id_t chip_id;
};

struct cluster_graph_edge_t {
  
};

namespace tt {
/* For now we assume a 1D topology to get things up and running. After that, we can go ahead and 
 * extend the class to be an actual graph. At that time, we can also open up a lower level/more 
 * direct access to the actual graph if desired */
class ClusterGraph {
  private:
    // avoiding const ref for now since lifetime of this outside of the scope of the cluster graph hasn't been nailed down yet
    const ClusterDescription _cluster_description; 


    void populate_graph_from_cluster_description(const ClusterDescription &cluster_description);

  public:
    tt_digraph_base<cluster_graph_node_t, cluster_graph_edge_t> graph;
    std::unordered_map<chip_id_t, size_t> node_id_of_chip;
    ClusterGraph(const ClusterDescription &cluster_description);

    const ClusterDescription &cluster_description() const { return _cluster_description; }

    std::unordered_set<chip_id_t> get_neighbours(chip_id_t chip_id) const;

};

}; // namespace tt