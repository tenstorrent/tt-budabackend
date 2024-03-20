// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_buffers_allocator.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model/stream_graph/stream_config.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    StreamBuffersAllocator::StreamBuffersAllocator(ResourceManager* resource_manager) :
        m_resource_manager(resource_manager)
    {
    }

    StreamBuffersAllocator::~StreamBuffersAllocator()
    {
    }

    void StreamBuffersAllocator::allocate_stream_buffers(const StreamGraphCollection* stream_graph_collection)
    {
        // Buffers of unpacker streams which are fed by multicast stream need to have aligned locations in memory.
        // TODO: Find out and document here why exactly.
        // Allocating memory for such streams first to make sure that is acomplished.
        const std::vector<StreamNode*>& all_streams = stream_graph_collection->get_streams();

        allocate_extra_tile_header_buffers(all_streams);

        const std::vector<StreamNode*> sorted_multicast_destinations = get_sorted_multicast_destinations(all_streams);

        for (StreamNode* multicast_destination: sorted_multicast_destinations)
        {
            allocate_stream_buffer(multicast_destination);
        }

        // Now, allocating memory for rest of the streams.
        for (StreamNode* stream_node: all_streams)
        {
            // If the stream has local_sources_connected flag set, that means it is operating in pointer passing mode,
            // i.e. it receives a pointer to data buffer of the input stream, which means that there is no need to
            // allocate an extra buffer, since this stream will always use the input stream's buffer.
            if (!stream_node->get_base_config().get_local_sources_connected().value_or(false))
            {
                allocate_stream_buffer(stream_node);
            }
        }

        // Check if the buffers took more memory then there is available in L1.
        for (const tt_cxy_pair& physical_locations : stream_graph_collection->get_physical_locations())
        {
            m_resource_manager->check_if_out_of_l1_data_buffers_memory(physical_locations);
        }
    }

    void StreamBuffersAllocator::allocate_extra_tile_header_buffers(const std::vector<StreamNode*>& all_streams)
    {
        std::unordered_map<tt_cxy_pair, std::unordered_set<unsigned int>> core_to_msg_sizes;
        for (StreamNode* stream_node: all_streams)
        {
            core_to_msg_sizes[stream_node->get_physical_location()].insert(
                stream_node->get_base_config().get_msg_size().value());
        }

        m_resource_manager->allocate_l1_extra_tile_headers_space(core_to_msg_sizes);
    }

    void StreamBuffersAllocator::allocate_stream_buffer(StreamNode* stream_node)
    {
        // Skip allocating buffer for this stream if it has already been assigned a buffer address, because:
        // 1) It is a multicast destination and therefore has been processed before the rest of the streams,
        // 2) It is a part of a packer fork group and was allocated the address when the first stream from that group
        //    was processed.
        // 3) It shares buffer with another stream and was allocated the address when the other stream was processed.
        if (stream_node->has_assigned_buffer_address())
        {
            return;
        }

        const unsigned int buffer_address = get_allocated_stream_buffers_address(stream_node);

        set_buffer_address(stream_node, buffer_address);
        if (stream_node->is_sharing_buffer())
        {
            set_buffer_address(stream_node->get_base_config().get_space_shared_with_stream().value(), buffer_address);
        }
    }

    void StreamBuffersAllocator::set_buffer_address(StreamNode* stream_node, const unsigned int buffer_address)
    {
        if (stream_node->get_base_config().get_fork_streams().has_value())
        {
            for (StreamNode* fork_stream : stream_node->get_base_config().get_fork_streams().value())
            {
                fork_stream->set_buffer_address(buffer_address);
            }
        }
        else
        {
            stream_node->set_buffer_address(buffer_address);
        }
    }

    unsigned int StreamBuffersAllocator::get_allocated_stream_buffers_address(StreamNode* stream_node)
    {
        StreamConfig& base_config = stream_node->get_base_config();
        const unsigned int buffer_size = base_config.get_buf_full_size_bytes().has_value() ?
                                         base_config.get_buf_full_size_bytes().value() :
                                         base_config.get_buffer_size().value();

        const unsigned int buffer_address = base_config.get_is_reading_from_padding_table().value_or(false) ?
            get_prologue_buffer_address_for_padding_table(stream_node, buffer_size) :
            m_resource_manager->allocate_core_l1_stream_buffer(stream_node, buffer_size);

        return buffer_address;
    }

    std::vector<StreamNode*> StreamBuffersAllocator::get_sorted_multicast_destinations(
        const std::vector<StreamNode*>& all_streams)
    {
        // Find all multicast destinations and keep them sorted by operand ID in increasing order. If the two streams
        // have the same operand ID, we sort them by their buffer ID.
        auto increasing_operand_id_comp = [](const StreamNode* left, const StreamNode* right)
        {
            if (left->get_operand_id() != right->get_operand_id())
            {
                return left->get_operand_id() < right->get_operand_id();
            }

            return left->get_base_config().get_buf_id().value_or(0) <= right->get_base_config().get_buf_id().value_or(0);
        };
        std::set<StreamNode*, decltype(increasing_operand_id_comp)> sorted_multicast_destinations_set(
            increasing_operand_id_comp);

        for (StreamNode* stream_node: all_streams)
        {
            if (!stream_node->is_multicast_stream())
            {
                continue;
            }

            for (const PhaseConfig& phase_config: stream_node->get_phase_configs())
            {
                log_assert(phase_config.config.get_dest().has_value(),
                           "Expecting multicast stream to have dest streams configured in all the phase configs");

                for (StreamNode* multicast_destination: phase_config.config.get_dest().value())
                {
                    sorted_multicast_destinations_set.insert(multicast_destination);
                }
            }
        }

        return std::vector<StreamNode*>(std::make_move_iterator(sorted_multicast_destinations_set.begin()),
                                        std::make_move_iterator(sorted_multicast_destinations_set.end()));
    }

    unsigned int StreamBuffersAllocator::get_prologue_buffer_address_for_padding_table(StreamNode* stream_node,
                                                                                       const unsigned int buffer_size)
    {
        const StreamConfig& base_stream_config = stream_node->get_base_config();
        log_assert(base_stream_config.get_dram_buf_noc_addr().has_value(),
                   "Stream reading from padding table is expected to have dram_buf_noc_addr field set");

        const tt_cxy_pair& core_physical_location = stream_node->get_physical_location();

        const SoCInfo* soc_info = m_resource_manager->get_soc_info();
        const std::uint64_t local_dram_buf_noc_address = soc_info->get_local_dram_buffer_noc_address(
            base_stream_config.get_dram_buf_noc_addr().value());

        WorkerCorePaddingTable* worker_core_padding_table = get_worker_core_padding_table(core_physical_location);

        if (!worker_core_padding_table->has_allocated_buffer(buffer_size, local_dram_buf_noc_address))
        {
            unsigned int buffer_address = m_resource_manager->allocate_core_l1_stream_buffer(stream_node, buffer_size);

            worker_core_padding_table->set_allocated_buffer_address(
                buffer_size, local_dram_buf_noc_address, buffer_address);
        }

        return worker_core_padding_table->get_allocated_buffer_address(buffer_size, local_dram_buf_noc_address);
    }

    WorkerCorePaddingTable* StreamBuffersAllocator::get_worker_core_padding_table(
        const tt_cxy_pair& core_physical_location)
    {
        auto worker_core_padding_table_it = m_worker_core_padding_table.find(core_physical_location);
        if (worker_core_padding_table_it == m_worker_core_padding_table.end())
        {
            worker_core_padding_table_it = m_worker_core_padding_table.emplace(
                core_physical_location, std::make_unique<WorkerCorePaddingTable>()).first;
        }

        return worker_core_padding_table_it->second.get();
    }

    bool WorkerCorePaddingTable::has_allocated_buffer(const unsigned int buffer_size,
                                                      const std::uint64_t dram_buf_noc_addr) const
    {
        const auto noc_address_to_padding_buffer_it = m_allocated_padding_buffers.find(buffer_size);
        if (noc_address_to_padding_buffer_it == m_allocated_padding_buffers.end())
        {
            return false;
        }

        return noc_address_to_padding_buffer_it->second.find(dram_buf_noc_addr) !=
               noc_address_to_padding_buffer_it->second.end();
    }

    void WorkerCorePaddingTable::set_allocated_buffer_address(const unsigned int buffer_size,
                                                              const std::uint64_t dram_buf_noc_addr,
                                                              const unsigned int allocated_padding_buffer_address)
    {
        m_allocated_padding_buffers[buffer_size][dram_buf_noc_addr] = allocated_padding_buffer_address;
    }

    unsigned int WorkerCorePaddingTable::get_allocated_buffer_address(const unsigned int buffer_size,
                                                                      const std::uint64_t dram_buf_noc_addr) const
    {
        log_assert(has_allocated_buffer(buffer_size, dram_buf_noc_addr),
                   "Padding buffer for a given buffer size and dram buf noc addr has not been allocated");

        return m_allocated_padding_buffers.at(buffer_size).at(dram_buf_noc_addr);
    }

}