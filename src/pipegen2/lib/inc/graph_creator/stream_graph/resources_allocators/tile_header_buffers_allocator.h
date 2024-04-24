// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/resource_manager.h"
#include "device/tt_xy_pair.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2 {

// Implements Union Find Disjoint Set data structure for finding multicast groups. Each multicast destination core
// is a node in the graph. If two cores are in the same multicast group, then they are connected with an edge. If
// two cores from different multicast groups are connected, then the two groups are merged into one.
class MulticastGroupFinder {
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

// Allocates L1 memory for tile headers of all streams.
class TileHeaderBuffersAllocator {
   public:
    TileHeaderBuffersAllocator(const ResourceManager* resource_manager);

    // Allocates tile header buffers and assigns their addressses to all streams.
    void allocate_tile_header_buffers(const StreamGraphCollection* stream_graph_collection) const;

   private:
    // Allocates buffer for each unique tile header (based on its size). If two or more streams have the same tile
    // header size, they will share the same buffer. Multicast destinations must have their tile headers in the same
    // order.
    void allocate_extra_tile_header_buffers(const std::vector<StreamNode*>& all_streams) const;

    // Assigns tile header buffer address to all streams.
    void assign_tile_header_buffer_address(const std::vector<StreamNode*>& all_streams) const;

    // Uses MulticastGroupFinder to return a set of all the multicast groups.
    std::vector<std::unordered_set<tt_cxy_pair>> find_multicast_groups(
        const std::vector<StreamNode*>& all_streams) const;

    // Resource manager instance.
    const ResourceManager* m_resource_manager;
};

}  // namespace pipegen2