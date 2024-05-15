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

// Forward declarations.
class RGBaseNode;
class RGBasePipe;

// Struct modeling data flow information of a single phase.
struct PhaseInfo
{
    unsigned int phase_offset;
    unsigned int data_offset;
    unsigned int num_msgs;

    PhaseInfo(unsigned int phase_offset, unsigned int data_offset, unsigned int num_msgs)
        : phase_offset(phase_offset), data_offset(data_offset), num_msgs(num_msgs)
    {
    }

    PhaseInfo(unsigned int phase_offset, unsigned int num_msgs)
        : PhaseInfo(phase_offset, 0 /* data_offset */, num_msgs)
    {
    }
};

// Class holding data flow information for a single rational graph.
class DataFlowInfo
{
public:
    // Returns a list of phases on an edge between a given node and a pipe.
    const std::vector<PhaseInfo>& get_edge_phases(const RGBaseNode* source, const RGBasePipe* dest) const;

    // Returns a list of phases on and edge between a given pipe and a node.
    const std::vector<PhaseInfo>& get_edge_phases(const RGBasePipe* source, const RGBaseNode* dest) const;

    unsigned int get_max_num_tiles_per_phase(const RGBaseNode* node) const;

    // Returns how many tiles does a given node send.
    unsigned int get_tiles_to_send(const RGBaseNode* node) const;

    // Returns how many iterations does it take to transfer all epoch tiles for a given pipe.
    unsigned int get_num_iterations_in_epoch(const RGBasePipe* pipe) const;

    // Returns the greatest common divisor of all consume granularities in the subtree starting rooted at the given
    // pipe.
    const unsigned int get_subtree_divisor(const RGBasePipe* pipe) const;

private:
    friend class DataFlowCalculator;

    // Internal helper class used to model phases on an edge between node and a pipe or pipe and a node.
    template<typename Source, typename Destination>
    class DataFlowEdges
    {
    public:
        using EdgePhasesConstIterator =
            typename std::map<std::pair<const Source*, const Destination*>, std::vector<PhaseInfo>>::const_iterator;

        // Returns iterator pointing to a list of phases on a given edge from source to destination.
        EdgePhasesConstIterator get_edge_phases_iterator(const Source* source, const Destination* dest) const
        {
            return m_phases_on_edge.find({source, dest});
        }

        // Saves list of phases on a given edge from source to desination.
        void set_edge_phases(const Source* source, const Destination* dest, const std::vector<PhaseInfo>& phases)
        {
            m_phases_on_edge.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(source, dest),
                                     std::forward_as_tuple(phases));
        }

        // Checks if the edge phases iterator is a valid iterator.
        bool is_valid_edge_phases_iterator(EdgePhasesConstIterator edge_phases_it) const
        {
            return edge_phases_it != m_phases_on_edge.end();
        }

        // Merges other DataFlowEdges object into this by merging the list of phases per edge.
        DataFlowEdges& operator+=(const DataFlowEdges& other)
        {
            for (const auto& pair : other.m_phases_on_edge)
            {
                m_phases_on_edge.insert(pair);
            }

            return *this;
        }

    private:
        // Holds list of phases per source -> destination edge.
        std::map<std::pair<const Source*, const Destination*>, std::vector<PhaseInfo>> m_phases_on_edge;
    };

    // Merges `other` data flow info object into this object.
    DataFlowInfo& operator+=(const DataFlowInfo& other);

    void set_edge_phases(const RGBaseNode* source, const RGBasePipe* dest, const std::vector<PhaseInfo>& phases)
    {
        m_node_2_pipe_edges.set_edge_phases(source, dest, phases);
    }

    void set_edge_phases(const RGBasePipe* source, const RGBaseNode* dest, const std::vector<PhaseInfo>& phases)
    {
        m_pipe_2_node_edges.set_edge_phases(source, dest, phases);
    }

    void set_node_max_num_tiles_per_phase(const RGBaseNode* node, const unsigned int value)
    {
        m_node_2_max_num_tiles_per_phase[node] = value;
    }

    void set_node_msgs_to_send(const RGBaseNode* node, const unsigned int value)
    {
        m_node_2_msgs_to_send[node] = value;
    }

    void set_pipe_num_iterations(const RGBasePipe* pipe, const unsigned int value)
    {
        m_pipe_2_num_iterations_in_epoch[pipe] = value;
    }

    void set_pipe_subtree_divisor(const RGBasePipe* pipe, const unsigned int value)
    {
        m_pipe_2_subtree_divisor[pipe] = value;
    }

    // Holds lists of phases between pairs of nodes and pipes.
    DataFlowEdges<RGBaseNode, RGBasePipe> m_node_2_pipe_edges;

    // Holds lists of phases between pairs of pipes and nodes.
    DataFlowEdges<RGBasePipe, RGBaseNode> m_pipe_2_node_edges;

    // Maximum number of tiles a node can transfer per phases.
    std::unordered_map<const RGBaseNode*, unsigned int> m_node_2_max_num_tiles_per_phase;

    // How many tiles does every node sends.
    std::unordered_map<const RGBaseNode*, unsigned int> m_node_2_msgs_to_send;

    // Number of iterations for every pipe in a rational graph.
    std::unordered_map<const RGBasePipe*, unsigned int> m_pipe_2_num_iterations_in_epoch;

    // The greatest common divisor of all consume granularities in the subtree starting rooted at the given pipe.
    std::unordered_map<const RGBasePipe*, unsigned int> m_pipe_2_subtree_divisor;
};

} // namespace pipegen2