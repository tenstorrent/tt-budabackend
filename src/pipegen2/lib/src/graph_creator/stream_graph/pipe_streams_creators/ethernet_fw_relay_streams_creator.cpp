// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/ethernet_fw_relay_streams_creator.h"

#include "pipegen2_exceptions.h"

namespace pipegen2
{
void EthernetFWRelayStreamsCreator::set_ethernet_stream_properties(
    StreamNode* sending_stream,
    StreamNode* receiving_stream,
    const RGBasePipe* relay_pipe,
    const EthernetRelayNode* relay_node)
{
    log_assert(
        sending_stream->get_physical_location().chip != receiving_stream->get_physical_location().chip,
        "Expecting different chips for sending and receiving streams of EthernetRelay pipe");
    log_assert(relay_node->get_use_fw_ethernet_stream(), "Expecting FW ethernet node for FW relay streams creator");

    sending_stream->get_base_config().set_eth_sender(true);
    receiving_stream->get_base_config().set_eth_receiver(true);

    // Kernel input/output index are allocated here instead of in RG creator because at the point of RG creation, we
    // don't know the location of the sending stream. It is fine to allocate them here since on ethernet cores,
    // there can't be any intermedaite streams so we will never run into a problem where we have the intermediate
    // stream with input/output index smaller than the index of the packer/unpacker stream on the same core.
    // TODO: Remove this comment once tenstorrent/budabackend#2074
    // is fixed and the explicit ordering in RG is removed.
    sending_stream->get_base_config().set_use_ethernet_fw_stream(true);
    sending_stream->get_base_config().opt_set_remote_receiver(std::nullopt);
    sending_stream->get_base_config().set_receiver_endpoint(true);
    sending_stream->get_base_config().set_input_index(
        m_resource_manager->allocate_kernel_input(sending_stream->get_physical_location()));

    receiving_stream->get_base_config().set_use_ethernet_fw_stream(true);
    receiving_stream->get_base_config().opt_set_remote_source(std::nullopt);
    receiving_stream->get_base_config().set_source_endpoint(true);
    receiving_stream->get_base_config().set_output_index(
        m_resource_manager->allocate_kernel_output(receiving_stream->get_physical_location()));

    if (relay_node->get_size_tiles() > relay_node->get_scatter_gather_num_tiles() &&
        relay_node->get_size_tiles() % relay_node->get_scatter_gather_num_tiles() != 0)
    {
        throw InvalidPipeGraphSpecificationException(
            "Invalid buffer size in tiles for ethernet fw stream producer buffer " +
                std::to_string(relay_node->get_size_tiles()) + ", which is not divisible by the scatter gather " +
                "num tiles which is " + std::to_string(relay_node->get_scatter_gather_num_tiles()),
            std::nullopt /* logical_location */,
            std::make_optional(relay_node->get_physical_location()));
    }

    receiving_stream->get_base_config().set_num_msgs_in_block(
        std::min(relay_node->get_scatter_gather_num_tiles(), relay_node->get_size_tiles()));
}
}  // namespace pipegen2