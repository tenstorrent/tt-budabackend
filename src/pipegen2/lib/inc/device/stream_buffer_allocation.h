// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/l1_memory_allocation.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{

// Represents an allocation of the stream buffer in L1 memory of a core.
class StreamBufferAllocation : public L1MemoryAllocation
{
public:
    StreamBufferAllocation(const StreamNode* stream_node, 
                           const unsigned int buffer_size, 
                           const unsigned int buffer_address) : 
        L1MemoryAllocation(buffer_size, buffer_address), 
        m_stream_node(stream_node) 
    {
    } 

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    virtual ~StreamBufferAllocation();

    // Returns a string representation of the stream buffer allocation.
    std::string allocation_info() const override;

private:
    // Stream node related to the stream buffer being allocated.
    const StreamNode* m_stream_node;

    static std::string stream_type_to_string(const StreamNode* stream_node);

    static std::string prefetch_type_to_string(const PrefetchType& prefetch_type);
};

} // namespace pipegen2