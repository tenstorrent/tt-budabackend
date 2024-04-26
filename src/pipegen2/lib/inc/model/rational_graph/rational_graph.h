// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nodes/base_input_node.h"
#include "nodes/base_output_node.h"
#include "nodes/base_rg_node.h"
#include "pipes/base_rg_pipe.h"

namespace pipegen2
{
    class RationalGraph
    {
    public:
        RationalGraph(std::vector<std::unique_ptr<RGBaseNode>>&& nodes,
                      std::vector<std::unique_ptr<RGBasePipe>>&& pipes,
                      const bool is_doing_pcie_transfer);

        const std::vector<std::unique_ptr<RGBaseNode>>& get_nodes() const { return m_nodes; }

        const std::vector<std::unique_ptr<RGBasePipe>>& get_pipes() const { return m_pipes; }

        // Gets all source nodes in this graph.
        // Expensive, creates vector for each call.
        std::vector<const BaseInputNode*> get_source_nodes() const;

        // Gets all leaf nodes in this graph.
        // Expensive, creates vector for each call.
        std::vector<const BaseOutputNode*> get_leaf_nodes() const;

        // Goes through all graph pipes and checks if graph is direct, i.e. every pipe has at most one input node.
        bool is_direct_graph() const;

        // Returns true if graph is doing transfer through PCIe.
        bool is_doing_pcie_transfer() const { return m_is_doing_pcie_transfer; }

        // Returns vector of all pipes in topological order.
        std::vector<const RGBasePipe*> get_pipes_in_topological_order() const;

        // Replaces pipe on a given index with a given new pipe.
        void replace_pipe(std::size_t pipe_index, std::unique_ptr<RGBasePipe> new_pipe);

        // Returns root pipe of a given pipes chain that ends with a given node.
        static const RGBasePipe* get_chain_root_pipe(const RGBaseNode* rg_node);

#ifdef TT_DEBUG
        // Forms JSON string used for debug visualizer to visualize rational graph.
        std::string to_json() const;
#endif

    private:
        // Recursive depth-first search for topological order.
        void depth_first_search(const RGBasePipe* node,
                                std::unordered_set<const RGBasePipe*>& visited_nodes,
                                std::vector<const RGBasePipe*>& topological_order) const;

        // Nodes in this graph.
        std::vector<std::unique_ptr<RGBaseNode>> m_nodes;

        // Edges (pipes) in this graph.
        std::vector<std::unique_ptr<RGBasePipe>> m_pipes;

        // True if graph is doing transfer through PCIe.
        bool m_is_doing_pcie_transfer;
    };
}