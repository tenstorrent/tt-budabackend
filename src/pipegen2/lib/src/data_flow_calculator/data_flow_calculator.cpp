// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/data_flow_calculator.h"

#include <memory>
#include <numeric>
#include <vector>
#include <unordered_set>

#include "data_flow_calculator/data_flow_calculator_internal.h"
#include "data_flow_calculator/data_flow_graph_creator.h"
#include "data_flow_calculator/subgraph_leaf_groups_finder.h"
#include "data_flow_calculator/transfer_limits_calculator.h"
#include "data_flow_calculator/transfers_calculator.h"
#include "model/data_flow/subgraph_leaf_groups.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    DataFlowCalculator::DataFlowCalculator(const RationalGraph* rational_graph, unsigned int max_num_tiles_per_phase) :
        m_rational_graph(rational_graph),
        m_max_num_tiles_per_phase(max_num_tiles_per_phase)
    {
    }

    std::vector<std::unique_ptr<DataFlowGraph>> DataFlowCalculator::create_data_flow_graphs()
    {
        DataFlowGraphCreator data_flow_graph_creator;
        return data_flow_graph_creator.create_data_flow_graphs(m_rational_graph);
    }

    std::unique_ptr<SubgraphLeafGroups> DataFlowCalculator::find_leaf_groups(
        const std::vector<DataFlowNode*>& root_nodes)
    {
        SubgraphLeafGroupsFinder subgraph_leaf_groups_finder;
        return subgraph_leaf_groups_finder.find_leaf_groups(root_nodes);
    }

    void DataFlowCalculator::calculate_transfers(const SubgraphLeafGroups* subgraph_leaf_groups)
    {
        TransfersCalculator transfers_calculator;
        transfers_calculator.calculate_phases(subgraph_leaf_groups);
    }

    void DataFlowCalculator::calculate_transfer_limits(const std::vector<DataFlowNode*> root_nodes,
                                                       const std::vector<DataFlowNode*> leaf_nodes)
    {
        TransferLimitsCalculator transfer_limits_calculator(m_max_num_tiles_per_phase, c_max_num_phases_per_iteration);
        transfer_limits_calculator.calculate_transfer_limits(root_nodes, leaf_nodes);
    }

    DataFlowInfo DataFlowCalculator::get_data_flow_info()
    {
        // All data flow calculations conducted on separate DataFlowGraph will be summed up in `result`.
        DataFlowInfo result;

        std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs = create_data_flow_graphs();

        for (std::unique_ptr<DataFlowGraph>& df_graph : connected_data_flow_graphs)
        {
            // Skipping single node (isolated) graphs.
            if (df_graph->get_nodes().size() == 1)
            {
                continue;
            }
            result += get_data_flow_info(df_graph.get());
        }

        return result;
    }

    DataFlowInfo DataFlowCalculator::get_data_flow_info(const DataFlowGraph* df_graph)
    {
        std::vector<DataFlowNode*> root_nodes = data_flow_internal::find_root_nodes(df_graph);
        log_assert(!root_nodes.empty(), "Expecting to find at least one root node in a subgraph");

        std::vector<DataFlowNode*> leaf_nodes = data_flow_internal::find_leaf_nodes(df_graph);
        log_assert(!leaf_nodes.empty(), "Expecting to find at least one leaf node in a subgraph");

        calculate_transfer_limits(root_nodes, leaf_nodes);

        std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups = find_leaf_groups(root_nodes);

        calculate_transfers(subgraph_leaf_groups.get());

        return create_data_flow_info(df_graph);
    }

    DataFlowInfo DataFlowCalculator::create_data_flow_info(const DataFlowGraph* data_flow_graph)
    {
        DataFlowInfo result;
        for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_graph->get_nodes())
        {
            for (DataFlowNode* dest : df_node->get_destinations())
            {
                result.set_edge_phases(df_node->get_rg_node(), dest->get_rg_pipe(), df_node->get_sending_phases(dest));
            }
            if (!df_node->is_root_node())
            {
                result.set_edge_phases(df_node->get_rg_pipe(),
                                       df_node->get_rg_node(),
                                       df_node->get_receiving_phases_for_all_input_groups());
                result.set_pipe_num_iterations(df_node->get_rg_pipe(), df_node->get_num_iterations());
                result.set_pipe_subtree_divisor(df_node->get_rg_pipe(), df_node->get_subtree_common_divisor());
            }
            result.set_node_max_num_tiles_per_phase(df_node->get_rg_node(), df_node->get_max_num_tiles_per_phase());
            result.set_node_msgs_to_send(df_node->get_rg_node(), df_node->get_tiles_to_send());
        }

        return result;
    }
}
