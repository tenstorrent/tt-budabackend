// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/base_rg_pipe.h"

#include <numeric>
#include <unordered_set>

#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
#ifdef TT_DEBUG
    std::string pipe_type_to_string(RGPipeType stream_type)
    {
        switch (stream_type)
        {
            case RGPipeType::DramUnicast:
                return "DramUnicastPipe";
            case RGPipeType::DramTilizer:
                return "DramTilizerPipe";
            case RGPipeType::Unicast:
                return "UnicastPipe";
            case RGPipeType::UnicastToDram:
                return "UnicastToDramPipe";
            case RGPipeType::NonSharedIntermed:
                return "NonSharedIntermedPipe";
            case RGPipeType::DramOutputIntermed:
                return "DramOutputIntermedPipe";
            case RGPipeType::SharedPackerIntermed:
                return "SharedPackerIntermedPipe";
            case RGPipeType::DramMulticast:
                return "DramMulticastPipe";
            case RGPipeType::Multicast:
                return "MulticastPipe";
            case RGPipeType::DramParallelFork:
                return "DramParallelForkPipe";
            case RGPipeType::DramPrefetchPreTMParallelFork:
                return "DramPrefetchPreTMParallelForkPipe";
            case RGPipeType::ParallelFork:
                return "ParallelForkPipe";
            case RGPipeType::SerialFork:
                return "SerialForkPipe";
            case RGPipeType::PaddingSerialFork:
                return "PaddingSerialForkPipe";
            case RGPipeType::DramGather:
                return "DramGatherPipe";
            case RGPipeType::DramEmbedding:
                return "DramEmbeddingPipe";
            case RGPipeType::Gather:
                return "GatherPipe";
            case RGPipeType::GatherToDram:
                return "GatherToDramPipe";
            case RGPipeType::Relay:
                return "RelayPipe";
            case RGPipeType::PCIeUnicast:
                return "PCIeUnicastPipe";
            case RGPipeType::UnicastToPCIe:
                return "UnicastToPCIePipe";
            case RGPipeType::PCIeMulticast:
                return "PCIeMulticastPipe";
            case RGPipeType::GatherToPCIe:
                return "GatherToPCIePipe";
            case RGPipeType::EthernetRelay:
                return "EthernetRelayPipe";
            case RGPipeType::EthernetFWRelay:
                return "EthernetFWRelayPipe";
            case RGPipeType::DormantRelay:
                return "DormantRelayPipe";
            case RGPipeType::Union:
                return "UnionPipe";
            default:
                return "Unknown";
        }
    }
#endif

    const RGBaseNode* RGBasePipe::get_first_input_node() const
    {
        log_assert(!m_inputs.empty(), "Expecting at least one input to the pipe");

        return m_inputs[0].get_node();
    }

    const RGBaseNode* RGBasePipe::get_output_node() const
    {
        log_assert(m_output_nodes.size() == 1, "Expecting only a single node at the pipe output");

        return m_output_nodes[0];
    }

    void RGBasePipe::replace_input_node(const RGBaseNode* old_node, const RGBaseNode* new_node)
    {
        for (PipeInput& pipe_input : m_inputs)
        {
            if (pipe_input.get_node() == old_node)
            {
                pipe_input.set_node(new_node);
            }
        }
    }

    void RGBasePipe::replace_output_node(const RGBaseNode* old_node, const RGBaseNode* new_node)
    {
        for (std::size_t i = 0; i < m_output_nodes.size(); ++i)
        {
            if (m_output_nodes[i] == old_node)
            {
                m_output_nodes[i] = new_node;
            }
        }
    }

    void RGBasePipe::swap_output_nodes(const std::size_t first_output_node_index,
                                       const std::size_t second_output_node_index)
    {
        log_assert(first_output_node_index >= 0 && first_output_node_index < m_output_nodes.size(),
                   "Given first output node index is out of bounds");
        log_assert(second_output_node_index >= 0 && second_output_node_index < m_output_nodes.size(),
                   "Given second output node index is out of bounds");

        std::swap(m_output_nodes[first_output_node_index], m_output_nodes[second_output_node_index]);
    }

    std::vector<const RGBaseNode*> RGBasePipe::get_unique_input_nodes() const
    {
        std::vector<const RGBaseNode*> unique_input_nodes;
        std::unordered_set<const RGBaseNode*> seen_nodes;

        for (const PipeInput& pipe_input : get_inputs())
        {
            const RGBaseNode* input_node = pipe_input.get_node();
            if (seen_nodes.find(input_node) == seen_nodes.end())
            {
                unique_input_nodes.push_back(input_node);
                seen_nodes.insert(input_node);
            }
        }

        return unique_input_nodes;
    }

    std::vector<unsigned int> RGBasePipe::get_input_node_offsets(const RGBaseNode* input_node) const
    {
        std::vector<unsigned int> offsets;

        for (const PipeInput& pipe_input : m_inputs)
        {
            if (pipe_input.get_node() == input_node)
            {
                offsets.push_back(pipe_input.get_offset());
            }
        }

        log_assert(!offsets.empty(), "Tried to get input offsets for the node which is not in pipe inputs");

        return offsets;
    }

    int RGBasePipe::get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const
    {
        int min_num_tiles_to_transfer = 0;

        for (const PipeInput& pipe_input : get_inputs())
        {
            min_num_tiles_to_transfer = std::gcd(
                min_num_tiles_to_transfer, data_flow_info.get_tiles_to_send(pipe_input.get_node()));
        }

        return min_num_tiles_to_transfer;
    }

    int RGBasePipe::get_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const
    {
        return data_flow_info.get_tiles_to_send(m_output_nodes[0]);
    }
}