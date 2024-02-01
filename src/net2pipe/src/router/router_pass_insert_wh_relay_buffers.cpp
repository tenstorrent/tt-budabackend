// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router.hpp"
#include "common/size_lib.hpp"
#include "router/router_passes_common.h"
#include "src/net2pipe/inc/router_types.h"

#include <algorithm>
#include <numeric>

namespace router {

namespace logging {
void log_candidate_core_rejection_reasons(const tt_cxy_pair &location, const HwCoreAttributes &core, int buffer_size, int num_required_extra_streams, bool output_pipe_is_multicast) {
    auto ss = std::stringstream();
    if (!core.has_available_input_from_dram_slot()) {
        ss << "| no available input from dram streams |";
    }
    if (!core.has_available_relay_buffer_slot()) {
        ss << "| no available relay buffer slots |";
    }
    if (!core.can_allocate_bytes(buffer_size)) {
        ss << "| insufficient l1 available |";
    }
    if (output_pipe_is_multicast && !core.has_available_multicast_stream_slots()) {
        ss << "| insufficient multicast streams |";
    }
    if (!core.has_extra_streams_available(num_required_extra_streams)) {
        ss << "| Insufficient extra streams available required " << num_required_extra_streams << ", had " << core.number_of_available_extra_streams() << "|";
    }
    log_debug(tt::LogRouter, "Core (y={},x={}) rejected for reasons: {}.", location.y, location.x, ss.str());
}

};

tt_cxy_pair select_dram_subchan_core_with_least_buffers(router::Router &router, unique_id_t dram_buffer_id) {
    auto const& dram_queue_buffer = router.get_buffer(dram_buffer_id);
    int channel = dram_queue_buffer.queue_info().get_allocation_info().channel;
    chip_id_t chip = dram_queue_buffer.chip_location();
    const auto &valid_subchan_cores = router.get_soc_descriptor(chip).dram_cores.at(channel);

    auto compare_buffer_count = [&router, &chip](const tt_xy_pair &core1, const tt_xy_pair &core2) -> bool {
        return router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core1)).get_buffer_count() <
               router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core2)).get_buffer_count();
    };
    const auto least_used_dram_subchan_core =
        *std::min_element(valid_subchan_cores.begin(), valid_subchan_cores.end(), compare_buffer_count);

    return tt_cxy_pair(chip, least_used_dram_subchan_core);
}


void assign_dram_subchan(router::Router &router) {
    // Note: this choice may be overridden by the final router_pass_op_queue_routing. 
    // In particular, it ignores the read_port and write_port fields in the netlist, 
    // and treats the buffer as having a single location for all access. 

    for (const auto &[buffer_id, router_buffer_info] : router.get_buffer_map()) {
        if (router.is_queue_buffer(buffer_id)) {
            unique_id_t parent_id = router_buffer_info.queue_info().get_parent_queue_id();
            bool no_real_associated_parent_queue = parent_id == router::NO_ASSOCIATED_QUEUE_OBJECT_ID;
            if (no_real_associated_parent_queue || router.get_queue(parent_id).loc == QUEUE_LOCATION::DRAM) {
                tt_cxy_pair router_selected_dram_core;
 
                router_selected_dram_core =
                    no_real_associated_parent_queue
                        ? tt_cxy_pair(
                                router_buffer_info.chip_location(),
                                router.get_soc_descriptor(router_buffer_info.chip_location())
                                    .dram_cores.at(router_buffer_info.queue_info().get_allocation_info().channel)
                                    .at(0))
                        : select_dram_subchan_core_with_least_buffers(router, buffer_id);
                const dram_channel_t &dram_chan_subchan = router.get_dram_channels().at(router_selected_dram_core);
                log_debug(
                    tt::LogRouter,
                    "Assigning dram queue {} to dram chan {} subchan {} core {})",
                    buffer_id,
                    dram_chan_subchan.channel,
                    dram_chan_subchan.subchannel,
                    router_selected_dram_core.str());
                router.update_dram_queue_core_location(buffer_id, router_selected_dram_core);
                if (!no_real_associated_parent_queue) {
                    const auto &queue_info = router.get_queue(parent_id);
                    log_debug(
                        tt::LogRouter,
                        "   dram core buffer count: {}, buffer size in tiles: {}, bytes {}",
                        router.get_cluster_resource_model()
                            .get_core_attributes(router_selected_dram_core)
                            .get_buffer_count(),
                        get_entry_size_in_tiles(queue_info),
                        get_entry_size_in_bytes(queue_info, /*include_header_padding=*/true));
                }
            }
        }
    }

    validate_all_buffers_and_pipes_assigned_to_cores(router);
}

};  // namespace router
