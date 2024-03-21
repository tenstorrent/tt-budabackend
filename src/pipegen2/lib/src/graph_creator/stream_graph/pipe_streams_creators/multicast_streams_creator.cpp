// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/multicast_streams_creator.h"

#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/pipes/fork/multicast_pipe.h"
#include "utils/logger.hpp"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> MulticastStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;

        const MulticastPipe* multicast_pipe = dynamic_cast<const MulticastPipe*>(pipe);
        log_assert(multicast_pipe, "MulticastStreamsCreator invoked for wrong type of pipe");

        const RGBaseNode* input_node = multicast_pipe->get_inputs()[0].get_node();
        const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(input_node);
        log_assert(virt_node, "Expecting virtual node at the Multicast pipe input");

        const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virt_node);
        StreamNode* input_stream = stream_phases_common_config.get_stream_node();

        StreamNode* multicast_stream_ptr = nullptr;

        if (should_create_multicast_stream(input_stream, multicast_pipe))
        {
            created_streams.emplace_back(create_multicast_stream(multicast_pipe, data_flow_info));
            multicast_stream_ptr = created_streams.back().get();

            m_stream_creator->configure_phases_of_input_stream(
                stream_phases_common_config, input_node, multicast_pipe, {multicast_stream_ptr}, data_flow_info);
        }
        else
        {
            if (is_direct_packer_multicast(input_stream, multicast_pipe))
            {
                // We are converting packer stream so we have to configure its phases.
                m_stream_creator->configure_phases_of_input_stream(
                    stream_phases_common_config, input_node, multicast_pipe, {}, data_flow_info);
            }

            convert_input_stream_to_multicast(multicast_pipe, input_stream);
            multicast_stream_ptr = input_stream;
        }

        std::vector<std::unique_ptr<StreamNode>> unpacker_streams = create_unpacker_receiving_streams(
            multicast_pipe, data_flow_info);

        m_stream_creator->configure_multicast_streams_src_and_dest(multicast_stream_ptr, unpacker_streams);

        std::move(unpacker_streams.begin(), unpacker_streams.end(), std::back_inserter(created_streams));

        return created_streams;
    }

    bool MulticastStreamsCreator::should_create_multicast_stream(const StreamNode* input_stream,
                                                                 const MulticastPipe* multicast_pipe)
    {
        if (is_direct_packer_multicast(input_stream, multicast_pipe))
        {
            // In case of optimized packer multicast we shouldn't create separate multicast stream.
            return false;
        }

        // We need to create a separate multicast stream in one of the two following scenarios:
        //  1) Input stream node is on different core than the core multicast stream should be allocated (e.g. packer
        //     buffer on a different core, or a relay buffer),
        //  2) Input stream is prefetch Pre-TM relay stream.
        const bool input_stream_on_same_core = input_stream->get_physical_location() ==
                                               multicast_pipe->get_physical_location();
        const bool is_input_prefetch_relay = input_stream->get_base_config().get_dram_input().value_or(false);

        // TODO: Remove this.
        // This is temporary hack to enforce creation of multicast stream that are following relay streams associated
        // with relay buffers. It's not needed, but it's easier to test vs pipegen_v1 this way.
        const bool is_input_relay_buffer = input_stream->get_base_config().get_buf_id().has_value();

        return !input_stream_on_same_core || is_input_prefetch_relay || is_input_relay_buffer;
    }

    bool MulticastStreamsCreator::is_direct_packer_multicast(const StreamNode* input_stream,
                                                             const MulticastPipe* multicast_pipe)
    {
        return input_stream->get_stream_type() == StreamType::Packer &&
               multicast_pipe->is_packer_multicast_optimization_enabled();
    }

    std::unique_ptr<StreamNode> MulticastStreamsCreator::create_multicast_stream(const RGBasePipe* pipe,
                                                                                 const DataFlowInfo& data_flow_info)
    {
        std::unique_ptr<StreamNode> multicast_stream = std::make_unique<StreamNode>(
            StreamType::Multicast,
            pipe->get_physical_location(),
            data_flow_info.get_edge_phases(pipe, pipe->get_output_nodes()[0]),
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_nodes()[0]));

        m_stream_creator->configure_multicast_stream(pipe, multicast_stream.get());
        m_stream_creator->set_common_stream_properties_from_pipe(
            multicast_stream.get(), pipe, false /* incoming_stream */);

        return multicast_stream;
    }

    void MulticastStreamsCreator::convert_input_stream_to_multicast(const MulticastPipe* multicast_pipe,
                                                                    StreamNode* input_stream)
    {
        const bool is_direct_packer_multicast =
            MulticastStreamsCreator::is_direct_packer_multicast(input_stream, multicast_pipe);

        log_assert(input_stream->get_stream_type() == StreamType::Gather ||
                   input_stream->get_stream_type() == StreamType::Relay ||
                   is_direct_packer_multicast,
                   "Can't convert stream of given type to multicast stream");

        if (input_stream->get_stream_type() == StreamType::Relay || is_direct_packer_multicast)
        {
            m_stream_creator->set_common_stream_properties_from_pipe(
                input_stream, multicast_pipe, false /* incoming_stream */);
        }

        m_stream_creator->convert_stream_to_multicast(is_direct_packer_multicast, input_stream);
    }

    std::vector<std::unique_ptr<StreamNode>> MulticastStreamsCreator::create_unpacker_receiving_streams(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> unpacker_streams;

        for (std::size_t i = 0; i < pipe->get_output_nodes().size(); ++i)
        {
            unpacker_streams.push_back(create_unpacker_receiving_stream(pipe, i, data_flow_info));
        }

        return unpacker_streams;
    }

    std::unique_ptr<StreamNode> MulticastStreamsCreator::create_unpacker_receiving_stream(
        const RGBasePipe* pipe,
        const std::size_t output_index,
        const DataFlowInfo& data_flow_info)
    {
        // Use raw output node for data flow info queries.
        const RGBaseNode* output_node = pipe->get_output_nodes()[output_index];
        const std::vector<PhaseInfo>& phases_info = data_flow_info.get_edge_phases(pipe, output_node);

        // Use unpacker output node for stream creation.
        const UnpackerOutputNode* unpacker_node = get_output_node<UnpackerOutputNode>(pipe, output_index);

        std::unique_ptr<StreamNode> unpacker_stream = std::make_unique<StreamNode>(
            StreamType::Unpacker,
            unpacker_node->get_physical_location(),
            phases_info,
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(output_node),
            unpacker_node->get_operand_id());

        m_stream_creator->configure_noc_to_unpacker_stream(unpacker_node, unpacker_stream.get());

        return unpacker_stream;
    }
}