// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/data_flow_calculator_internal.h"

#include <utility>

#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_node.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
namespace data_flow_internal
{

std::vector<DataFlowNode*> find_root_nodes(const DataFlowGraph* data_flow_graph)
{
    std::vector<DataFlowNode*> root_nodes;
    std::vector<DataFlowNode*> padding_root_nodes;
    for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_graph->get_nodes())
    {
        log_assert(!df_node->is_isolated_node(),
                       "Expecting non-isolated nodes in graph but found node {}", df_node->get_id());

        if (!df_node->is_root_node())
        {
            continue;
        }

        if (df_node->is_padding())
        {
            padding_root_nodes.push_back(df_node.get());
        }
        else
        {
            root_nodes.push_back(df_node.get());
        }
    }

    // Padding root nodes are added last to the root list because they don't contribute to the general transfer
    // limits on a root -> leaf path (since they are just static dram buffers which are initialized by the runtime), 
    // therefore they are processed at the very end, once transfer limits for all other nodes have been calculated.
    std::move(padding_root_nodes.begin(), padding_root_nodes.end(), std::back_inserter(root_nodes));

    return root_nodes;
}

std::vector<DataFlowNode*> find_leaf_nodes(const DataFlowGraph* data_flow_graph)
{
    std::vector<DataFlowNode*> leaf_nodes;
    for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_graph->get_nodes())
    {
        log_assert(!df_node->is_isolated_node(),
                       "Expecting non-isolated nodes in graph but found node {}", df_node->get_id());

        if (df_node->is_leaf_node())
        {
            leaf_nodes.push_back(df_node.get());
        }
    }

    return leaf_nodes;
}

} // namespace data_flow_internal
} // namespace pipegen2