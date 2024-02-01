// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/gather_streams_creator.h"

#include <numeric>

#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> GatherStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;
        const std::vector<const RGBaseNode*> input_nodes = pipe->get_unique_input_nodes();

        const RGBaseNode* output_node = pipe->get_output_nodes()[0];
        const VirtualNode* out_virtual_node = dynamic_cast<const VirtualNode*>(output_node);
        log_assert(out_virtual_node, "Expecting virtual node at the Gather pipe output");

        // TODO: pipegen v1 currently handles gather phases differently, as it forces cumulative total num messages
        // not to exceed 2K tiles, but doesn't reset the tile counter after we switch the scatter group. Pipegen v2
        // will produce different results because it does reset this counter after we switch input scatter group.
        // Make sure that pipegen v2 version works on silicon.
        const std::vector<PhaseInfo> gather_relay_phases = data_flow_info.get_edge_phases(pipe, output_node);

        const unsigned int num_gather_relay_streams =
            std::min<unsigned int>(input_nodes.size(), c_max_gather_relay_streams);

        // TODO: Handle the case where if there are more than 3 gathers on the current pipe's core, no relay streams
        // are created, but rather connects packer input streams directly to the gather stream.
        std::vector<std::unique_ptr<StreamNode>> relay_streams = create_gather_relay_streams(
            pipe, data_flow_info, num_gather_relay_streams, gather_relay_phases);

        std::vector<StreamNode*> relay_streams_raw_ptrs;
        for (const auto& relay_stream : relay_streams)
        {
            relay_streams_raw_ptrs.push_back(relay_stream.get());
        }

        std::unique_ptr<StreamNode> gather_stream = create_gather_stream(pipe, data_flow_info, gather_relay_phases);
        connect_relays_to_gather_stream(relay_streams_raw_ptrs, gather_stream.get());

        connect_inputs_to_relay_streams(input_nodes, pipe, relay_streams_raw_ptrs, data_flow_info);

        std::move(relay_streams.begin(), relay_streams.end(), std::back_inserter(created_streams));

        created_streams.push_back(std::move(gather_stream));

        return created_streams;
    }

    std::vector<std::unique_ptr<StreamNode>> GatherStreamsCreator::create_gather_relay_streams(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int num_gather_relay_streams,
        const std::vector<PhaseInfo>& gather_relay_phases)
    {
        std::vector<std::unique_ptr<StreamNode>> relay_streams;

        for (std::size_t gather_relay_index = 0; gather_relay_index < num_gather_relay_streams; ++gather_relay_index)
        {
            relay_streams.push_back(create_gather_relay_stream(
                pipe, data_flow_info, gather_relay_index, num_gather_relay_streams, gather_relay_phases));
        }

        return relay_streams;
    }

    std::unique_ptr<StreamNode> GatherStreamsCreator::create_gather_relay_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int gather_relay_index,
        const unsigned int num_gather_relay_streams,
        const std::vector<PhaseInfo>& gather_relay_phases)
    {
        std::vector<PhaseInfo> current_relay_phases;
        for (std::size_t i = gather_relay_index; i < gather_relay_phases.size(); i += num_gather_relay_streams)
        {
            current_relay_phases.push_back(gather_relay_phases[i]);
        }

        std::unique_ptr<StreamNode> gather_relay_stream = std::make_unique<StreamNode>(
            StreamType::GatherRelay,
            pipe->get_physical_location(),
            current_relay_phases,
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_node()));

        m_stream_creator->configure_gather_relay_stream(gather_relay_stream.get(), pipe);

        return gather_relay_stream;
    }

    std::unique_ptr<StreamNode> GatherStreamsCreator::create_gather_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const std::vector<PhaseInfo>& gather_stream_phases)
    {
        std::unique_ptr<StreamNode> gather_stream = std::make_unique<StreamNode>(
            StreamType::Gather,
            pipe->get_physical_location(),
            gather_stream_phases,
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_node()));

        m_stream_creator->configure_gather_stream(pipe, gather_stream.get());
        m_stream_creator->set_common_stream_properties_from_pipe(
            gather_stream.get(), pipe, false /* incoming_stream */);

        return gather_stream;
    }

    void GatherStreamsCreator::connect_relays_to_gather_stream(const std::vector<StreamNode*>& relay_streams,
                                                               StreamNode* gather_stream)
    {
        for (StreamNode* relay_stream: relay_streams)
        {
            m_stream_creator->configure_stream_src_and_dest(
                relay_stream, nullptr /* source_stream */, gather_stream /* destination_stream */);
        }
        std::size_t relay_stream_idx = 0;
        for (PhaseConfig& phase_config: gather_stream->get_phase_configs())
        {
            StreamConfig& gather_phase_config = phase_config.config;
            gather_phase_config.set_source(relay_streams[relay_stream_idx]);
            relay_stream_idx = (relay_stream_idx + 1) % relay_streams.size();
        }
    }

    void GatherStreamsCreator::connect_inputs_to_relay_streams(const std::vector<const RGBaseNode*>& input_nodes,
                                                               const RGBasePipe* pipe,
                                                               const std::vector<StreamNode*>& relay_streams,
                                                               const DataFlowInfo& data_flow_info)
    {
        for (const RGBaseNode* input_node: input_nodes)
        {
            const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(input_node);
            log_assert(virtual_node != nullptr, "Expecting virtual node at the Gather pipe input");

            const StreamPhasesCommonConfig& stream_phases_common_config =
                get_stream_node_from_virtual_node(virtual_node);
            StreamNode* input_stream_node = stream_phases_common_config.get_stream_node();

            m_stream_creator->configure_phases_of_input_stream(
                stream_phases_common_config, input_node, pipe, relay_streams, data_flow_info);
        }
    }
}