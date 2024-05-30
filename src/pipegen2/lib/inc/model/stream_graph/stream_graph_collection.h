// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <unordered_set>
#include <vector>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/l1/l1_buffer.h"
#include "stream_graph.h"
// clang-format on

namespace pipegen2
{

// TODO: Move this to /common or somewhere else in BBE
struct L1BufferAllocationInfo
{
    L1BufferAllocationInfo() = default;

    L1BufferAllocationInfo(const L1Buffer* buffer) :
        address(buffer->get_address()), size(buffer->get_size()), name(buffer->get_name())
    {
    }

    unsigned int address;
    unsigned int size;
    std::string name;
};

class StreamGraphCollection

{
public:
    StreamGraphCollection() = default;

    // Adds stream graph to the collection. The collection takes ownership of the stream graph.
    void add_stream_graph(std::unique_ptr<StreamGraph>&& stream_graph);

    // Keep track of L1 memory allocations for NCRISC fallback buffers.
    void add_ncrisc_fallback_buffer_allocation(
        const tt_cxy_pair core_location, L1BufferAllocationInfo ncrisc_fallback_buffer_allocation);

    // Returns all stream graphs in the collection.
    const std::vector<std::unique_ptr<StreamGraph>>& get_stream_graphs() const { return m_stream_graphs; }

    // Returns all streams in all the stream graphs in the collection.
    const std::vector<StreamNode*>& get_streams() const { return m_streams; }

    // Returns all streams that are on the same core as the provided core location.
    std::vector<const StreamNode*> get_streams_on_core(const tt_cxy_pair& core_location) const;

    // Get physical locations for all streams in the StreamGraphCollection.
    std::unordered_set<tt_cxy_pair> get_physical_locations() const;

    // Returns NCRISC fallback buffers allocations per core.
    const std::map<tt_cxy_pair, L1BufferAllocationInfo>& get_ncrisc_fallback_buffers_allocations_per_core() const
    {
        return m_ncrisc_fallback_buffer_allocation_per_core;
    }

private:
    // All stream graphs in the collection.
    std::vector<std::unique_ptr<StreamGraph>> m_stream_graphs;

    // Mapping memory allocations of NCRISC fallback buffers to cores. Kept sorted by core location in order to have
    // deterministic output.
    std::map<tt_cxy_pair, L1BufferAllocationInfo> m_ncrisc_fallback_buffer_allocation_per_core;

    // All streams in all the stream graphs in the collection.
    std::vector<StreamNode*> m_streams;
};
}  // namespace pipegen2