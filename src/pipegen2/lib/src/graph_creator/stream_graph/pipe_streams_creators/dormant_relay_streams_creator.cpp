// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dormant_relay_streams_creator.h"

#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> DormantRelayStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;
        const std::vector<const RGBaseNode*> input_nodes = pipe->get_unique_input_nodes();

        const RGBaseNode* output_node = pipe->get_output_node();
        const VirtualNode* out_virtual_node = dynamic_cast<const VirtualNode*>(output_node);
        log_assert(out_virtual_node, "Expecting virtual node at the Relay pipe output");

        const std::vector<PhaseInfo> relay_phases = data_flow_info.get_edge_phases(pipe, output_node);
        std::unique_ptr<StreamNode> relay_stream = create_relay_stream(
            pipe, data_flow_info, relay_phases, input_nodes.size());

        connect_inputs_to_relay_stream(relay_stream.get(), input_nodes, pipe, data_flow_info);

        created_streams.push_back(std::move(relay_stream));

        return created_streams;
    }

    std::unique_ptr<StreamNode> DormantRelayStreamsCreator::create_relay_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const std::vector<PhaseInfo>& relay_phases,
        const int num_input_nodes)
    {
        std::unique_ptr<StreamNode> relay_stream = std::make_unique<StreamNode>(
            StreamType::Relay,
            pipe->get_physical_location(),
            relay_phases,
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_node()));

        m_stream_creator->configure_relay_stream_with_default_buffer_size(
            relay_stream.get(),
            num_input_nodes,
            pipe->get_output_node()->get_tile_size(),
            m_resource_manager->is_ethernet_core(pipe->get_physical_location()));

        return relay_stream;
    }

    void DormantRelayStreamsCreator::connect_inputs_to_relay_stream(StreamNode* relay_stream,
                                                                    const std::vector<const RGBaseNode*>& input_nodes,
                                                                    const RGBasePipe* pipe,
                                                                    const DataFlowInfo& data_flow_info)
    {
        for (const RGBaseNode* input_node: input_nodes)
        {
            const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(input_node);
            log_assert(virtual_node != nullptr, "Expecting virtual node at the Relay pipe input");

            const StreamPhasesCommonConfig& stream_phases_common_config =
                get_stream_node_from_virtual_node(virtual_node);
            StreamNode* input_stream_node = stream_phases_common_config.get_stream_node();

            m_stream_creator->configure_phases_of_input_stream(
                stream_phases_common_config, input_node, pipe, {relay_stream}, data_flow_info);
        }
    }
}