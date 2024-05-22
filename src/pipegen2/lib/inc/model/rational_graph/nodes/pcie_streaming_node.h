// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_rg_node.h"

namespace pipegen2
{
class PCIeStreamingNode : public RGBaseNode
{
public:
    PCIeStreamingNode(
        ChipId chip_id,
        unsigned int size_tiles,
        unsigned int destination_size_tiles,
        unsigned int tile_size,
        unsigned int num_epoch_tiles,
        bool is_streaming_downstream,
        unsigned int mmio_pipe_scatter_index,
        NodeId mmio_pipe_id) :
        RGBaseNode(
            -1,
            RGNodeType::PCIeStreaming,
            tt_cxy_pair(chip_id, -1, -1),
            size_tiles,
            tile_size,
            num_epoch_tiles,
            size_tiles /* tiles_per_input */),
        m_destination_size_tiles(destination_size_tiles),
        m_is_streaming_downstream(is_streaming_downstream),
        m_mmio_pipe_scatter_index(mmio_pipe_scatter_index),
        m_mmio_pipe_id(mmio_pipe_id)
    {
    }

    // Calculates number of queue slots required to configure NCRISC to do the PCIe transfer.
    unsigned int get_num_queue_slots() const { return m_destination_size_tiles / m_size_tiles; }

    // Returns true if streaming through PCIe downstream, false if upstream.
    bool is_streaming_downstream() const { return m_is_streaming_downstream; }

    // Returns the MMIO pipe scatter index this PCIeStreamingNode is created for.
    unsigned int get_mmio_pipe_scatter_index() const { return m_mmio_pipe_scatter_index; }

    // Returns the MMIO pipe id this PCIeStreamingNode is created for.
    NodeId get_mmio_pipe_id() const { return m_mmio_pipe_id; }

    // Returns true if PCIe streaming node is PCIe writer.
    bool is_pcie_writer() const { return get_output_pipes().empty(); }

    // Returns true if PCIe streaming node is PCIe reader.
    bool is_pcie_reader() const { return get_input_pipe() == nullptr; }

private:
    // Necessary buffer size in tiles on the output destination side.
    unsigned int m_destination_size_tiles;

    // True if streaming through PCIe downstream, false if upstream.
    bool m_is_streaming_downstream;

    // MMIO pipe scatter index this PCIeStreamingNode is created for, used for connecting PCIe streams.
    unsigned int m_mmio_pipe_scatter_index;

    // MMIO pipe id this PCIeStreamingNode is created for, used for connecting PCIe streams.
    NodeId m_mmio_pipe_id;
};
}  // namespace pipegen2