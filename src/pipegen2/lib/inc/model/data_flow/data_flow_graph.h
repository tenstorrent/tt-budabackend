// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_set>

#include "data_flow_node.h"

namespace pipegen2
{
    class DataFlowGraph
    {
    public:
        void add_node(DataFlowNode* node) { m_nodes.insert(node); }

        const std::unordered_set<DataFlowNode*>& get_nodes() const { return m_nodes; }

    private:
        std::unordered_set<DataFlowNode*> m_nodes;
    };
}