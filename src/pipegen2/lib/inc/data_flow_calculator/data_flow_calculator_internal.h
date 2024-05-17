// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

namespace pipegen2
{

class DataFlowGraph;
class DataFlowNode;

namespace data_flow_internal
{

// Finds all root nodes in a given data flow graph.
std::vector<DataFlowNode*> find_root_nodes(const DataFlowGraph* data_flow_graph);

// Finds all leaf nodes in a given data flow graph.
std::vector<DataFlowNode*> find_leaf_nodes(const DataFlowGraph* data_flow_graph);

}  // namespace data_flow_internal
}  // namespace pipegen2