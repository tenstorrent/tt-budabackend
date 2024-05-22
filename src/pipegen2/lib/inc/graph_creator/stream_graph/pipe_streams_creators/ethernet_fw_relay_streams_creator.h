// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ethernet_relay_streams_creator.h"
#include "model/rational_graph/nodes/ethernet_relay_node.h"

namespace pipegen2
{
class EthernetFWRelayStreamsCreator : public EthernetRelayStreamsCreator
{
public:
    EthernetFWRelayStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        EthernetRelayStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

private:
    // Overrides the base class method to set ethernet stream properties for a given pipe. In this case, streams are
    // configured to use the FW for ethernet communication.
    void set_ethernet_stream_properties(
        StreamNode* sending_stream,
        StreamNode* receiving_stream,
        const RGBasePipe* relay_pipe,
        const EthernetRelayNode* relay_node) override;
};
}  // namespace pipegen2