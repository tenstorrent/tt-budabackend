// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <unordered_set>
#include <vector>

#include "router/router_passes.h"

namespace router {

// This pass iterates though all the buffers and assigns them to an available operand ID
// on their local core. This is done by iterating through all core resource trackers in the
// router's cluster resource model and visiting each buffer (relay, input, output) that
// has an unassigned operand ID/stream ID. The pass assigns the buffer to the next available operand ID
// in the operand ID range for the buffer types operand ID range.
//
// In this pass, the operand ID and the stream ID mean the same thing.
//
// The following is the mapping from buffer type to operand ID range (inclusive):
// RouterBufferType::Input         -> 0 to OUTPUT_BUFFER_STREAM-1
// RouterBufferType::Output        -> OUTPUT_BUFFER_STREAM to (INTERMEDIATE_BUFFER_STREAM-1)
// RouterBufferType::Intermediate  -> INTERMEDIATE_BUFFER_STREAM
// RouterBufferType::Relay         -> RELAY_BUFFER_STREAM_FIRST to RELAY_BUFFER_STREAM_LAST-1
//
// For now the pass only visits ethernet datacopy input and output buffers and assigns them to operand IDs
void resolve_buffer_stream_ids(router::Router &router) {
    for (chip_id_t chip : router.get_cluster_graph().cluster_description().get_all_chips()) {
        for (const auto &[core_xy, core_descriptor] : router.get_soc_descriptor(chip).cores) {
            const auto &core_resource_tracker =
                router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core_xy));

            auto assign_to_operand_ids_in_range_if_unassigned = [](Router &router,
                                                                   const std::vector<unique_id_t> &buffer_ids,
                                                                   int range_start_inclusive,
                                                                   int range_end_exclusive) {
                std::unordered_set<int> used_operand_ids;
                for (auto &buffer_id : buffer_ids) {
                    const auto &buffer = router.get_buffer(buffer_id);
                    if (buffer.info().stream_id() != -1) {
                        used_operand_ids.insert(buffer.info().stream_id());
                    }
                }

                int next_operand_id = range_start_inclusive;
                for (auto &buffer_id : buffer_ids) {
                    auto &buffer = router.get_buffer(buffer_id);
                    if (buffer.info().stream_id() == -1) {
                        log_debug(tt::LogRouter, "Buffer {} is unassigned to a stream.", buffer_id);
                        while (used_operand_ids.find(next_operand_id) != used_operand_ids.end()) {
                            next_operand_id++;
                        }
                        TT_ASSERT(next_operand_id < range_end_exclusive);
                        router.assign_buffer_stream_id(buffer_id, next_operand_id);
                        used_operand_ids.insert(next_operand_id);
                        next_operand_id++;

                        log_debug(
                            tt::LogRouter, "Assigned buffer {} to stream {}.", buffer_id, buffer.info().stream_id());
                        // Also assign all scatter buffers to the same stream ID
                    }
                }
            };

            assign_to_operand_ids_in_range_if_unassigned(
                router, core_resource_tracker.get_input_buffers(), 0, OUTPUT_BUFFER_STREAM_START);
            assign_to_operand_ids_in_range_if_unassigned(
                router,
                core_resource_tracker.get_output_buffers(),
                OUTPUT_BUFFER_STREAM_START,
                INTERMEDIATE_BUFFER_STREAM);
            assign_to_operand_ids_in_range_if_unassigned(
                router,
                core_resource_tracker.get_intermediate_buffers(),
                INTERMEDIATE_BUFFER_STREAM,
                INTERMEDIATE_BUFFER_STREAM + 1);
            assign_to_operand_ids_in_range_if_unassigned(
                router, core_resource_tracker.get_relay_buffers(), RELAY_BUFFER_STREAM_FIRST, RELAY_BUFFER_STREAM_LAST);
        }
    }

    // Iterate through all buffers and for scatter buffers that aren't the base ID, assign them tot the same stream ID
    // as the base ID if they don't have one already
    for (auto &[id, buffer] : router.get_buffer_map()) {
        if (!buffer.is_queue() && router.is_buffer_scatter(id)) {
            if (buffer.info().stream_id() == -1) {
                unique_id_t base_id = router.get_scatter_buffer_base_id(id);
                TT_ASSERT(router.is_buffer_scatter(id) && base_id != id);
                auto &base_buffer = router.get_buffer(base_id);
                router.assign_buffer_stream_id(id, base_buffer.info().stream_id());
            }
        }
    }
}

};  // namespace router
