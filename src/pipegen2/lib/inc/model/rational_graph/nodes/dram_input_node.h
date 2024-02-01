// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_input_node.h"
#include "dram_node_interface.h"

namespace pipegen2
{
    enum class DramInputNodeType
    {
        DramIO,
        DramPrefetchPostTM,
        DramPrefetchPreTM
    };

    class DramInputNode : public BaseInputNode, public DramNodeInterface
    {
    public:
        DramInputNode(NodeId node_id,
                      const tt_cxy_pair& physical_location,
                      unsigned int size_tiles,
                      unsigned int tile_size,
                      unsigned int num_epoch_tiles,
                      unsigned int tiles_per_input,
                      unsigned int operand_id,
                      unsigned int num_scatter_chunks,
                      unsigned int scatter_gather_num_tiles,
                      unsigned int num_queue_slots,
                      bool is_ram,
                      unsigned long dram_address,
                      unsigned int channel,
                      unsigned int subchannel,
                      bool is_remote_io,
                      DramInputNodeType dram_buffer_type,
                      bool is_padding) :
            BaseInputNode(node_id, RGNodeType::DramInput, physical_location, size_tiles, tile_size, num_epoch_tiles,
                          tiles_per_input, operand_id, num_scatter_chunks, scatter_gather_num_tiles),
            m_num_queue_slots(num_queue_slots),
            m_is_ram(is_ram),
            m_dram_address(dram_address),
            m_dram_channel(channel),
            m_dram_subchannel(subchannel),
            m_is_remote_io(is_remote_io),
            m_dram_buffer_type(dram_buffer_type),
            m_is_padding(is_padding)
        {
        }

        unsigned int get_num_queue_slots() const override { return m_num_queue_slots; }

        unsigned long get_dram_address() const override { return m_dram_address; }

        unsigned int get_dram_channel() const override { return m_dram_channel; }

        unsigned int get_dram_subchannel() const override { return m_dram_subchannel; }

        bool get_is_ram() const override { return m_is_ram; }

        bool get_is_remote_io() const override { return m_is_remote_io; }

        DramInputNodeType get_dram_buffer_type() const { return m_dram_buffer_type; }

        bool is_dram_io() const { return m_dram_buffer_type == DramInputNodeType::DramIO; }

        bool is_dram_prefetch_post_tm() const { return m_dram_buffer_type == DramInputNodeType::DramPrefetchPostTM; }

        bool is_dram_prefetch_pre_tm() const { return m_dram_buffer_type == DramInputNodeType::DramPrefetchPreTM; }

        bool is_dram_prefetch() const { return is_dram_prefetch_post_tm() || is_dram_prefetch_pre_tm(); }

        bool is_padding() const { return m_is_padding; }

    private:
        // Number of queue slots in dram for this node.
        unsigned int m_num_queue_slots;

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

        // Type of this DRAM buffer.
        DramInputNodeType m_dram_buffer_type;

        // Is DRAM buffer representing padding.
        // TODO: Make a separate class for padding DRAM input nodes.
        bool m_is_padding;
    };
}