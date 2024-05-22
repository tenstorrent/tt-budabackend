// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/resources_allocators/stream_resources_allocator.h"

#include "device/resource_manager.h"
#include "graph_creator/stream_graph/resources_allocators/ncrisc_fallback_buffers_allocator.h"
#include "graph_creator/stream_graph/resources_allocators/stream_buffers_allocator.h"
#include "graph_creator/stream_graph/resources_allocators/stream_ids_allocator.h"
#include "graph_creator/stream_graph/resources_allocators/tile_header_buffers_allocator.h"

namespace pipegen2
{

StreamResourcesAllocator::StreamResourcesAllocator(const ResourceManager* resource_manager) :
    m_resource_manager(resource_manager)
{
}

void StreamResourcesAllocator::allocate_resources(StreamGraphCollection* stream_graph_collection) const
{
    allocate_stream_ids(stream_graph_collection);
    allocate_tile_header_buffers(stream_graph_collection);
    allocate_stream_buffers(stream_graph_collection);
    allocate_ncrisc_fallback_buffers(stream_graph_collection);
}

void StreamResourcesAllocator::allocate_stream_ids(const StreamGraphCollection* stream_graph_collection) const
{
    StreamIDsAllocator stream_ids_allocator(m_resource_manager);
    stream_ids_allocator.allocate_stream_ids(stream_graph_collection);
}

void StreamResourcesAllocator::allocate_tile_header_buffers(const StreamGraphCollection* stream_graph_collection) const
{
    TileHeaderBuffersAllocator tile_header_buffers_allocator(m_resource_manager);
    tile_header_buffers_allocator.allocate_tile_header_buffers(stream_graph_collection);
}

void StreamResourcesAllocator::allocate_stream_buffers(const StreamGraphCollection* stream_graph_collection) const
{
    StreamBuffersAllocator stream_buffers_allocator(m_resource_manager);
    stream_buffers_allocator.allocate_stream_buffers(stream_graph_collection);
}

void StreamResourcesAllocator::allocate_ncrisc_fallback_buffers(StreamGraphCollection* stream_graph_collection) const
{
    NcriscFallbackBuffersAllocator ncrisc_fallback_buffer_allocator(m_resource_manager);
    ncrisc_fallback_buffer_allocator.allocate_ncrisc_fallback_buffers(stream_graph_collection);
}

}  // namespace pipegen2