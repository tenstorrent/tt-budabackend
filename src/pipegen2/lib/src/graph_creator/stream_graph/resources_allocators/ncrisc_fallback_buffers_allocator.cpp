// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/resources_allocators/ncrisc_fallback_buffers_allocator.h"

#include "device/core_resources_constants.h"
#include "device/l1/l1_buffer.h"
#include "device/tt_xy_pair.h"
#include "logger.hpp"
#include "model/stream_graph/stream_graph_collection.h"
#include "pipegen2_exceptions.h"

namespace pipegen2 {

NcriscFallbackBuffersAllocator::NcriscFallbackBuffersAllocator(const ResourceManager* resource_manager)
    : m_resource_manager(resource_manager) {}

void NcriscFallbackBuffersAllocator::allocate_ncrisc_fallback_buffers(
    StreamGraphCollection* stream_graph_collection) const {
    std::unordered_map<tt_cxy_pair, unsigned int> num_active_buffers_per_core =
        get_number_of_active_dram_queues_accessed_per_core(stream_graph_collection);

    std::unordered_map<tt_cxy_pair, unsigned int> buffer_size_per_core =
        calculate_fallback_buffer_sizes_per_core(num_active_buffers_per_core);

    allocate_ncrisc_fallback_buffers_on_cores(buffer_size_per_core, stream_graph_collection);
}

std::unordered_map<tt_cxy_pair, unsigned int>
NcriscFallbackBuffersAllocator::get_number_of_active_dram_queues_accessed_per_core(
    const StreamGraphCollection* stream_graph_collection) const {
    std::unordered_map<tt_cxy_pair, unsigned int> num_active_buffers_per_core;

    for (const tt_cxy_pair& core_location : stream_graph_collection->get_physical_locations()) {
        for (const StreamNode* stream : stream_graph_collection->get_streams_on_core(core_location)) {
            num_active_buffers_per_core[core_location] += stream->get_num_active_buffers_accessed_through_ncrisc();
        }
    }

    return num_active_buffers_per_core;
}

std::unordered_map<tt_cxy_pair, unsigned int> NcriscFallbackBuffersAllocator::calculate_fallback_buffer_sizes_per_core(
    const std::unordered_map<tt_cxy_pair, unsigned int>& num_active_buffers_per_core) {
    std::unordered_map<tt_cxy_pair, unsigned int> fallback_buffer_size_per_core;

    for (const auto& [core_location, num_active_buffers_accessed] : num_active_buffers_per_core) {
        // Do nothing for cores that satisfy limitations.
        if (num_active_buffers_accessed <= core_resources_constants::max_num_active_buffers_accessed_through_l0) {
            continue;
        }

        // Buffer needs to be large enough to accommodate however many DRAM or PCIe buffers are left unhandled by
        // NCRISC FW through structs in L0.
        // TODO note that this will take up exact amount of space we need in L1. This might present a low level perf
        // issue with CPU reading from unaligned (in KBs) addresses. If we notice that, we should round this size up.
        const unsigned int fallback_buffer_size =
            (num_active_buffers_accessed - core_resources_constants::max_num_active_buffers_accessed_through_l0) *
            core_resources_constants::ncrisc_buffer_tracking_struct_size;

        fallback_buffer_size_per_core[core_location] = fallback_buffer_size;
    }

    return fallback_buffer_size_per_core;
}

void NcriscFallbackBuffersAllocator::allocate_ncrisc_fallback_buffers_on_cores(
    const std::unordered_map<tt_cxy_pair, unsigned int>& fallback_buffer_size_per_core,
    StreamGraphCollection* stream_graph_collection) const {
    for (const auto& [core_location, fallback_buffer_size] : fallback_buffer_size_per_core) {
        log_debug(tt::LogPipegen2,
                  "Core {} exceeded maximum number ({}) of active DRAM or PCIe buffers that NCRISC can handle through "
                  "structures in L0. Using additional {} bytes of space in L1 to handle remaining buffers.",
                  core_location.str(), core_resources_constants::max_num_active_buffers_accessed_through_l0,
                  fallback_buffer_size);

        const L1Buffer* fallback_buffer_allocation =
            m_resource_manager->allocate_l1_ncrisc_fallback_buffer(core_location, fallback_buffer_size);

        // Keep track of allocated buffer in collection to pass it down to firmware through blobgen.
        stream_graph_collection->add_ncrisc_fallback_buffer_allocation(core_location, fallback_buffer_allocation);
    }

    // Check if the buffers took more memory then there is available in L1. This check is done after all allocations are
    // finished in order to provide full allocation info per core in case exception is thrown.
    for (const tt_cxy_pair& physical_location : stream_graph_collection->get_physical_locations()) {
        m_resource_manager->check_if_out_of_l1_data_buffers_memory(physical_location);
    }
}

}  // namespace pipegen2