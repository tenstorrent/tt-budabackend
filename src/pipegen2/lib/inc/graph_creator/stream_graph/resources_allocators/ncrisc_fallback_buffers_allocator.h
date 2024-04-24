// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/resource_manager.h"
#include "device/tt_xy_pair.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2 {

// Allocates additional memory in L1 for cores on which NCRISC needs more space to handle reads or writes than it has
// dedicated in its L0. After reaching core_resources_constants::max_num_active_buffers_accessed_through_l0 buffers
// which are handled through structs located in NCRISC's L0, firmware will handle additional buffers through structs
// located in buffer in L1. For each 4KB of space we provide in L1, additional 4*1024 / sizeof(dram_q_state_t) ~ 113
// buffers can be accommodated.
class NcriscFallbackBuffersAllocator {
   public:
    NcriscFallbackBuffersAllocator(const ResourceManager* resource_manager);

    // Finds cores that exceed the limit and allocates buffer in L1 on them for NCRISC to use.
    void allocate_ncrisc_fallback_buffers(StreamGraphCollection* stream_graph_collection) const;

   private:
    // Returns number of active queues NCRISC configs access from core through all streams allocated on it.
    std::unordered_map<tt_cxy_pair, unsigned int> get_number_of_active_dram_queues_accessed_per_core(
        const StreamGraphCollection* stream_graph_collection) const;

    // Calculates additional space in L1 that needs to be allocated on each core that breaches limit.
    static std::unordered_map<tt_cxy_pair, unsigned int> calculate_fallback_buffer_sizes_per_core(
        const std::unordered_map<tt_cxy_pair, unsigned int>& num_active_buffers_per_core);

    // Allocates additional space in L1 for fallback buffer on cores and keeps track of these allocations in
    // collection of stream graphs.
    void allocate_ncrisc_fallback_buffers_on_cores(
        const std::unordered_map<tt_cxy_pair, unsigned int>& fallback_buffer_size_per_core,
        StreamGraphCollection* stream_graph_collection) const;

    // Resource manager instance.
    const ResourceManager* m_resource_manager;
};

}  // namespace pipegen2