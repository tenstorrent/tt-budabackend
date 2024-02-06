// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pipegen2
{
    struct PhaseInfo
    {
        uint32_t phase_offset;
        uint32_t data_offset;
        uint32_t num_msgs;

        PhaseInfo(uint32_t phase_offset, uint32_t data_offset, uint32_t num_msgs)
            : phase_offset(phase_offset), data_offset(data_offset), num_msgs(num_msgs)
        {
        }

        PhaseInfo(uint32_t phase_offset, uint32_t num_msgs)
            : phase_offset(phase_offset), data_offset(0), num_msgs(num_msgs)
        {
        }
    };

    template<typename Source, typename Destination>
    class DataFlowEdges
    {
    public:
        const std::vector<PhaseInfo>& get_edge_phases(const Source* source, const Destination* dest) const
        {
            const auto iter = m_phases_on_edge.find({source, dest});
            // TODO assert iter != m_phases_on_edge.end()
            return iter->second;
        }

        void set_edge_phases(const Source* source, const Destination* dest, const std::vector<PhaseInfo>& phases)
        {
            m_phases_on_edge.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(source, dest),
                                     std::forward_as_tuple(phases));
        }

        DataFlowEdges& operator+=(const DataFlowEdges& other)
        {
            for (const auto& pair : other.m_phases_on_edge)
            {
                m_phases_on_edge.insert(pair);
            }

            return *this;
        }

    private:
        std::map<std::pair<const Source*, const Destination*>, std::vector<PhaseInfo>> m_phases_on_edge;
    };

    // Forward declarations.
    class RGBaseNode;
    class RGBasePipe;

    class DataFlowInfo
    {
    public:
        const std::vector<PhaseInfo>& get_edge_phases(const RGBaseNode* source, const RGBasePipe* dest) const
        {
            return m_node_2_pipe_edges.get_edge_phases(source, dest);
        }

        const std::vector<PhaseInfo>& get_edge_phases(const RGBasePipe* source, const RGBaseNode* dest) const
        {
            return m_pipe_2_node_edges.get_edge_phases(source, dest);
        }

        uint32_t get_max_num_tiles_per_phase(const RGBaseNode* node) const
        {
            return m_node_2_max_num_tiles_per_phase.at(node);
        }

        uint32_t get_tiles_to_send(const RGBaseNode* node) const
        {
            return m_node_2_msgs_to_send.at(node);
        }

        uint32_t get_num_iterations_in_epoch(const RGBasePipe* pipe) const
        {
            return m_pipe_2_num_iterations_in_epoch.at(pipe);
        }

        // Returns the greatest common divisor of all consume granularities in the subtree starting rooted at the given
        // pipe.
        const uint32_t get_subtree_divisor(const RGBasePipe* pipe) const
        {
            return m_pipe_2_subtree_divisor.at(pipe);
        }

        DataFlowInfo& operator+=(const DataFlowInfo& other)
        {
            m_node_2_pipe_edges += other.m_node_2_pipe_edges;
            m_pipe_2_node_edges += other.m_pipe_2_node_edges;

            for (const auto& pair : other.m_node_2_max_num_tiles_per_phase)
            {
                m_node_2_max_num_tiles_per_phase.insert(pair);
            }

            for (const auto& pair : other.m_node_2_msgs_to_send)
            {
                m_node_2_msgs_to_send.insert(pair);
            }

            for (const auto& pair : other.m_pipe_2_num_iterations_in_epoch)
            {
                m_pipe_2_num_iterations_in_epoch.insert(pair);
            }

            for (const auto& pair : other.m_pipe_2_subtree_divisor)
            {
                m_pipe_2_subtree_divisor.insert(pair);
            }

            return *this;
        }

    private:
        friend class DataFlowCalculator;

        void set_edge_phases(const RGBaseNode* source, const RGBasePipe* dest, const std::vector<PhaseInfo>& phases)
        {
            m_node_2_pipe_edges.set_edge_phases(source, dest, phases);
        }

        void set_edge_phases(const RGBasePipe* source, const RGBaseNode* dest, const std::vector<PhaseInfo>& phases)
        {
            m_pipe_2_node_edges.set_edge_phases(source, dest, phases);
        }

        void set_node_max_num_tiles_per_phase(const RGBaseNode* node, const uint32_t value)
        {
            m_node_2_max_num_tiles_per_phase[node] = value;
        }

        void set_node_msgs_to_send(const RGBaseNode* node, const uint32_t value)
        {
            m_node_2_msgs_to_send[node] = value;
        }

        void set_pipe_num_iterations(const RGBasePipe* pipe, const uint32_t value)
        {
            m_pipe_2_num_iterations_in_epoch[pipe] = value;
        }

        void set_pipe_subtree_divisor(const RGBasePipe* pipe, const uint32_t value)
        {
            m_pipe_2_subtree_divisor[pipe] = value;
        }

        DataFlowEdges<RGBaseNode, RGBasePipe> m_node_2_pipe_edges;
        DataFlowEdges<RGBasePipe, RGBaseNode> m_pipe_2_node_edges;
        std::unordered_map<const RGBaseNode*, uint32_t> m_node_2_max_num_tiles_per_phase;
        std::unordered_map<const RGBaseNode*, uint32_t> m_node_2_msgs_to_send;
        std::unordered_map<const RGBasePipe*, uint32_t> m_pipe_2_num_iterations_in_epoch;

        // The greatest common divisor of all consume granularities in the subtree starting rooted at the given pipe.
        std::unordered_map<const RGBasePipe*, uint32_t> m_pipe_2_subtree_divisor;
    };
}