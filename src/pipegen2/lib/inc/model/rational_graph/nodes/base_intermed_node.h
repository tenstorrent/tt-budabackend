// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_rg_node.h"

namespace pipegen2
{
    class BaseIntermedNode : public RGBaseNode
    {
    public:
        unsigned int get_operand_id() const { return m_operand_id; }

        unsigned int get_transfer_granularity() const { return m_transfer_granularity; }

        unsigned int get_scatter_gather_num_tiles() const { return m_scatter_gather_num_tiles; }

        unsigned int get_kernel_input_index() const { return m_kernel_input_index; }

        void set_kernel_input_index(unsigned int kernel_input_index) { m_kernel_input_index = kernel_input_index; }

        unsigned int get_kernel_output_index() const { return m_kernel_output_index; }

        void set_kernel_output_index(unsigned int kernel_output_index) { m_kernel_output_index = kernel_output_index; }

        const BlockingParams& get_blocking_params() const { return m_blocking_params; }

    protected:
        BaseIntermedNode(NodeId node_id,
                         RGNodeType node_type,
                         const tt_cxy_pair& physical_location,
                         unsigned size_tiles,
                         unsigned int tile_size,
                         unsigned int num_epoch_tiles,
                         unsigned int operand_id,
                         unsigned int transfer_granularity,
                         unsigned int scatter_gather_num_tiles,
                         BlockingParams&& blocking_params) :
            RGBaseNode(node_id, node_type, physical_location, size_tiles, tile_size, num_epoch_tiles,
                       1 /* tiles_per_input */),
            m_operand_id(operand_id),
            m_transfer_granularity(transfer_granularity),
            m_scatter_gather_num_tiles(scatter_gather_num_tiles),
            m_blocking_params(std::move(blocking_params))
        {
        }

    private:
        // Operand id.
        unsigned int m_operand_id;

        // Minimal number of tiles node can receive in one step.
        unsigned int m_transfer_granularity;

        // Number of tiles to transfer per scatter input.
        unsigned int m_scatter_gather_num_tiles;

        // Kernel input index of this node.
        unsigned int m_kernel_input_index;

        // Kernel output index of this node.
        unsigned int m_kernel_output_index;

        // OP blocking parameters.
        BlockingParams m_blocking_params;
    };
}