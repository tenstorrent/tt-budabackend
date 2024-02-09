// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dram_input_node.h"

namespace pipegen2
{
    class DramEmbeddingTableInputNode : public DramInputNode
    {
    public:
        DramEmbeddingTableInputNode(NodeId node_id,
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
                                    int embedding_table_core_c_div,
                                    int embedding_table_row_size_per_core,
                                    unsigned int untilized_input_c_dim,
                                    unsigned int untilized_input_tile_dim_r,
                                    unsigned int untilized_input_tile_dim_c,
                                    unsigned int tilize_mblock_n_loop_num_rows,
                                    DramInputNodeType dram_buffer_type) :
            DramInputNode(node_id, physical_location, size_tiles, tile_size, num_epoch_tiles, tiles_per_input,
                          operand_id, num_scatter_chunks, scatter_gather_num_tiles, num_queue_slots, is_ram,
                          dram_address, channel, subchannel, is_remote_io, dram_buffer_type, false),
            m_embedding_table_core_c_div(embedding_table_core_c_div),
            m_embedding_table_row_size_per_core(embedding_table_row_size_per_core),
            m_untilized_input_c_dim(untilized_input_c_dim),
            m_untilized_input_tile_dim_r(untilized_input_tile_dim_r),
            m_untilized_input_tile_dim_c(untilized_input_tile_dim_c),
            m_tilize_mblock_n_loop_num_rows(tilize_mblock_n_loop_num_rows)
        {
            m_node_type = RGNodeType::DramEmbeddingTableInput;
        }

        int get_embedding_table_core_c_div() const { return m_embedding_table_core_c_div; }

        int get_embedding_table_row_size_per_core() const { return m_embedding_table_row_size_per_core; }

        unsigned int get_untilized_input_c_dim() const { return m_untilized_input_c_dim; }

        unsigned int get_untilized_input_tile_dim_r() const { return m_untilized_input_tile_dim_r; }

        unsigned int get_untilized_input_tile_dim_c() const { return m_untilized_input_tile_dim_c; }

        unsigned int get_tilize_mblock_n_loop_num_rows() const { return m_tilize_mblock_n_loop_num_rows; }

    private:
        // How many cores of consumer OP on x grid are fed by one embedding table queue
        // (consumer_op_info.grid_size_logical_c() / queue_info.grid_size.c).
        int m_embedding_table_core_c_div;

        // How many tiles embedding row consists of per core of consumer op.
        int m_embedding_table_row_size_per_core;

        // Embedding table has flat (untilized) layout in DRAM memory, and is tilized on device.
        // This field represents full C dimension of a single core input.
        unsigned int m_untilized_input_c_dim;

        // Embedding table has flat (untilized) layout in DRAM memory, and is tilized on device.
        // This field represents tile dim R to use for tilization.
        unsigned int m_untilized_input_tile_dim_r;

        // Embedding table has flat (untilized) layout in DRAM memory, and is tilized on device.
        // This field represents tile dim C to use for tilization.
        unsigned int m_untilized_input_tile_dim_c;

        // TODO: Math on this in net2pipe is pretty unclear, figure out what this represents.
        unsigned int m_tilize_mblock_n_loop_num_rows;
    };
}