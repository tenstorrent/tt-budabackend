// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator_unit_test_utils.h"

using namespace pipegen2;

void connect_data_flow_graph(const std::vector<std::pair<DataFlowNode*, DataFlowNode*>>& edges)
{
    for (const auto& [source_df_node, dest_df_node] : edges)
    {
        source_df_node->add_dest(dest_df_node);
        dest_df_node->add_source(source_df_node);
    }
}

std::pair<DataFlowNode*, DataFlowNode*> make_df_edge(DataFlowNode& src_df_node, DataFlowNode& dest_df_node)
{
    return std::make_pair(&src_df_node, &dest_df_node);
}

std::pair<DataFlowNode*, DataFlowNode*> make_df_edge(
    std::unique_ptr<DataFlowNodeMock>& src_df_node, std::unique_ptr<DataFlowNodeMock>& dest_df_node)
{
    return make_df_edge(*src_df_node, *dest_df_node);
}