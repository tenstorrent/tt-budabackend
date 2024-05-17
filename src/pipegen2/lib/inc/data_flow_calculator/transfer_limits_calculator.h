// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

namespace pipegen2
{

class DataFlowNode;

class TransferLimitsCalculator
{
public:
    // Constructor, intializes member variables.
    TransferLimitsCalculator(unsigned int max_num_tiles_per_phase, unsigned int max_num_phases_per_iteration);

    // Calculates transfer limits on a data flow graph defined by the lists of root and leaf nodes.
    void calculate_transfer_limits(
        const std::vector<DataFlowNode*>& root_nodes, const std::vector<DataFlowNode*>& leaf_nodes);

private:
    // Finds all single source paths starting from the given root nodes. A single source path is a path from root to
    // (multiple) leaf nodes, such that every node has at most one input at there are no scatter nodes on the path.
    void find_single_source_paths(const std::vector<DataFlowNode*> root_nodes);

    // Calculates num iterations field for all data flow nodes which can be reached from any root node.
    void calculate_num_iterations(
        const std::vector<DataFlowNode*>& leaf_nodes, const std::vector<DataFlowNode*>& root_nodes);

    // Calculates tiles to send for every data flow node whose sink is present in the list of leaf nodes.
    void calculate_tiles_to_send(const std::vector<DataFlowNode*>& leaf_nodes);

    // Calculates number of unique data flow paths going through every data flow node whose sink is present in the
    // list of leaf nodes.
    void calculate_number_of_paths_through_node(const std::vector<DataFlowNode*>& leaf_nodes);

    // Calculates common df node subtree divisor, starting from the given root nodes and then visiting all the nodes
    // which can be reached.
    void calculate_subtree_common_divisor(const std::vector<DataFlowNode*>& root_nodes);

    // Calculates max tiles per phase for every data flow node which can be reached from the given list of root
    // nodes.
    void calculate_max_tiles_per_phase(const std::vector<DataFlowNode*>& root_nodes);

private:
    // Maximum number of tiles which can be transfered in one phase.
    const unsigned int m_max_num_tiles_per_phase;

    // Maximum number of phases we can have in one iteration on direct path.
    const unsigned int m_max_num_phases_per_iteration;
};

}  // namespace pipegen2
