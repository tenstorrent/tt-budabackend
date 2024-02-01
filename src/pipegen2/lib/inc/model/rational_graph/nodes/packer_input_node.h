// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_input_node.h"

namespace pipegen2
{
    class PackerInputNode : public BaseInputNode
    {
    public:
        PackerInputNode(NodeId node_id,
                        const tt_cxy_pair& physical_location,
                        unsigned int size_tiles,
                        unsigned int tile_size,
                        unsigned int num_epoch_tiles,
                        unsigned int tiles_per_input,
                        unsigned int operand_id,
                        unsigned int num_scatter_chunks,
                        unsigned int scatter_gather_num_tiles,
                        bool untilized_output,
                        NodeId shared_space_buffer_node_id,
                        BlockingParams&& blocking_params) :
            BaseInputNode(node_id, RGNodeType::PackerInput, physical_location, size_tiles, tile_size, num_epoch_tiles,
                          tiles_per_input, operand_id, num_scatter_chunks, scatter_gather_num_tiles),
            m_untilized_output(untilized_output),
            m_shared_space_buffer_node_id(shared_space_buffer_node_id),
            m_blocking_params(std::move(blocking_params))
        {
        }

        bool is_untilized_output() const { return m_untilized_output; }

        // TODO: Extract -1 as c_invalid_node_id constant.
        bool is_sharing_buffer() const { return m_shared_space_buffer_node_id != -1; }

        NodeId get_shared_space_buffer_node_id() const { return m_shared_space_buffer_node_id; }

        unsigned int get_kernel_output_index() const { return m_kernel_output_index; }

        void set_kernel_output_index(unsigned int kernel_output_index) { m_kernel_output_index = kernel_output_index; }

        const BlockingParams& get_blocking_params() const { return m_blocking_params; }

    private:
        // Does the packer untilize its output.
        bool m_untilized_output;

        // Packer may share its buffer with another node on the same core. This id is used to identify that node.
        NodeId m_shared_space_buffer_node_id;

        // Kernel output index of the packer.
        unsigned int m_kernel_output_index;

        // Packer OP blocking parameters.
        BlockingParams m_blocking_params;
    };
}