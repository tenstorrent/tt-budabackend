// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"

namespace pipegen2
{
class PackerInputNodeMock : public PackerInputNode
{
public:
    PackerInputNodeMock(
        NodeId node_id,
        unsigned int size_tiles,
        unsigned int num_epoch_tiles,
        unsigned int tiles_per_input,
        unsigned int num_scatter_chunks,
        unsigned int scatter_gather_num_tiles) :
        PackerInputNode(
            node_id,
            tt_cxy_pair() /* physical_location */,
            size_tiles,
            0 /* tile_size */,
            num_epoch_tiles,
            tiles_per_input,
            0 /* operand_id */,
            num_scatter_chunks,
            scatter_gather_num_tiles,
            false /* untilized_output */,
            -1 /* shared_space_buffer_node_id */,
            RGBaseNode::BlockingParams(0, 0, 0, 0, 0),
            "" /* op_name */)
    {
    }
};

class UnpackerOutputNodeMock : public UnpackerOutputNode
{
public:
    UnpackerOutputNodeMock(NodeId node_id, unsigned int size_tiles, unsigned int transfer_granularity) :
        UnpackerOutputNode(
            node_id,
            "" /* op_name */,
            tt_cxy_pair() /* physical_location */,
            size_tiles,
            0 /* tile_size */,
            0 /* num_epoch_tiles */,
            0 /* tiles_per_input */,
            0 /* operand_id */,
            transfer_granularity,
            0 /* scatter_gather_num_tiles */)
    {
    }
};

// RG node class only used to model structure of rational graph. All fields apart from node id are irrelevant.
class RGNodeStructuralMock : public RGBaseNode
{
public:
    RGNodeStructuralMock(NodeId node_id) :
        RGBaseNode(
            node_id,
            RGNodeType::Virtual /* node_type */,
            tt_cxy_pair() /* physical_location */,
            0 /* size_tiles */,
            0 /* tile_size */,
            0 /* num_epoch_tiles */,
            0 /* tiles_per_input */)
    {
    }
};

}  // namespace pipegen2