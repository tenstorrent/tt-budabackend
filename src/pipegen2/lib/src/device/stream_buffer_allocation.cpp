#include "device/stream_buffer_allocation.h"

namespace pipegen2
{ 

StreamBufferAllocation::StreamBufferAllocation(const StreamNode* stream_node, 
                                               unsigned int buffer_size, 
                                               unsigned int buffer_address) :
    L1MemoryAllocation(buffer_size, buffer_address), 
    m_stream_node(stream_node) 
{        
}

StreamBufferAllocation::~StreamBufferAllocation()
{
}

std::string StreamBufferAllocation::allocation_info() const 
{
    std::stringstream string_stream;
    string_stream << "Stream " << m_stream_node->get_stream_id() << ":\n";
    string_stream << "\ttype: " << stream_type_to_string(m_stream_node->get_stream_type()) << "\n";
    string_stream << "\tbuffer_size: " << get_size() << "\n";

    return string_stream.str();
}

} //namespace pipegen2