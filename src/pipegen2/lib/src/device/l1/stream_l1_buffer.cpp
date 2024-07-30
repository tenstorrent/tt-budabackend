// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/l1/stream_l1_buffer.h"

namespace pipegen2
{

std::string StreamL1Buffer::get_name() const { return stream_type_to_string(m_stream_node) + " stream buffer"; }

std::string StreamL1Buffer::get_allocation_info() const
{
    std::stringstream string_stream;
    string_stream << "\t\tType: " << get_name() << "\n";

    if (m_stream_node->get_stream_type() == StreamType::Unpacker)
    {
        string_stream << "\t\t\tOperand ID: " << m_stream_node->get_operand_id() << "\n";
    }

    if (m_stream_node->is_ncrisc_reader_or_writer())
    {
        const NcriscConfig& ncrisc_config = m_stream_node->get_ncrisc_configs()[0];
        if (ncrisc_config.prefetch_type.has_value())
        {
            string_stream << "\t\t\tPrefetch type: " << prefetch_type_to_string(ncrisc_config.prefetch_type.value())
                          << "\n";
        }
    }

    string_stream << "\t\t\tBuffer size: " << get_size() << "\n"
                  << "\t\t\tBuffer address: " << get_address();

    return string_stream.str();
}

std::string StreamL1Buffer::stream_type_to_string(const StreamNode* stream_node)
{
    if (stream_node->is_ncrisc_reader_or_writer())
    {
        const NcriscConfig& ncrisc_config = stream_node->get_ncrisc_configs()[0];
        if (ncrisc_config.dram_io.has_value())
        {
            return ncrisc_config.dram_io.value() ? "DRAM IO" : "Prefetch";
        }
    }

    return StreamNode::stream_type_to_string(stream_node->get_stream_type());
}

std::string StreamL1Buffer::prefetch_type_to_string(const PrefetchType& prefetch_type)
{
    switch (prefetch_type)
    {
        case PrefetchType::PRE_TM:
            return "PRE TM";
        case PrefetchType::POST_TM:
            return "POST TM";
        default:
            return "Unknown";
    }
}

}  // namespace pipegen2