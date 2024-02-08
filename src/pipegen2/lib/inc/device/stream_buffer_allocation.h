#pragma once

#include "device/l1_memory_allocation.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{

// Represents an allocation of the stream buffer in L1 memory of a core.
class StreamBufferAllocation : public L1MemoryAllocation
{
public:
    StreamBufferAllocation(const StreamNode* stream_node, unsigned int buffer_size, unsigned int buffer_address);

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    ~StreamBufferAllocation();

    std::string allocation_info() const override;

private:
    // Stream node related to the stream buffer being allocated.
    const StreamNode* m_stream_node;
};

} // namespace pipegen2