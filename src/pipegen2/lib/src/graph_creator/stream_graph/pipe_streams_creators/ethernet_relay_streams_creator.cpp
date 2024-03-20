// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/ethernet_relay_streams_creator.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> EthernetRelayStreamsCreator::create_streams_internal(
        const RGBasePipe* relay_pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;

        const EthernetRelayNode* relay_node = get_output_node<EthernetRelayNode>(relay_pipe);
        log_assert(relay_node != nullptr, "Expecting EthernetRelayNode at the EthernetRelay pipe output");

        std::unique_ptr<StreamNode> relay_stream = create_relay_stream(relay_pipe, relay_node, data_flow_info);

        const RGBaseNode* input_node = relay_pipe->get_inputs()[0].get_node();
        const VirtualNode* in_virtual_node = dynamic_cast<const VirtualNode*>(input_node);
        log_assert(in_virtual_node, "Expecting virtual node at the Relay pipe input");

        const StreamPhasesCommonConfig& in_stream_phases_common_config =
            get_stream_node_from_virtual_node(in_virtual_node);

        m_stream_creator->configure_phases_of_input_stream(
            in_stream_phases_common_config, input_node, relay_pipe, {relay_stream.get()}, data_flow_info);

        StreamNode* input_stream_node = in_stream_phases_common_config.get_stream_node();

        set_ethernet_stream_properties(input_stream_node, relay_stream.get(), relay_pipe, relay_node);

        created_streams.push_back(std::move(relay_stream));
        return created_streams;
    }

    std::unique_ptr<StreamNode> EthernetRelayStreamsCreator::create_relay_stream(
        const RGBasePipe* relay_pipe,
        const EthernetRelayNode* relay_node,
        const DataFlowInfo& data_flow_info)
    {
        const std::vector<PhaseInfo>& relay_phases = data_flow_info.get_edge_phases(
            relay_pipe, relay_pipe->get_output_node());
        std::unique_ptr<StreamNode> relay_stream = std::make_unique<StreamNode>(
            StreamType::Relay,
            relay_node->get_physical_location(),
            relay_phases,
            data_flow_info.get_num_iterations_in_epoch(relay_pipe),
            data_flow_info.get_max_num_tiles_per_phase(relay_pipe->get_output_node()));

        m_stream_creator->configure_relay_stream(
            relay_stream.get(),
            relay_node->get_size_tiles(),
            relay_node->get_tile_size());

        relay_stream->get_base_config().set_buf_id(relay_node->get_id());

        return relay_stream;
    }

    void EthernetRelayStreamsCreator::set_ethernet_stream_properties(StreamNode* sending_stream,
                                                                     StreamNode* receiving_stream,
                                                                     const RGBasePipe* relay_pipe,
                                                                     const EthernetRelayNode* relay_node)
    {
        log_assert(sending_stream->get_physical_location().chip != receiving_stream->get_physical_location().chip,
                       "Expecting different chips for sending and receiving streams of EthernetRelay pipe");

        sending_stream->get_base_config().set_eth_sender(true);
        receiving_stream->get_base_config().set_eth_receiver(true);
    }
}