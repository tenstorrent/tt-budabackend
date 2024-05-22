// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/l1/l1_buffer.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{

// Represents an allocation of the stream buffer in L1 memory of a core.
class StreamL1Buffer : public L1Buffer
{
public:
    StreamL1Buffer(const StreamNode* stream_node, const unsigned int buffer_address, const unsigned int buffer_size) :
        L1Buffer(stream_node->get_physical_location(), buffer_address, buffer_size), m_stream_node(stream_node)
    {
    }

    std::string get_name() const override;

    std::string get_allocation_info() const override;

private:
    static std::string stream_type_to_string(const StreamNode* stream_node);

    static std::string prefetch_type_to_string(const PrefetchType& prefetch_type);

    // Stream node related to the stream buffer being allocated.
    const StreamNode* m_stream_node;
};

}  // namespace pipegen2