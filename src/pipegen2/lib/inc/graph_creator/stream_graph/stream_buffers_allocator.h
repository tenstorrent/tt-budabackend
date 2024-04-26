// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "device/resource_manager.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{
    // Helper class for tracking allocated padding buffer per worker core. If two or more streams on the same core are
    // reading from padding buffers which have the same buffers size and the same address (because they use the same
    // padding value), then only one buffer should be allocated on that core on which the padding buffer will be
    // prologued, and from which all the streams can read.
    class WorkerCorePaddingTable
    {
    public:
        // Checks if there is already a padding buffer allocated for a given buffer size and noc address.
        bool has_allocated_buffer(const unsigned int buffer_size, const std::uint64_t dram_buf_noc_addr) const;

        // Sets buffer adress of the allocated padding buffers so that all next padding buffers with the same size
        // and the same noc address can use that buffer on the worker core.
        void set_allocated_buffer_address(const unsigned int buffer_size,
                                          const std::uint64_t dram_buf_noc_addr,
                                          const unsigned int allocated_padding_buffer_address);

        // Returns the buffer address of a padding buffer of specific size and noc address.
        unsigned int get_allocated_buffer_address(const unsigned int buffer_size,
                                                  const std::uint64_t dram_buf_noc_addr) const;

    private:
        // Mapping between padding buffer size in bytes and the local address bits of the NOC address (lower 35 bits)
        // and the address of the allocated padding buffer.
        std::unordered_map<unsigned int, std::unordered_map<std::uint64_t, unsigned int>> m_allocated_padding_buffers;
    };

    // Implements Union Find Disjoint Set data structure for finding multicast groups. Each multicast destination core
    // is a node in the graph. If two cores are in the same multicast group, then they are connected with an edge. If
    // two cores from different multicast groups are connected, then the two groups are merged into one.
    class MulticastGroupFinder
    {
    public:
        // Returns a vector of all the merged multicast groups. Each multicast group is a set of cores.
        std::vector<std::unordered_set<tt_cxy_pair>> find_grouped_cores();

        // Unites two cores into the same multicast group.
        void unite_cores(const tt_cxy_pair& core1, const tt_cxy_pair& core2);

    private:
        // Finds the parent core of a given core in the union find disjoint set.
        tt_cxy_pair find_parent_core(const tt_cxy_pair& core);

        // Mapping between a core and its parent core in the union find disjoint set.
        std::unordered_map<tt_cxy_pair, tt_cxy_pair> m_core_to_parent_core;
    };

    // Allocates a buffer for every streams which requires a buffer, configures the stream buf_addr field with the
    // address of the allocatead buffer and updates all the streams in the fork group to have the same buffer address.
    class StreamBuffersAllocator
    {
    public:
        StreamBuffersAllocator(ResourceManager* resource_manager);

        // Destructor, necessary for forward declarations of classes in smart pointer members.
        ~StreamBuffersAllocator();

        // Allocates L1 memory for all stream buffers.
        void allocate_stream_buffers(const StreamGraphCollection* stream_graph_collection);

    private:
        // Allocates L1 memory for tile headers of all streams. Each unique tile header (based on its size) gets its own
        // buffer. If two or more streams have the same tile header size, they will share the same buffer. Multicast
        // destinations must have their tile headers in the same order.
        void allocate_extra_tile_header_buffers(const std::vector<StreamNode*>& all_streams);

        // Assigns tile header buffer address to all streams.
        void assign_tile_header_buffer_address(const std::vector<StreamNode*>& all_streams);

        // Uses MulticastGroupFinder to return a set of all the multicast groups.
        std::vector<std::unordered_set<tt_cxy_pair>> find_multicast_groups(const std::vector<StreamNode*>& all_streams);

        // Allocates L1 memory for given stream buffer.
        void allocate_stream_buffer(StreamNode* stream_node);

        // Sets the buffer address for a given stream including its fork group.
        void set_buffer_address(StreamNode* stream_node, const unsigned int buffer_address);

        // Returns the address of the allocated buffer for a given stream.
        unsigned int get_allocated_stream_buffers_address(StreamNode* stream_node);

        // Returns a list of all the multicast destination streams sorted by operand ID in ascending order.
        std::vector<StreamNode*> get_sorted_multicast_destinations(const std::vector<StreamNode*>& all_streams);

        // Returns address of the prologue buffer for a padding table which the given stream reads from. If the buffer
        // has already been allocated, simply returns it's address.
        unsigned int get_prologue_buffer_address_for_padding_table(StreamNode* stream_node,
                                                                   const unsigned int buffer_size);

        // Returns the padding table for a given worker core.
        WorkerCorePaddingTable* get_worker_core_padding_table(const tt_cxy_pair& core_physical_location);

        // Resource manager instance.
        ResourceManager* m_resource_manager;

        // Mapping of allocated padding buffers per worker core.
        std::unordered_map<tt_cxy_pair, std::unique_ptr<WorkerCorePaddingTable>> m_worker_core_padding_table;
    };
}