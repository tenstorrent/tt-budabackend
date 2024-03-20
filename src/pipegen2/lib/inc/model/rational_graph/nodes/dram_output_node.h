// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_output_node.h"
#include "dram_node_interface.h"

namespace pipegen2
{
    class DramOutputNode : public BaseOutputNode, public DramNodeInterface
    {
    public:
        DramOutputNode(NodeId node_id,
                       const tt_cxy_pair& physical_location,
                       unsigned int size_tiles,
                       unsigned int tile_size,
                       unsigned int num_epoch_tiles,
                       unsigned int tiles_per_input,
                       unsigned int operand_id,
                       unsigned int scatter_gather_num_tiles,
                       unsigned int q_slots,
                       bool is_ram,
                       unsigned long dram_address,
                       unsigned int channel,
                       unsigned int subchannel,
                       bool is_remote_io,
                       bool untilized_output,
                       unsigned int untilized_output_r_dim,
                       unsigned int untilized_output_c_dim,
                       unsigned int untilized_output_z_dim,
                       unsigned int untilized_output_full_r_dim,
                       unsigned int untilized_output_full_c_dim,
                       unsigned int untilized_output_tile_dim_r,
                       unsigned int untilized_output_tile_dim_c) :
            BaseOutputNode(node_id, RGNodeType::DramOutput, physical_location, size_tiles, tile_size, num_epoch_tiles,
                           tiles_per_input, operand_id, size_tiles /* transfer_granularity */,
                           scatter_gather_num_tiles),
            m_num_queue_slots(q_slots),
            m_is_ram(is_ram),
            m_dram_address(dram_address),
            m_dram_channel(channel),
            m_dram_subchannel(subchannel),
            m_is_remote_io(is_remote_io),
            m_untilized_output(untilized_output),
            m_untilized_output_r_dim(untilized_output_r_dim),
            m_untilized_output_c_dim(untilized_output_c_dim),
            m_untilized_output_z_dim(untilized_output_z_dim),
            m_untilized_output_full_r_dim(untilized_output_full_r_dim),
            m_untilized_output_full_c_dim(untilized_output_full_c_dim),
            m_untilized_output_tile_dim_r(untilized_output_tile_dim_r),
            m_untilized_output_tile_dim_c(untilized_output_tile_dim_c)
        {
        }

        unsigned int get_num_queue_slots() const override { return m_num_queue_slots; }

        unsigned long get_dram_address() const override { return m_dram_address; }

        unsigned int get_dram_channel() const override { return m_dram_channel; }

        unsigned int get_dram_subchannel() const override { return m_dram_subchannel; }

        bool get_is_ram() const override { return m_is_ram; }

        bool get_is_remote_io() const override { return m_is_remote_io; }

        bool is_untilized_output() const { return m_untilized_output; }

        unsigned int get_untilized_output_r_dim() const { return m_untilized_output_r_dim; }

        unsigned int get_untilized_output_c_dim() const { return m_untilized_output_c_dim; }

        unsigned int get_untilized_output_z_dim() const { return m_untilized_output_z_dim; }

        unsigned int get_untilized_output_full_r_dim() const { return m_untilized_output_full_r_dim; }

        unsigned int get_untilized_output_full_c_dim() const { return m_untilized_output_full_c_dim; }

        unsigned int get_untilized_output_tile_dim_r() const { return m_untilized_output_tile_dim_r; }

        unsigned int get_untilized_output_tile_dim_c() const { return m_untilized_output_tile_dim_c; }

        unsigned int get_untilized_output_tile_size() const;

        unsigned int get_datum_size_bytes() const;

        unsigned int get_datums_row_size_bytes() const;

        // Returns bytes offset in untilized dram buffer corresponding to the given stride index.
        unsigned int get_stride_offset_bytes(const unsigned int stride_index) const;

        // Returns number of bytes in untilized dram buffer's single row of datums.
        unsigned int get_bytes_count_in_untilized_row_of_datums() const;

        // Returns number of bytes in untilized dram buffer's single row of tiles.
        unsigned int get_bytes_count_in_untilized_row_of_tiles() const;

        // Returns number of bytes in untilized dram buffer's row of datums filled by single producer.
        unsigned int get_bytes_count_in_untilized_row_for_single_producer() const;

        // If there are MxN cores that fill the untilized buffer, then this function returns number of bytes written by
        // the N cores in a row of cores.
        unsigned int get_bytes_count_in_untilized_row_of_cores() const;

        // Returns number of bytes in untilized dram buffer.
        unsigned int get_bytes_count_in_untilized_output() const;

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

        // Whether the data in the output buffer is untilized.
        bool m_untilized_output;

        // If untilized, output consists of several parts, where each part is filled by a single producer. Following are
        // properties of an untilized output. Producer's position in the grid of output parts is also known as stride
        // index, where stride index is calculated row-wise.

        // R dimension of a single part in tiles.
        unsigned int m_untilized_output_r_dim;

        // C dimension of a single part in tiles.
        unsigned int m_untilized_output_c_dim;

        // Z dimension of a single part in tiles.
        unsigned int m_untilized_output_z_dim;

        // R dimension of all parts together in tiles.
        unsigned int m_untilized_output_full_r_dim;

        // C dimension of all parts together in tiles.
        unsigned int m_untilized_output_full_c_dim;

        // Untilized output tile R dimension.
        unsigned int m_untilized_output_tile_dim_r;

        // Untilized output tile C dimension.
        unsigned int m_untilized_output_tile_dim_c;
    };
}