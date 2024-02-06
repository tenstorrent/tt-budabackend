// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_ids_allocator.h"

namespace pipegen2
{
    StreamIDsAllocator::StreamIDsAllocator(ResourceManager* resource_manager) :
        m_resource_manager(resource_manager)
    {
    }

    StreamIDsAllocator::~StreamIDsAllocator()
    {
    }

    void StreamIDsAllocator::allocate_stream_ids(const StreamGraphCollection* stream_graph_collection)
    {
        for (StreamNode* stream_node : stream_graph_collection->get_streams())
        {
            if (stream_node->has_stream_id())
            {
                // If this stream is part of a packer fork streams, it will already have stream id allocated.
                continue;
            }

            if (stream_node->get_stream_type() == StreamType::Multicast)
            {
                stream_node->assign_stream_id(
                    m_resource_manager->allocate_multicast_stream(stream_node->get_physical_location()));
            }
            else if (stream_node->get_stream_type() == StreamType::PackerMulticast)
            {
                stream_node->assign_stream_id(
                    m_resource_manager->allocate_packer_multicast_stream(stream_node->get_physical_location(),
                                                                         stream_node->get_operand_id()));
            }
            else if (stream_node->get_stream_type() == StreamType::Packer)
            {
                allocate_packer_stream_id(stream_node);
            }
            else if (stream_node->get_stream_type() == StreamType::Unpacker)
            {
                if (stream_node->get_base_config().get_eth_receiver().value_or(false))
                {
                    stream_node->assign_stream_id(
                        m_resource_manager->allocate_ethernet_stream(stream_node->get_physical_location()));
                }
                else
                {
                    stream_node->assign_stream_id(
                        m_resource_manager->allocate_unpacker_stream(stream_node->get_physical_location(),
                                                                     stream_node->get_operand_id()));
                }
            }
            else if (stream_node->get_stream_type() == StreamType::Gather)
            {
                stream_node->assign_stream_id(
                    m_resource_manager->allocate_gather_stream(stream_node->get_physical_location()));
            }
            else if (stream_node->get_stream_type() == StreamType::Intermed)
            {
                stream_node->assign_stream_id(
                    m_resource_manager->allocate_intermed_stream(stream_node->get_physical_location(),
                                                                 stream_node->get_operand_id()));
            }
            else if (stream_node->get_stream_type() == StreamType::Relay)
            {
                allocate_relay_stream_id(stream_node);
            }
            else
            {
                stream_node->assign_stream_id(
                    m_resource_manager->allocate_general_purpose_stream(stream_node->get_physical_location()));
            }
        }
    }

    void StreamIDsAllocator::allocate_packer_stream_id(StreamNode* stream_node)
    {
        ASSERT(stream_node->get_base_config().get_fork_streams().has_value(),
               "Packer stream must have fork streams set");
        const std::vector<StreamNode*>& fork_streams = stream_node->get_base_config().get_fork_streams().value();
        for (std::size_t i = 0; i < fork_streams.size(); ++i)
        {
            StreamNode* fork_stream = fork_streams[i];

            if (fork_stream->has_stream_id())
            {
                // This can happen only for packer multicast streams, because other streams in packer fork list are
                // of Packer stream type, so their ID would be assigned in this function.
                ASSERT(fork_stream->get_stream_type() == StreamType::PackerMulticast,
                       "Already allocated stream ID for some of the packer fork streams");
                continue;
            }

            const bool is_first_packer_fork_stream = i == 0;

            if (is_first_packer_fork_stream)
            {
                // Optimized packer multicast can only be on the first fork path.
                if (fork_stream->get_stream_type() == StreamType::PackerMulticast)
                {
                    fork_stream->assign_stream_id(
                        m_resource_manager->allocate_packer_multicast_stream(fork_stream->get_physical_location(),
                                                                             fork_stream->get_operand_id()));
                }
                else
                {
                    fork_stream->assign_stream_id(
                        m_resource_manager->allocate_packer_stream(fork_stream->get_physical_location(),
                                                                   fork_stream->get_operand_id()));
                }
            }
            else if (fork_stream->get_stream_type() == StreamType::Intermed)
            {
                fork_stream->assign_stream_id(
                    m_resource_manager->allocate_intermed_stream(fork_stream->get_physical_location(),
                                                                 fork_stream->get_operand_id()));
            }
            else
            {
                fork_stream->assign_stream_id(
                    m_resource_manager->allocate_general_purpose_stream(fork_stream->get_physical_location()));
            }
        }
    }

    void StreamIDsAllocator::allocate_relay_stream_id(StreamNode* stream_node)
    {
        StreamConfig& base_config = stream_node->get_base_config();
        const bool eth_fw_stream = base_config.get_use_ethernet_fw_stream().value_or(false);
        const bool eth_stream = base_config.get_eth_receiver().value_or(false) ||
                                base_config.get_eth_sender().value_or(false);
        const bool eth_hw_stream = eth_stream && !eth_fw_stream;
        if (eth_hw_stream)
        {
            stream_node->assign_stream_id(
                m_resource_manager->allocate_ethernet_stream(stream_node->get_physical_location()));
        }
        else
        {
            stream_node->assign_stream_id(
                m_resource_manager->allocate_general_purpose_stream(stream_node->get_physical_location()));
        }
    }
}