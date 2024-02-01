// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/nodes/base_rg_node.h"

#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
#ifdef DEBUG
    std::string node_type_to_string(RGNodeType node_type)
    {
        switch (node_type)
        {
            case RGNodeType::DramInput:
                return "DramInputNode";
            case RGNodeType::DramEmbeddingIndexInput:
                return "DramEmbeddingIndexInputNode";
            case RGNodeType::DramEmbeddingTableInput:
                return "DramEmbeddingTableInputNode";
            case RGNodeType::DramOutput:
                return "DramOutputNode";
            case RGNodeType::DramOutputIntermed:
                return "DramOutputIntermedNode";
            case RGNodeType::Relay:
                return "RelayNode";
            case RGNodeType::EthernetRelay:
                return "EthernetRelayNode";
            case RGNodeType::NonSharedIntermed:
                return "NonSharedIntermedNode";
            case RGNodeType::PackerInput:
                return "PackerInputNode";
            case RGNodeType::PCIeStreaming:
                return "PCIeStreamingNode";
            case RGNodeType::Virtual:
                return "VirtualNode";
            case RGNodeType::SerialFork:
                return "SerialForkNode";
            case RGNodeType::SharedPackerIntermed:
                return "SharedPackerIntermedNode";
            case RGNodeType::UnpackerOutput:
                return "UnpackerOutputNode";
            default:
                return "Unknown";
        }
    }

    std::string node_type_to_color(RGNodeType node_type)
    {
        switch (node_type)
        {
            case RGNodeType::DramInput:
            case RGNodeType::DramEmbeddingIndexInput:
            case RGNodeType::DramEmbeddingTableInput:
            case RGNodeType::DramOutput:
            case RGNodeType::DramOutputIntermed:
                return "#8A2BE2";
            case RGNodeType::Relay:
            case RGNodeType::EthernetRelay:
                return "#ADFF2F";
            case RGNodeType::NonSharedIntermed:
            case RGNodeType::SharedPackerIntermed:
                return "#8B4513";
            case RGNodeType::PackerInput:
            case RGNodeType::Virtual:
            case RGNodeType::SerialFork:
            case RGNodeType::UnpackerOutput:
                return "#00FF00";
            case RGNodeType::PCIeStreaming:
                return "#800080";
            default:
                return "#FFFFFF";
        }
    }
#endif

    RGBasePipe* RGBaseNode::get_output_pipe() const
    {
        ASSERT(!m_output_pipes.empty(), "Tried to get output pipe for the node which has no outputs");
        return *m_output_pipes.begin();
    }

    void RGBaseNode::add_output_pipe(RGBasePipe* pipe)
    {
        if (m_output_pipes_set.find(pipe) != m_output_pipes_set.end())
        {
            return;
        }
        m_output_pipes.push_back(pipe);
        m_output_pipes_set.insert(pipe);
    }

    void RGBaseNode::remove_output_pipe(RGBasePipe* pipe)
    {
        if (m_output_pipes_set.erase(pipe) > 0)
        {
            m_output_pipes.erase(std::find(m_output_pipes.begin(), m_output_pipes.end(), pipe));
        }
    }

    void RGBaseNode::replace_output_pipe(RGBasePipe* old_pipe, RGBasePipe* new_pipe)
    {
        if (m_output_pipes_set.erase(old_pipe) > 0)
        {
            m_output_pipes_set.insert(new_pipe);
            std::replace(m_output_pipes.begin(), m_output_pipes.end(), old_pipe, new_pipe);
        }
    }
}