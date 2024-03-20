// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model/rational_graph/rational_graph.h"
#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_info.h"
#include "model/data_flow/data_flow_node.h"

namespace pipegen2
{

class RGBaseNode;
class RGBasePipe;
class SubgraphLeafGroups;

class DataFlowCalculator
{
public:
    // Constructor.
    DataFlowCalculator(const RationalGraph* rational_graph, unsigned int max_num_tiles_per_phase);

    // Returns data flow information for the given rational graph.
    DataFlowInfo get_data_flow_info();

protected:
    // Creates connected data flow graphs from a given rational graph.
    std::vector<std::unique_ptr<DataFlowGraph>> create_data_flow_graphs();

    // Finds all leaf groups per subgraphs starting from the given set of root nodes.
    std::unique_ptr<SubgraphLeafGroups> find_leaf_groups(const std::vector<DataFlowNode*>& root_nodes);

    // Calculates phases for all data flow nodes, starting calculation from the given leaf groups.
    void calculate_transfers(const SubgraphLeafGroups* subgraph_leaf_groups);

    // Calculates transfer limits for a given data flow graph (determined by the list of root and leaf nodes).
    void calculate_transfer_limits(const std::vector<DataFlowNode*> root_nodes,
                                   const std::vector<DataFlowNode*> leaf_nodes);

private:
    // Returns data flow information for the given data flow graph.
    DataFlowInfo get_data_flow_info(const DataFlowGraph* df_graph);

    // Creates a data flow info object by extracting required information from all nodes in the data flow graph.
    DataFlowInfo create_data_flow_info(const DataFlowGraph* data_flow_graph);

private:
    // Maximum number of phases we can have in one iteration on single source path.
    static constexpr unsigned int c_max_num_phases_per_iteration = 15;

    // Rational graph instance for which to calculate data flow info.
    const RationalGraph *const m_rational_graph;

    // Maximum number of tiles we can transfer in one phase.
    const unsigned int m_max_num_tiles_per_phase;
};

} // namespace pipegen2