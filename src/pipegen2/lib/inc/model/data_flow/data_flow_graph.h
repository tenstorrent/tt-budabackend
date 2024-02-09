// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_set>

#include "data_flow_node.h"

namespace pipegen2
{
    // Represents a collection of data flow nodes which make up the connected data flow graph.
    class DataFlowGraph
    {
    public:
        // Adds a new node to the graph (and transfers ownership).
        void add_node(std::unique_ptr<DataFlowNode>&& node) { m_nodes.insert(std::move(node)); }

        // Returns a list of all the nodes in the graph.
        const std::unordered_set<std::unique_ptr<DataFlowNode>>& get_nodes() const { return m_nodes; }

        // Returns true if the graph contains only a single isolated node.
        bool is_single_node_graph() const { return m_nodes.size() == 1; }

    private:
        // List of data flow nodes which make up the graph.
        std::unordered_set<std::unique_ptr<DataFlowNode>> m_nodes;
    };
}