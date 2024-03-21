// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/data_flow_info.h"

#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "utils/logger.hpp"

namespace pipegen2
{

const std::vector<PhaseInfo>& DataFlowInfo::get_edge_phases(const RGBaseNode* source, const RGBasePipe* dest) const
{
    auto it = m_node_2_pipe_edges.get_edge_phases_iterator(source, dest);
    log_assert(m_node_2_pipe_edges.is_valid_edge_phases_iterator(it),
               "Expecting to find edge phases from node {} to pipe {}", source->get_id(), dest->get_id());

    return it->second;
}

const std::vector<PhaseInfo>& DataFlowInfo::get_edge_phases(const RGBasePipe* source, const RGBaseNode* dest) const
{
    auto it = m_pipe_2_node_edges.get_edge_phases_iterator(source, dest);
    log_assert(m_pipe_2_node_edges.is_valid_edge_phases_iterator(it),
               "Expecting to find edge phases from pipe {} to node {}", source->get_id(), dest->get_id());

    return it->second;
}

unsigned int DataFlowInfo::get_max_num_tiles_per_phase(const RGBaseNode* node) const
{
    auto it = m_node_2_max_num_tiles_per_phase.find(node);
    log_assert(it != m_node_2_max_num_tiles_per_phase.end(),
               "Expecting to find data flow info for node {}", node->get_id());

    return it->second;
}

unsigned int DataFlowInfo::get_tiles_to_send(const RGBaseNode* node) const
{
    auto it = m_node_2_msgs_to_send.find(node);
    log_assert(it != m_node_2_msgs_to_send.end(),
               "Expecting to find data flow info for node {}", node->get_id());

    return it->second;
}

unsigned int DataFlowInfo::get_num_iterations_in_epoch(const RGBasePipe* pipe) const
{
    auto it = m_pipe_2_num_iterations_in_epoch.find(pipe);
    log_assert(it != m_pipe_2_num_iterations_in_epoch.end(),
               "Expecting to find data flow info for pipe {}", pipe->get_id());

    return it->second;
}

const unsigned int DataFlowInfo::get_subtree_divisor(const RGBasePipe* pipe) const
{
    auto it = m_pipe_2_subtree_divisor.find(pipe);
    log_assert(it != m_pipe_2_subtree_divisor.end(),
               "Expecting to find data flow info for pipe {}", pipe->get_id());

    return it->second;
}

DataFlowInfo& DataFlowInfo::operator+=(const DataFlowInfo& other)
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

} // namespace pipegen2