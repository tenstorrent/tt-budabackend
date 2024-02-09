// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfer_limits_calculator.h"

#include <unordered_set>

#include "data_flow_calculator/transfer_limits_calculator_internal.h"

namespace pipegen2
{

TransferLimitsCalculator::TransferLimitsCalculator(unsigned int max_num_tiles_per_phase,
                                                   unsigned int max_num_phases_per_iteration) :
    m_max_num_tiles_per_phase(max_num_tiles_per_phase),
    m_max_num_phases_per_iteration(max_num_phases_per_iteration)
{
}

void TransferLimitsCalculator::calculate_transfer_limits(const std::vector<DataFlowNode*>& root_nodes,
                                                         const std::vector<DataFlowNode*>& leaf_nodes)
{
    find_single_source_paths(root_nodes);

    calculate_num_iterations(leaf_nodes, root_nodes);

    calculate_tiles_to_send(leaf_nodes);

    calculate_number_of_paths_through_node(leaf_nodes);

    calculate_subtree_common_divisor(root_nodes);

    calculate_max_tiles_per_phase(root_nodes);
}

void TransferLimitsCalculator::find_single_source_paths(const std::vector<DataFlowNode*> root_nodes)
{
    for (DataFlowNode* df_node : root_nodes)
    {
        data_flow_internal::find_single_source_paths(df_node, true /* is_upstream_single_source */);
    }
}

void TransferLimitsCalculator::calculate_num_iterations(const std::vector<DataFlowNode*>& leaf_nodes,
                                                        const std::vector<DataFlowNode*>& root_nodes)
{
    data_flow_internal::calculate_num_iterations(
        leaf_nodes, root_nodes, m_max_num_tiles_per_phase, m_max_num_phases_per_iteration);
}

void TransferLimitsCalculator::calculate_tiles_to_send(const std::vector<DataFlowNode*>& leaf_nodes)
{
    std::unordered_set<DataFlowNode*> visited_nodes;
    for (DataFlowNode* leaf_node : leaf_nodes)
    {
        data_flow_internal::calculate_tiles_to_send(leaf_node, visited_nodes);
    }
}

void TransferLimitsCalculator::calculate_number_of_paths_through_node(const std::vector<DataFlowNode*>& leaf_nodes)
{
    std::unordered_set<DataFlowNode*> visited_nodes;
    for (DataFlowNode* leaf_node : leaf_nodes)
    {
        data_flow_internal::calculate_number_of_paths_through_node(leaf_node, visited_nodes);
    }
}

void TransferLimitsCalculator::calculate_subtree_common_divisor(const std::vector<DataFlowNode*>& root_nodes)
{
    std::unordered_set<DataFlowNode*> visited_nodes;
    for (DataFlowNode* root_node : root_nodes)
    {
        data_flow_internal::calculate_subtree_common_divisor(root_node, visited_nodes);
    }
}

void TransferLimitsCalculator::calculate_max_tiles_per_phase(const std::vector<DataFlowNode*>& root_nodes)
{
    std::unordered_set<DataFlowNode*> visited_nodes;
    for (DataFlowNode* root_node : root_nodes)
    {
        data_flow_internal::calculate_max_tiles_per_phase(root_node, visited_nodes, m_max_num_tiles_per_phase);
    }
}

} // namespace pipegen2
