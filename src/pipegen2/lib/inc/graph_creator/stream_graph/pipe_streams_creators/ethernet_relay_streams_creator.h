// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/nodes/ethernet_relay_node.h"
#include "pipe_streams_creator.h"

namespace pipegen2
{
class EthernetRelayStreamsCreator : public PipeStreamsCreator
{
public:
    EthernetRelayStreamsCreator(
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

    // Creates a relay stream for a given pipe.
    std::unique_ptr<StreamNode> create_relay_stream(
        const RGBasePipe* relay_pipe, const EthernetRelayNode* relay_node, const DataFlowInfo& data_flow_info);

    // Sets ethernet stream properties for a given relay pipe.
    virtual void set_ethernet_stream_properties(
        StreamNode* sending_stream,
        StreamNode* receiving_stream,
        const RGBasePipe* relay_pipe,
        const EthernetRelayNode* relay_node);
};
}  // namespace pipegen2