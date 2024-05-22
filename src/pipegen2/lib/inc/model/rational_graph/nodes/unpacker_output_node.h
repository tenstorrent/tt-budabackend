// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "base_output_node.h"

namespace pipegen2
{
class UnpackerOutputNode : public BaseOutputNode
{
public:
    UnpackerOutputNode(
        NodeId node_id,
        std::string op_name,
        const tt_cxy_pair& physical_location,
        unsigned int size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input,
        unsigned int operand_id,
        unsigned int transfer_granularity,
        unsigned int scatter_gather_num_tiles,
        bool embedding_index_kernel_input = false,
        unsigned int overlay_blob_extra_size = 0) :
        BaseOutputNode(
            node_id,
            RGNodeType::UnpackerOutput,
            physical_location,
            size_tiles,
            tile_size,
            num_epoch_tiles,
            tiles_per_input,
            operand_id,
            transfer_granularity,
            scatter_gather_num_tiles),
        m_op_name(std::move(op_name)),
        m_embedding_index_kernel_input(embedding_index_kernel_input),
        m_overlay_blob_extra_size(overlay_blob_extra_size)
    {
    }

    std::string get_op_name() const { return m_op_name; }

    unsigned int get_kernel_input_index() const { return m_kernel_input_index; }

    void set_kernel_input_index(unsigned int kernel_input_index) { m_kernel_input_index = kernel_input_index; }

    bool is_embedding_index_kernel_input() const { return m_embedding_index_kernel_input; }

    unsigned int get_overlay_blob_extra_size() const { return m_overlay_blob_extra_size; }

private:
    // Netlist op name.
    std::string m_op_name;

    // Kernel input index of the unpacker.
    unsigned int m_kernel_input_index;

    // Whether the unpacker is an embedding index kernel input.
    bool m_embedding_index_kernel_input;

    // TODO: Blobgen relies on this field to be sent through unpacker stream config. This is not the best reasoning
    // as this field is related to the whole overlay on a core, not only a single unpacker. We should think of
    // another way to pass this info to blobgen. For now, this is the simplest solution.
    //
    // Amount of extra space allowed for the overlay blob on the core where the unpacker is located.
    unsigned int m_overlay_blob_extra_size;
};
}  // namespace pipegen2