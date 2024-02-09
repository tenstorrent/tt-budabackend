// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_resources_allocator.h"

#include "graph_creator/stream_graph/stream_buffers_allocator.h"
#include "graph_creator/stream_graph/stream_ids_allocator.h"

namespace pipegen2
{
    StreamResourcesAllocator::StreamResourcesAllocator(ResourceManager* resource_manager)
    {
        m_stream_buffers_allocator = std::make_unique<StreamBuffersAllocator>(resource_manager);
        m_stream_ids_allocator = std::make_unique<StreamIDsAllocator>(resource_manager);
    }

    StreamResourcesAllocator::~StreamResourcesAllocator()
    {
    }

    void StreamResourcesAllocator::allocate_resources(const StreamGraphCollection* stream_graph_collection)
    {
        m_stream_ids_allocator->allocate_stream_ids(stream_graph_collection);
        m_stream_buffers_allocator->allocate_stream_buffers(stream_graph_collection);
    }
}