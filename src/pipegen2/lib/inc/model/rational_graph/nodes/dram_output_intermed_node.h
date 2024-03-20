// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_intermed_node.h"
#include "dram_node_interface.h"

namespace pipegen2
{
    class DramOutputIntermedNode : public BaseIntermedNode, public DramNodeInterface
    {
    public:
        DramOutputIntermedNode(NodeId node_id,
                               const tt_cxy_pair& physical_location,
                               unsigned size_tiles,
                               unsigned int tile_size,
                               unsigned int num_epoch_tiles,
                               unsigned int operand_id,
                               unsigned int transfer_granularity,
                               unsigned int scatter_gather_num_tiles,
                               bool is_ram,
                               unsigned long dram_address,
                               unsigned int channel,
                               unsigned int subchannel,
                               bool is_remote_io,
                               BlockingParams&& blocking_params) :
            BaseIntermedNode(node_id, RGNodeType::DramOutputIntermed, physical_location, size_tiles, tile_size,
                             num_epoch_tiles, operand_id, transfer_granularity, scatter_gather_num_tiles,
                             std::move(blocking_params)),
            m_is_ram(is_ram),
            m_dram_address(dram_address),
            m_dram_channel(channel),
            m_dram_subchannel(subchannel),
            m_is_remote_io(is_remote_io)
        {
        }

        unsigned int get_num_queue_slots() const override { return 1; }

        unsigned long get_dram_address() const override { return m_dram_address; }

        unsigned int get_dram_channel() const override { return m_dram_channel; }

        unsigned int get_dram_subchannel() const override { return m_dram_subchannel; }

        bool get_is_ram() const override { return m_is_ram; }

        bool get_is_remote_io() const override { return m_is_remote_io; }

    private:
        // Dram address.
        unsigned long m_dram_address;

        // Dram channel.
        unsigned int m_dram_channel;

        // Dram subchannel.
        unsigned int m_dram_subchannel;

        // Whether this is ram dram queue.
        bool m_is_ram;

        // Whether this dram node connects different chips.
        bool m_is_remote_io;
    };
}