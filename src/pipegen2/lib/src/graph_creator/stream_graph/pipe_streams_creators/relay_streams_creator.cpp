// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/relay_streams_creator.h"

// clang-format off
#include "utils/logger.hpp"

#include "model/rational_graph/nodes/relay_node.h"
// clang-format on

namespace pipegen2
{
std::vector<std::unique_ptr<StreamNode>> RelayStreamsCreator::create_streams_internal(
    const RGBasePipe* relay_pipe, const DataFlowInfo& data_flow_info)
{
    std::vector<std::unique_ptr<StreamNode>> created_streams;

    std::unique_ptr<StreamNode> relay_stream = create_relay_stream(relay_pipe, data_flow_info);

    const RGBaseNode* input_node = relay_pipe->get_inputs()[0].get_node();
    const VirtualNode* in_virtual_node = dynamic_cast<const VirtualNode*>(input_node);
    log_assert(in_virtual_node, "Expecting virtual node at the Relay pipe input");

    const StreamPhasesCommonConfig& in_stream_phases_common_config = get_stream_node_from_virtual_node(in_virtual_node);

    m_stream_creator->configure_phases_of_input_stream(
        in_stream_phases_common_config, input_node, relay_pipe, {relay_stream.get()}, data_flow_info);

    StreamNode* input_stream_node = in_stream_phases_common_config.get_stream_node();

    // TODO: this is a hack to make sure that the outgoing noc id is set for scatter pack in the same way as in
    // pipegen1, even though it should be set to pipe's incoming noc id.
    StreamConfig& input_base_config = input_stream_node->get_base_config();
    if (input_base_config.get_is_sending_tiles_out_of_order().value_or(false))
    {
        input_base_config.set_outgoing_data_noc(relay_pipe->get_outgoing_noc_id());
    }

    created_streams.push_back(std::move(relay_stream));
    return created_streams;
}

std::unique_ptr<StreamNode> RelayStreamsCreator::create_relay_stream(
    const RGBasePipe* relay_pipe, const DataFlowInfo& data_flow_info)
{
    const RelayNode* relay_node = get_output_node<RelayNode>(relay_pipe);
    log_assert(relay_node != nullptr, "Expecting relay node at the Relay pipe output");

    const std::vector<PhaseInfo>& relay_phases =
        data_flow_info.get_edge_phases(relay_pipe, relay_pipe->get_output_nodes()[0]);
    std::unique_ptr<StreamNode> relay_stream = std::make_unique<StreamNode>(
        StreamType::Relay,
        relay_node->get_physical_location(),
        relay_phases,
        data_flow_info.get_num_iterations_in_epoch(relay_pipe),
        data_flow_info.get_max_num_tiles_per_phase(relay_pipe->get_output_node()));

    m_stream_creator->configure_relay_stream(
        relay_stream.get(), relay_node->get_size_tiles(), relay_node->get_tile_size());

    relay_stream->get_base_config().set_buf_id(relay_node->get_id());

    return relay_stream;
}
}  // namespace pipegen2