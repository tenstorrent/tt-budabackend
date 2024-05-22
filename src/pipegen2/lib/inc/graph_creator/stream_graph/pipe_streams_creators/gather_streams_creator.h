// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
class PackerInputNode;
class UnpackerOutputNode;

class GatherStreamsCreator : public PipeStreamsCreator
{
public:
    GatherStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

private:
    std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

    // Creates a gather relay stream for the given relay index.
    std::unique_ptr<StreamNode> create_gather_relay_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int gather_relay_index,
        const unsigned int num_gather_relay_streams,
        const std::vector<PhaseInfo>& gather_relay_phases);

    // Creates all gather relays streams required.
    std::vector<std::unique_ptr<StreamNode>> create_gather_relay_streams(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int num_gather_relay_streams,
        const std::vector<PhaseInfo>& gather_relay_phases);

    // Creates gather stream.
    std::unique_ptr<StreamNode> create_gather_stream(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info, const std::vector<PhaseInfo>& gather_stream_phases);

    // Connects relay stream to gather stream across all of its phases.
    void connect_relays_to_gather_stream(const std::vector<StreamNode*>& relay_streams, StreamNode* gather_stream);

    // Connects all input streams to the created relay streams.
    void connect_inputs_to_relay_streams(
        const std::vector<const RGBaseNode*>& input_nodes,
        const RGBasePipe* pipe,
        const std::vector<StreamNode*>& relay_streams,
        const DataFlowInfo& data_flow_info);

private:
    static constexpr unsigned int c_max_gather_relay_streams = 3;
};
}  // namespace pipegen2