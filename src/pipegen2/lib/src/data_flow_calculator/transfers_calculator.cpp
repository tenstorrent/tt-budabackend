// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfers_calculator.h"

#include <map>
#include <unordered_set>
#include <vector>

#include "data_flow_calculator/transfers_calculator_internal.h"
#include "model/data_flow/data_flow_node.h"
#include "model/data_flow/subgraph_leaf_groups.h"

namespace pipegen2
{

void TransfersCalculator::calculate_phases(const SubgraphLeafGroups* subgraph_leaf_groups)
{
    for (const auto& [subgraph_id, subgraph_leaf_cluster] : subgraph_leaf_groups->get_subgraph_leaves())
    {
        unsigned int start_phase_offset = 0;
        for (const LeafGroup& leaf_group : subgraph_leaf_cluster.get_leaf_groups())
        {
            if (leaf_group.empty())
            {
                continue;
            }

            // All leaves in a single leaf group will have the exact same phases, so we only compute the phases
            // for the first leaf, and copy computed phases to other leaf nodes at the end.
            DataFlowNode* first_leaf_node = leaf_group.get_first_leaf_node();

            start_phase_offset += data_flow_internal::calculate_phases(
                first_leaf_node, 0 /* data_offset */, nullptr /* dest */, start_phase_offset);
            ;
        }
    }

    // Copying calculated phases per leaf group needs to be done at the end to ensure that all phases from the input
    // nodes to the first node in the leaf group are computed properly, to avoid having to copy and override these
    // phases in other leaf group nodes multiple times.
    copy_calculated_phases_per_leaf_group(subgraph_leaf_groups);
}

void TransfersCalculator::copy_calculated_phases_per_leaf_group(const SubgraphLeafGroups* subgraph_leaf_groups)
{
    for (const auto& [subgraph_id, subgraph_leaf_cluster] : subgraph_leaf_groups->get_subgraph_leaves())
    {
        // It is enough to only keep track of the first node in the leaf group, since a leaf node cannot appear in more
        // than 1 non-duplicated leaf groups.
        std::unordered_set<DataFlowNode*> seen_leaf_group_first_nodes;

        for (const LeafGroup& leaf_group : subgraph_leaf_cluster.get_leaf_groups())
        {
            if (leaf_group.empty() || seen_leaf_group_first_nodes.count(leaf_group.get_first_leaf_node()))
            {
                continue;
            }

            copy_calculated_phases_per_leaf_group(leaf_group);
            seen_leaf_group_first_nodes.insert(leaf_group.get_first_leaf_node());
        }
    }
}

void TransfersCalculator::copy_calculated_phases_per_leaf_group(const LeafGroup& leaf_group)
{
    const std::unordered_set<DataFlowNode*>& leaf_nodes = leaf_group.get_leaf_nodes();

    DataFlowNode* first_leaf_node = leaf_group.get_first_leaf_node();
    const std::vector<DataFlowNode*>& source_nodes = first_leaf_node->get_sources();

    for (auto leaf_node_it = std::next(leaf_nodes.begin()); leaf_node_it != leaf_nodes.end(); ++leaf_node_it)
    {
        std::map<unsigned int, std::vector<PhaseInfo>> receiving_phases_per_input_group =
            first_leaf_node->get_receiving_phases_per_input_group();

        DataFlowNode* leaf_node = *leaf_node_it;
        leaf_node->set_receiving_phases_per_input_group(std::move(receiving_phases_per_input_group));

        for (DataFlowNode* source_node : source_nodes)
        {
            std::vector<PhaseInfo> sending_phases = source_node->get_sending_phases(first_leaf_node);
            source_node->set_sending_phases_to_dest(leaf_node, std::move(sending_phases));
        }
    }
}

}  // namespace pipegen2
