// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace pipegen2
{
    typedef std::uint64_t NodeId;

    class NodeVisitor;

    // Pipe graph base node types.
    enum class PBBaseNodeType
    {
        Source = 0,
        Destination,
        Isolated,
        SourceAndDestination
    };

    // Pipe graph base node input types.
    enum class PBBaseNodeInputType
    {
        NoInput = 0,
        SingleInput,
        MultipleInputs
    };

    // Pipe graph base node output types.
    enum class PBBaseNodeOutputType
    {
        NoOutput = 0,
        SingleOutput,
        MultipleOutputs
    };

    // Pipe graph base node class.
    class PGBaseNode
    {
    public:
        virtual ~PGBaseNode() = default;

        NodeId get_id() const { return m_id; }

        void set_id(NodeId id) { m_id = id; }

        const std::vector<PGBaseNode*>& get_input_nodes() const { return m_input_nodes; }

        const std::vector<PGBaseNode*>& get_output_nodes() const { return m_output_nodes; }

        void add_input_node(PGBaseNode* input_node) { m_input_nodes.push_back(input_node); }

        void add_output_node(PGBaseNode* output_node) { m_output_nodes.push_back(output_node); }

        bool has_no_inputs() const { return m_input_nodes.empty(); }

        bool has_single_input() const { return m_input_nodes.size() == 1; }

        bool has_no_outputs() const { return m_output_nodes.empty(); }

        bool has_single_output() const { return m_output_nodes.size() == 1; }

        virtual void accept_node_visitor(NodeVisitor&) const = 0;

        PBBaseNodeType get_node_type() const
        {
            if (has_no_inputs() && has_no_outputs())
            {
                return PBBaseNodeType::Isolated;
            }

            if (has_no_inputs())
            {
                return PBBaseNodeType::Source;
            }

            if (has_no_outputs())
            {
                return PBBaseNodeType::Destination;
            }

            return PBBaseNodeType::SourceAndDestination;
        }

        PBBaseNodeInputType get_node_input_type() const
        {
            if (has_no_inputs())
            {
                return PBBaseNodeInputType::NoInput;
            }

            if (has_single_input())
            {
                return PBBaseNodeInputType::SingleInput;
            }

            return PBBaseNodeInputType::MultipleInputs;
        }

        PBBaseNodeOutputType get_node_output_type() const
        {
            if (has_no_outputs())
            {
                return PBBaseNodeOutputType::NoOutput;
            }

            if (has_single_output())
            {
                return PBBaseNodeOutputType::SingleOutput;
            }

            return PBBaseNodeOutputType::MultipleOutputs;
        }

    protected:
        PGBaseNode() : m_id(-1) {}

    private:
        // Node id.
        NodeId m_id;

        // Nodes feeding this node.
        std::vector<PGBaseNode*> m_input_nodes;

        // Nodes this node is feeding.
        std::vector<PGBaseNode*> m_output_nodes;
    };
}