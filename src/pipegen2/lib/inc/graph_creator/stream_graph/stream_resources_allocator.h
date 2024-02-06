// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "device/resource_manager.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{
    class StreamBuffersAllocator;
    class StreamIDsAllocator;

    // Allocates resources (ID and stream buffer) for every stream in a stream graph.
    class StreamResourcesAllocator
    {
    public:
        StreamResourcesAllocator(ResourceManager* resource_manager);

        // Destructor, necessary for forward declarations of classes in smart pointer members.
        ~StreamResourcesAllocator();

        // Allocates resources for all generated streams.
        void allocate_resources(const StreamGraphCollection* stream_graph_collection);

    private:
        // Stream buffers allocator instance.
        std::unique_ptr<StreamBuffersAllocator> m_stream_buffers_allocator;

        // Strea IDs allocator instance.
        std::unique_ptr<StreamIDsAllocator> m_stream_ids_allocator;
    };
}