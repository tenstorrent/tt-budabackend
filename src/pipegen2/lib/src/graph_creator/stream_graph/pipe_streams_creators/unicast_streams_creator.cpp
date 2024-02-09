// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_streams_creator.h"

#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> UnicastStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;
        created_streams.push_back(create_unpacker_receiving_stream(pipe, data_flow_info));
        StreamNode* unpacker_stream = created_streams[0].get();

        const RGBaseNode* input_node = pipe->get_inputs()[0].get_node();

        const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(input_node);
        log_assert(virt_node, "Expecting virtual node at the Unicast pipe input");

        const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virt_node);
        StreamNode* input_stream_node = stream_phases_common_config.get_stream_node();

        m_stream_creator->configure_phases_of_input_stream(
            stream_phases_common_config, input_node, pipe, {unpacker_stream}, data_flow_info);

        // TODO: this is a hack to make sure that the outgoing noc id is set for scatter pack in the same way as in
        // pipegen1, even though it should be set to pipe's incoming noc id.
        StreamConfig& input_base_config = input_stream_node->get_base_config();
        if (input_base_config.get_is_sending_tiles_out_of_order().value_or(false))
        {
            input_base_config.set_outgoing_data_noc(pipe->get_outgoing_noc_id());
        }

        // There are cases when unpacker node is on a different chip than the input node. In such case we need to use
        // ethernet streams.
        // TODO: at the moment only HW ethernet streams may be used for unpacker destination in this case.
        // In the near future this feature (ethernet_datacopy op in the netlist level) should be updated and this code
        // will maybe be moved to another place more suitable for ethernet streams.
        if (input_stream_node->get_physical_location().chip != unpacker_stream->get_physical_location().chip)
        {
            input_stream_node->get_base_config().set_eth_sender(true);
            unpacker_stream->get_base_config().set_eth_receiver(true);
        }

        return created_streams;
    }

    std::unique_ptr<StreamNode> UnicastStreamsCreator::create_unpacker_receiving_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // Use raw output node to query data flow information, since the unpacker node could have been wrapped into
        // virtual node and thus could result in a non-existent data flow pipe -> node edge if we were to use the
        // concrete unpacker output node.
        const RGBaseNode* output_node = pipe->get_output_node();
        const UnpackerOutputNode* unpacker_node = get_output_node<UnpackerOutputNode>(pipe);
        log_assert(unpacker_node != nullptr, "Expecting unpacker node at the Unicast pipe output");

        const std::vector<PhaseInfo>& phases_info = data_flow_info.get_edge_phases(pipe, output_node);

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