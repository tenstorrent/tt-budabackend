// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/rational_graph.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "utils/logger.hpp"

namespace pipegen2
{
    RationalGraph::RationalGraph(std::vector<std::unique_ptr<RGBaseNode>>&& nodes,
                                 std::vector<std::unique_ptr<RGBasePipe>>&& pipes,
                                 const bool is_doing_pcie_transfer) :
        m_nodes(std::move(nodes)),
        m_pipes(std::move(pipes)),
        m_is_doing_pcie_transfer(is_doing_pcie_transfer)
    {
    }

    std::vector<const BaseInputNode*> RationalGraph::get_source_nodes() const
    {
        std::vector<const BaseInputNode*> source_nodes;

        for (const std::unique_ptr<RGBaseNode>& node : m_nodes)
        {
            const BaseInputNode* input_node = dynamic_cast<const BaseInputNode*>(node.get());
            if (input_node != nullptr && input_node->get_input_pipe() == nullptr)
            {
                source_nodes.push_back(input_node);
            }
        }

        return source_nodes;
    }

    std::vector<const BaseOutputNode*> RationalGraph::get_leaf_nodes() const
    {
        std::vector<const BaseOutputNode*> leaf_nodes;

        for (const std::unique_ptr<RGBaseNode>& node : m_nodes)
        {
            const BaseOutputNode* output_node = dynamic_cast<const BaseOutputNode*>(node.get());
            if (output_node != nullptr && output_node->get_output_pipes().empty())
            {
                leaf_nodes.push_back(output_node);
            }
        }

        return leaf_nodes;
    }

    bool RationalGraph::is_direct_graph() const
    {
        for (const std::unique_ptr<RGBasePipe>& pipe : m_pipes)
        {
            if (pipe->get_inputs().size() > 1)
            {
                return false;
            }
        }

        return true;
    }

    std::vector<const RGBasePipe*> RationalGraph::get_pipes_in_topological_order() const
    {
        std::vector<const RGBasePipe*> topological_order;

        std::unordered_set<const RGBasePipe*> visited_pipes;
        for (const std::unique_ptr<RGBasePipe>& pipe : m_pipes)
        {
            if (visited_pipes.find(pipe.get()) == visited_pipes.end())
            {
                depth_first_search(pipe.get(), visited_pipes, topological_order);
            }
        }

        std::reverse(topological_order.begin(), topological_order.end());

        return topological_order;
    }

    void RationalGraph::depth_first_search(const RGBasePipe* pipe,
                                           std::unordered_set<const RGBasePipe*>& visited_pipes,
                                           std::vector<const RGBasePipe*>& topological_order) const
    {
        visited_pipes.insert(pipe);

        // TODO: consider rewriting DFS in non-recursive way.
        for (const RGBaseNode* output_node : pipe->get_output_nodes())
        {
            for (const RGBasePipe* output_pipe : output_node->get_output_pipes())
            {
                if (visited_pipes.find(output_pipe) == visited_pipes.end())
                {
                    depth_first_search(output_pipe, visited_pipes, topological_order);
                }
            }
        }

        topological_order.push_back(pipe);
    }

    void RationalGraph::replace_pipe(std::size_t pipe_index, std::unique_ptr<RGBasePipe> new_pipe)
    {
        log_assert(pipe_index < m_pipes.size(), "Trying to replace pipe with invalid index");

        RGBasePipe* old_pipe = m_pipes[pipe_index].get();

        // Disconnect old pipe from all nodes.
        for (std::unique_ptr<RGBaseNode>& node : m_nodes)
        {
            if (node->get_input_pipe() == old_pipe)
            {
                node->set_input_pipe(new_pipe.get());
            }
            else
            {
                node->replace_output_pipe(old_pipe, new_pipe.get());
            }
        }

        // Connect new pipe to the old pipe's inputs.
        for (const PipeInput& pipe_input : old_pipe->get_inputs())
        {
            new_pipe->add_input(pipe_input);
        }

        // Connect new pipe to the old pipe's outputs.
        for (const RGBaseNode* output_node : old_pipe->get_output_nodes())
        {
            new_pipe->add_output_node(output_node);
        }

        m_pipes[pipe_index] = std::move(new_pipe);
    }

    const RGBasePipe* RationalGraph::get_chain_root_pipe(const RGBaseNode* rg_node)
    {
        RGBasePipe* root_pipe = nullptr;
        while (rg_node->has_input_pipe())
        {
            root_pipe = rg_node->get_input_pipe();
            rg_node = root_pipe->get_first_input_node();
        }

        return root_pipe;
    }

#ifdef TT_DEBUG
    std::string RationalGraph::to_json() const
    {
        std::stringstream json_builder;

        json_builder << "{ \"kind\": { \"graph\": true }, ";

        // Have to do this because some RG nodes do not have unique IDs (virtual nodes for example).
        std::unordered_map<const RGBaseNode*, std::size_t> node_to_id;

        json_builder << "\"nodes\": [";
        for (std::size_t i = 0; i < m_nodes.size(); ++i)
        {
            std::string node_type = node_type_to_string(m_nodes[i]->get_node_type());
            tt_cxy_pair node_location = m_nodes[i]->get_physical_location();
            std::string location_str = node_location.x == (std::size_t)(-1) ?
                                       "(chip=" + std::to_string(node_location.chip) + ", no worker core)" :
                                       node_location.str();
            std::string node_color = node_type_to_color(m_nodes[i]->get_node_type());
            json_builder << " { \"id\": \"" << i
                         << "\", \"label\": \"" << m_nodes[i]->get_id() << "\\n" << node_type << "\\n" << location_str
                         << "\", \"color\": \"" << node_color
                         << "\", \"shape\": \"ellipse\" },";
            node_to_id[m_nodes[i].get()] = i;
        }
        for (std::size_t i = 0; i < m_pipes.size(); ++i)
        {
            std::size_t pipe_graph_id = m_nodes.size() + i;
            std::string pipe_type = pipe_type_to_string(m_pipes[i]->get_pipe_type());
            std::string pipe_location = m_pipes[i]->get_physical_location().str();
            json_builder << " { \"id\": \"" << pipe_graph_id
                         << "\", \"label\": \"" << m_pipes[i]->get_id() << "\\n" << pipe_type << "\\n" << pipe_location
                         << "\", \"color\": \"#00BFFF\", \"shape\": \"box\" },";
        }
        // Erasing last ',' because that makes improper json.
        json_builder.seekp(-1, json_builder.cur);
        json_builder << " ], ";

        json_builder << "\"edges\": [";
        for (std::size_t i = 0; i < m_pipes.size(); ++i)
        {
            std::size_t pipe_graph_id = m_nodes.size() + i;
            for (const RGBaseNode* input_node : m_pipes[i]->get_unique_input_nodes())
            {
                // TODO: If input is scattered add label like: "scattered (N inputs)"
                json_builder << " { \"from\": \"" << node_to_id[input_node]
                             << "\", \"to\": \"" << pipe_graph_id << "\" },";
            }
            for (std::size_t j = 0; j < m_pipes[i]->get_output_nodes().size(); ++j)
            {
                json_builder << " { \"from\": \"" << pipe_graph_id
                             << "\", \"to\": \"" << node_to_id[m_pipes[i]->get_output_nodes()[j]] << "\" },";
            }
        }
        // Erasing last ',' because that makes improper json.
        json_builder.seekp(-1, json_builder.cur);
        json_builder << " ] }";

        // TODO: Make dotted edges for soft connections in RG (embeddings, intermediates).

        return json_builder.str();
    }
#endif
}