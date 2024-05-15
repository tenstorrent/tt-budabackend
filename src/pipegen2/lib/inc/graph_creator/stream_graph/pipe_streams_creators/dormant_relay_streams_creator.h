// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
    // Creates relay streams for a non-explicit relay pipes when it is needed.
    class DormantRelayStreamsCreator : public PipeStreamsCreator
    {
    public:
        DormantRelayStreamsCreator(
            std::unique_ptr<NcriscCreator> ncrisc_creator,
            std::unique_ptr<StreamCreator> stream_creator,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
            PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                               virt_node_to_stream_node)
        {
        }

    private:
        std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info) override;

        // Creates a relay stream for a given pipe.
        std::unique_ptr<StreamNode> create_relay_stream(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const std::vector<PhaseInfo>& relay_phases,
            const int num_input_nodes);

        // Connects all input streams to the created relay stream.
        void connect_inputs_to_relay_stream(StreamNode* relay_stream,
                                            const std::vector<const RGBaseNode*>& input_nodes,
                                            const RGBasePipe* pipe,
                                            const DataFlowInfo& data_flow_info);
    };
}