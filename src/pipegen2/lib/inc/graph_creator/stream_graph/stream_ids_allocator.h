// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/resource_manager.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{
    // Allocates a stream ID for every stream created based on the stream type.
    class StreamIDsAllocator
    {
    public:
        StreamIDsAllocator(ResourceManager* resource_manager);

        // Destructor, necessary for forward declarations of classes in smart pointer members.
        ~StreamIDsAllocator();

        // Assigns stream IDs from the resource manager.
        void allocate_stream_ids(const StreamGraphCollection* stream_graph_collection);

    private:
        // Allocates ID for a given packer stream, and all the other packer streams which are in the same fork group.
        void allocate_packer_stream_id(StreamNode* stream_node);

        // Allocates ID for a given relay stream based on his location and transfer type (Ethernet or NoC).
        void allocate_relay_stream_id(StreamNode* stream_node);

        // Resource manager instance.
        ResourceManager* m_resource_manager;
    };
}