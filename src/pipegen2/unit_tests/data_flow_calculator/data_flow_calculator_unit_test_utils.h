// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "model/data_flow/data_flow_node.h"

// Helper function which connects all source dest pairs in the given lists of data flow graph edges.
void connect_data_flow_graph(const std::vector<std::pair<pipegen2::DataFlowNode*, pipegen2::DataFlowNode*>>& edges);

// Helper function which creates a data flow graph edge pair from the given source and dest data flow nodes.
std::pair<pipegen2::DataFlowNode*, pipegen2::DataFlowNode*> make_df_edge(
    pipegen2::DataFlowNode& src_df_node, pipegen2::DataFlowNode& dest_df_node);

// Helper function which creates a data flow graph edge pair from the given source and dest mock data flow node unique
// pointers.
std::pair<pipegen2::DataFlowNode*, pipegen2::DataFlowNode*> make_df_edge(
    std::unique_ptr<pipegen2::DataFlowNodeMock>& src_df_node,
    std::unique_ptr<pipegen2::DataFlowNodeMock>& dest_df_node);