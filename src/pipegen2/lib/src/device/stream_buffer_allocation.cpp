// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/stream_buffer_allocation.h"

namespace pipegen2
{ 


StreamBufferAllocation::~StreamBufferAllocation() = default;

std::string StreamBufferAllocation::allocation_name() const 
{
    std::stringstream string_stream;
    string_stream << "buffer_";
    string_stream << stream_type_to_string(m_stream_node);
    if (m_stream_node->get_stream_type() == StreamType::Unpacker)
    {
        string_stream << "_" << m_stream_node->get_operand_id();
    }
    return string_stream.str();
}

std::string StreamBufferAllocation::allocation_info() const 
{
    std::stringstream string_stream;
    string_stream << "\t\t" << allocation_name() << ":\n";
    if (m_stream_node->is_ncrisc_reader_or_writer())
    {
        const NcriscConfig& ncrisc_config = m_stream_node->get_ncrisc_configs()[0];
        if (ncrisc_config.prefetch_type.has_value())
        {
            string_stream << "\t\t\tprefetch_type: " 
                        << prefetch_type_to_string(ncrisc_config.prefetch_type.value()) 
                        << "\n";
        }
    }
    string_stream << "\t\t\tbuffer_size: " << get_size() << "\n";

    return string_stream.str();
}

std::string StreamBufferAllocation::stream_type_to_string(const StreamNode* stream_node)
{
    if (stream_node->is_ncrisc_reader_or_writer())
    {
        const NcriscConfig& ncrisc_config = stream_node->get_ncrisc_configs()[0];
        if (ncrisc_config.dram_io.has_value())
        {
            return ncrisc_config.dram_io.value() ? "dram_io" : "prefetch";
        }
    }
    
    return StreamNode::stream_type_to_string(stream_node->get_stream_type());
}

std::string StreamBufferAllocation::prefetch_type_to_string(const PrefetchType& prefetch_type)
{
    switch (prefetch_type) 
    {
        case PrefetchType::PRE_TM:
            return "PRE_TM";
        case PrefetchType::POST_TM:
            return "POST_TM";
        default:
            return "Unknown";
    }
}

} //namespace pipegen2