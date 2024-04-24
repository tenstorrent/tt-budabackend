// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/resource_manager.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2 {

// Allocates resources for every stream in stream graph.
class StreamResourcesAllocator {
   public:
    StreamResourcesAllocator(const ResourceManager* resource_manager);

    // Allocates resources for all generated streams.
    void allocate_resources(StreamGraphCollection* stream_graph_collection) const;

   private:
    // Assigns stream IDs to all streams in collection.
    void allocate_stream_ids(const StreamGraphCollection* stream_graph_collection) const;

    // Allocates memory in L1 for tile headers for all streams in collection.
    void allocate_tile_header_buffers(const StreamGraphCollection* stream_graph_collection) const;

    // Allocates memory in L1 for stream buffers for all streams in collection.
    void allocate_stream_buffers(const StreamGraphCollection* stream_graph_collection) const;

    // Allocates additional memory in L1 for cores on which NCRISC needs more space to handle reads or writes than it
    // has dedicated in its L0.
    void allocate_ncrisc_fallback_buffers(StreamGraphCollection* stream_graph_collection) const;

    // Instance of resource manager through which allocations are made.
    const ResourceManager* m_resource_manager;
};

}  // namespace pipegen2