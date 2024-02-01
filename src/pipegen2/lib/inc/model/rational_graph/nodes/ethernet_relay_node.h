// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "relay_node.h"

namespace pipegen2
{
    class EthernetRelayNode : public RelayNode
    {
    public:
        EthernetRelayNode(NodeId node_id,
                  const tt_cxy_pair& physical_location,
                  unsigned int size_tiles,
                  unsigned int tile_size,
                  unsigned int num_epoch_tiles,
                  unsigned int tiles_per_input,
                  unsigned int operand_id,
                  unsigned int scatter_gather_num_tiles,
                  bool use_ethernet_fw_stream) :
            RelayNode(node_id, physical_location, size_tiles, tile_size, num_epoch_tiles, tiles_per_input, operand_id,
                      scatter_gather_num_tiles),
            m_use_ethernet_fw_stream(use_ethernet_fw_stream)
        {
            m_node_type = RGNodeType::EthernetRelay;
        }

        bool get_use_fw_ethernet_stream() const { return m_use_ethernet_fw_stream; }

    private:
        // Indicates whether FW should be used to send the data over ethernet.
        bool m_use_ethernet_fw_stream;
    };
}