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


bool has_input_or_output_scatter_buffers(const Router &router, const pipe_t &p) {
    for (auto id : p.input_buffer_ids) {
        if (router.is_buffer_scatter(id)) {
            return true;
        }
    }
    if (p.has_multiple_timesteps()) {
        for (const auto &ids : p.time_multiplexed_output_buffer_ids()) {
            for (auto id : ids) {
                if (router.is_buffer_scatter(id)) {
                    return true;
                }
            }
        }
    } else {
        for (auto id : p.output_buffer_ids()) {
            if (router.is_buffer_scatter(id)) {
                return true;
            }
        }
    }

    return false;
}

std::vector<tt_cxy_pair> collect_pipe_consumer_core_locations(Router &router, unique_id_t pipe_id) {
    auto consumer_locations = std::vector<tt_cxy_pair>{};
    const pipe_t &pipe = router.get_pipes().at(pipe_id);
    for (auto consumer_buffer_id : pipe.output_buffer_ids()) {
        const router_buffer_info_t &buffer = router.get_buffer(consumer_buffer_id);
        consumer_locations.push_back(buffer.core_location());
    }

    return consumer_locations;
}


int relay_buffer_required_extra_streams(Router &router, unique_id_t relay_buffer_input_pipe_id) {
    const auto &input_pipe = router.get_pipe(relay_buffer_input_pipe_id);
    unique_id_t relay_buffer_id = input_pipe.output_buffer_ids().at(0);
    int fork_factor = std::max<int>(1, router.get_buffer_output_pipes().at(relay_buffer_id).size());
    int mcast_factor = router.get_pipe(*router.get_buffer_output_pipes().at(relay_buffer_id).begin()).output_buffer_ids().size() - 1;
    bool input_pipe_is_gather = router.get_pipe(relay_buffer_input_pipe_id).input_buffer_ids.size() > 1;
    int extra_input_streams = requires_extra_input_streams(router, relay_buffer_input_pipe_id) ? compute_pipe_segment_gather_extra_stream_use(router, pipe_segment_id_t{.pipe_id=relay_buffer_input_pipe_id, .segment=0}): 1;
    int required_extra_streams = fork_factor + extra_input_streams + mcast_factor;
    log_debug(tt::LogRouter, "Inserting relay buffer {} requires {} extra streams. {} for relay buffer fork factor. {} for input pipe input streams. {} for mcast",
        relay_buffer_id, required_extra_streams, fork_factor, extra_input_streams, mcast_factor);
    return required_extra_streams;
}


tt_cxy_pair select_relay_buffer_core(Router &router, const std::vector<tt_cxy_pair> &candidate_cores) {
    // TODO(snijjar): pick based on some perf based metric (e.g. so we don't need to hop across nocs)
    TT_ASSERT(candidate_cores.size() > 0, "Couldn't find any valid cores to allocate relay buffer on");
    std::vector<tt_cxy_pair> sorted_cores = candidate_cores;
    std::sort(std::begin(sorted_cores), std::end(sorted_cores), [&router](const auto &core_a, const auto &core_b) { 
        return router.get_cluster_resource_model().get_core_attributes(core_a).get_relay_buffer_count() < router.get_cluster_resource_model().get_core_attributes(core_b).get_relay_buffer_count();
    });
    const auto &selected_core = sorted_cores.at(0);
    log_trace(tt::LogRouter, "Inserting relay buffer on core (c={},y={},x={})", selected_core.chip, selected_core.y, selected_core.x);
    return selected_core;
}


/* 
 * Break apart a pipe into 2 pipes with a relay buffer in the middle. This will create a pipe from
 * pipe_inputs -> *new sender pipe* -> relay buffer
 * and then
 * relay buffer -> *new receiver pipe* -> pipe outputs
 */
std::tuple<unique_id_t, unique_id_t> replace_pipe_with_pipe_relay_pipe_segment(Router &router, unique_id_t pipe_id, const pipe_t &pipe, unique_id_t relay_buffer_id) {//}, const tt_cxy_pair &relay_buffer_core) {
    const auto pipe_inputs = pipe.input_buffer_ids;
    const auto pipe_outputs = pipe.output_buffer_ids();
    router.disconnect_pipe(pipe_id);

    bool multiple_inputs = pipe_inputs.size() > 1;
    unique_id_t sender_side_pipe_id = multiple_inputs ? 
        router.create_gather_pipe_connection(pipe_inputs, relay_buffer_id/*, pipe.core_location()*/, pipe.consumer_tile_granularity) :
        router.create_unicast_pipe_connection(pipe_inputs.at(0), relay_buffer_id/*, pipe.core_location()*/, pipe.consumer_tile_granularity);

    bool multiple_outputs = pipe_outputs.size() > 1;
    unique_id_t receiver_side_pipe_id = multiple_outputs ? 
        router.create_multicast_pipe_connection(relay_buffer_id, pipe_outputs, pipe.consumer_tile_granularity) :
        router.create_unicast_pipe_connection(relay_buffer_id, pipe_outputs.at(0), pipe.consumer_tile_granularity);
    
    router.remove_pipe(pipe_id);
    return {sender_side_pipe_id, receiver_side_pipe_id};
}

void log_relay_buffer_scenario(Router &router, unique_id_t pipe_id, const std::vector<std::unordered_set<int>> &dram_channel_rows) {
    const auto &pipe = router.get_pipe(pipe_id);

    auto input_buffers_ss = std::stringstream();
    for (auto in_buffer_id : pipe.input_buffer_ids) {

        input_buffers_ss << "(" << in_buffer_id << ": ";
        const auto &buffer = router.get_buffer(in_buffer_id);
        if (buffer.is_queue()) {
            const int channel = buffer.queue_info().get_allocation_info().channel;
            input_buffers_ss << "is_q: {channel=" << channel << ", rows=";
            for (const auto row : dram_channel_rows.at(channel)) {
                input_buffers_ss << row << ",";
            }
            input_buffers_ss << "}";
        } else {
            if (buffer.core_location_assigned()) {
                input_buffers_ss << "row=" << router.get_buffer_location(in_buffer_id).y << "),";
            }
        }
    }

    auto output_buffers_ss = std::stringstream();
    if (pipe.has_multiple_timesteps()) {
        output_buffers_ss << "[";
        for (const auto &timestep_buffers : pipe.time_multiplexed_output_buffer_ids()) {
            for (auto out_buffer_id : timestep_buffers) {
                output_buffers_ss << "(" << out_buffer_id;
                const auto &out_buffer = router.get_buffer(out_buffer_id);
                if (out_buffer.core_location_assigned()) {
                    output_buffers_ss << ": row=" << router.get_buffer_location(out_buffer_id).y << "),";
                }
            }   
        }
        output_buffers_ss << "]";

    } else {
        for (auto out_buffer_id : pipe.output_buffer_ids()) {
            output_buffers_ss << "(" << out_buffer_id;
            const auto &out_buffer = router.get_buffer(out_buffer_id);
            if (out_buffer.core_location_assigned()) {
                output_buffers_ss << ": row=" << router.get_buffer_location(out_buffer_id).y << "),";
            }
        }

    }

    if (pipe.core_location_assigned()) {
        log_debug(tt::LogRouter, "Relay buffer insertion required for pipe: {}: (c={},y={},x={})", pipe_id, pipe.core_location().chip, pipe.core_location().y, pipe.core_location().x);
    } else {
        log_debug(tt::LogRouter, "Relay buffer insertion required for pipe: {}:", pipe_id);
    }
    log_debug(tt::LogRouter, "{} Input buffers: {}", pipe.input_buffer_ids.size(), input_buffers_ss.str());
    log_debug(tt::LogRouter, "Output buffers: {}", output_buffers_ss.str());
}


unique_id_t insert_relay_buffer_for_queues_on_single_dram_channel(Router &router, unique_id_t pipe_id, const std::vector<std::unordered_set<int>> &dram_channel_rows, const std::vector<tt_cxy_pair> &consumer_core_locations) {
    const auto pipe = router.get_pipes().at(pipe_id);
    bool has_scatter_buffers = has_input_or_output_scatter_buffers(router, pipe);

    const unique_id_t an_input_buffer_id = pipe.input_buffer_ids.at(0);
    const auto &an_input_buffer = router.get_buffer(an_input_buffer_id);
    DataFormat input_data_format = an_input_buffer.is_queue() ? 
        router.get_queue(an_input_buffer.queue_info().get_parent_queue_id()).data_format :
        an_input_buffer.info().data_format();

    const auto &input_buf = router.get_buffer(pipe.input_buffer_ids.at(0));
    int total_epoch_tiles = 0;
    if (pipe.has_multiple_timesteps()) {
        for (const auto &timestep_out_buffer_ids : pipe.time_multiplexed_output_buffer_ids()) {
            int epoch_tile_count = router.get_buffer_total_epoch_tiles(timestep_out_buffer_ids.at(0));
            total_epoch_tiles += epoch_tile_count;
        }
    } else {
        int epoch_tile_count = router.get_buffer_total_epoch_tiles(pipe.output_buffer_ids().at(0));
        total_epoch_tiles += epoch_tile_count;
    }

    int size_tiles = (has_scatter_buffers /*&& router.is_buffer_forked(an_input_buffer_id)*/) ? router.get_scatter_gather_granularity(an_input_buffer_id) : 1;
    const tt::buffer_info &relay_buffer_info = create_relay_buffer_info(
        size_tiles,
        router.get_scatter_gather_granularity(an_input_buffer_id),
        total_epoch_tiles,
        input_data_format,
        router.get_buffer_tile_dims(an_input_buffer_id)
    );
    unique_id_t relay_buffer_id = router.add_new_buffer(relay_buffer_info, an_input_buffer.chip_location());

    const auto &[sender_side_pipe_id, receiver_side_pipe_id] = replace_pipe_with_pipe_relay_pipe_segment(router, pipe_id, pipe, relay_buffer_id);

    log_debug(tt::LogRouter, "Added unplaced relay buffer {} on chip={}", relay_buffer_id, input_buf.chip_location());
    log_debug(tt::LogRouter, "New sender side pipe: {}", sender_side_pipe_id);
    log_relay_buffer_scenario(router, sender_side_pipe_id, dram_channel_rows);
    log_debug(tt::LogRouter, "New receiver side pipe: {}", receiver_side_pipe_id);
    log_relay_buffer_scenario(router, receiver_side_pipe_id, dram_channel_rows);
    
    return relay_buffer_id;
}


bool queues_on_same_channel(const Router &router, const std::vector<unique_id_t> &queue_input_ids) {
    auto channel = router.get_buffer(queue_input_ids.at(0)).queue_info().get_allocation_info().channel;
    for (auto queue_buffer_id : queue_input_ids) {
        if (router.get_buffer(queue_buffer_id).queue_info().get_allocation_info().channel != channel) {
            return false;
        }
    }

    return true;
}


/* Insert a relay buffer for each pipe input*/
std::vector<unique_id_t> insert_relay_buffer_for_queues_spanning_multiple_dram_channels(Router &router, unique_id_t pipe_id, const std::vector<unique_id_t> &queue_input_ids, const std::vector<std::unordered_set<int>> &dram_channel_rows, const std::vector<tt_cxy_pair> &consumer_core_locations) {
    const auto old_pipe_inputs = router.get_pipe(pipe_id).input_buffer_ids;
    TT_ASSERT(queue_input_ids.size() == old_pipe_inputs.size());
    auto unplaced_relay_buffers = std::vector<unique_id_t>();
    unplaced_relay_buffers.reserve(old_pipe_inputs.size());
    
    for (const auto input_buffer_id : old_pipe_inputs) {
        // auto queue_channel = router.get_buffer(input_buffer_id).queue_info().get_allocation_info().channel;

        const auto &input_buf = router.get_buffer(input_buffer_id);
        int size_tiles = 1;
        const tt::buffer_info &relay_buffer_info = create_relay_buffer_info(
            size_tiles,
            router.get_scatter_gather_granularity(input_buffer_id),
            router.get_buffer_total_epoch_tiles(input_buffer_id),
            router.get_buffer_data_format(input_buffer_id),
            router.get_buffer_tile_dims(input_buffer_id)
        );
        unique_id_t relay_buffer_id = router.add_new_buffer(relay_buffer_info, input_buf.chip_location());
        unplaced_relay_buffers.push_back(relay_buffer_id);
        log_debug(tt::LogRouter, "Added unplaced relay buffer {} on chip={}", relay_buffer_id, input_buf.chip_location());

        router.replace_pipe_input(pipe_id, input_buffer_id, relay_buffer_id);
        router.create_unicast_pipe_connection(input_buffer_id, relay_buffer_id, 1);
    }

    return unplaced_relay_buffers;
}


bool is_relay_buffer_required(
    Router &router, 
    const std::vector<tt_cxy_pair> &consumer_core_locations, 
    const std::vector<unique_id_t> &queue_input_ids, 
    const std::vector<std::unordered_set<int>> &dram_channel_rows,
    const std::unordered_set<unique_id_t> &queues_with_multiple_consumer_noc_rows
) {
    auto consumer_core_rows = std::vector<int>{};
    std::transform(consumer_core_locations.begin(), consumer_core_locations.end(), std::back_inserter(consumer_core_rows), [](const tt_cxy_pair &loc) { return loc.y; });

    bool requires_relay_buffer = false;
    for (auto queue_input_id : queue_input_ids) {
        const auto &queue_buffer = router.get_buffer(queue_input_id);
        int queue_dram_channel = queue_buffer.queue_info().get_allocation_info().channel;
        const auto &channel_routing_rows = dram_channel_rows.at(queue_dram_channel);
        auto dram_channel_mismatch = [&channel_routing_rows](const int row) {
            return channel_routing_rows.find(row) == channel_routing_rows.end();
        };
        requires_relay_buffer |= std::any_of(consumer_core_rows.begin(), consumer_core_rows.end(), dram_channel_mismatch);

        if (queue_buffer.core_location_assigned()) {
            int queue_noc_row = queue_buffer.core_location().y;
            requires_relay_buffer |= std::any_of(consumer_core_rows.begin(), consumer_core_rows.end(), [&queue_noc_row] (int row) { return queue_noc_row != row; } );

        }
    }

    bool any_input_queue_has_consumers_across_noc_rows = std::any_of(
        queue_input_ids.begin(),
        queue_input_ids.end(),
        [&router,&queues_with_multiple_consumer_noc_rows] (unique_id_t q_id) {
            auto base_id = router.is_buffer_scatter(q_id) ? router.get_scatter_buffer_base_id(q_id) : q_id;
            return queues_with_multiple_consumer_noc_rows.find(base_id) != queues_with_multiple_consumer_noc_rows.end();
        }
    );
    requires_relay_buffer |= any_input_queue_has_consumers_across_noc_rows;

    return requires_relay_buffer;
}

std::vector<unique_id_t> create_unplaced_relay_buffers_to_setup_all_dram_reads_to_be_on_same_row(
    Router &router, 
    unique_id_t pipe_id, 
    const std::vector<unique_id_t> &queue_input_ids, 
    int id, 
    const std::vector<std::unordered_set<int>> &dram_channel_rows,
    const std::unordered_set<unique_id_t> &queues_with_multiple_consumer_noc_rows
) {
    const auto pipe = router.get_pipes().at(pipe_id);

    bool pipe_has_input_or_output_scatter_buffers = has_input_or_output_scatter_buffers(router, pipe);
    bool input_queues_all_on_same_channel = queues_on_same_channel(router, queue_input_ids);
    bool input_queue_channel_constraints_satisfied = not pipe_has_input_or_output_scatter_buffers || input_queues_all_on_same_channel;
    if (not input_queue_channel_constraints_satisfied) {
        log_error("Expect all input queue buffers to be located on the same channel if pipe inputs or outputs are scatter buffers");
    }

    const std::vector<tt_cxy_pair> &consumer_core_locations = collect_pipe_consumer_core_locations(router, pipe_id);
    bool requires_relay_buffer = is_relay_buffer_required(router, consumer_core_locations, queue_input_ids, dram_channel_rows, queues_with_multiple_consumer_noc_rows);
    if (not requires_relay_buffer) {
        return {};
    }

    log_debug(tt::LogRouter, "-------------------------------------------------------");
    log_relay_buffer_scenario(router, pipe_id, dram_channel_rows);

    if (input_queues_all_on_same_channel) {
        auto relay_buffer_id = insert_relay_buffer_for_queues_on_single_dram_channel(router, pipe_id, dram_channel_rows, consumer_core_locations);
        return {relay_buffer_id};
    } else {
        return insert_relay_buffer_for_queues_spanning_multiple_dram_channels(router, pipe_id, queue_input_ids, dram_channel_rows, consumer_core_locations);
    }
}

std::vector<std::vector<int>> get_noc_row_dram_channels(const buda_SocDescriptor &soc_desc) {
    auto channels_per_row = std::vector<std::vector<int>>(soc_desc.grid_size.y);
    for (int c = 0; c < soc_desc.get_num_dram_channels(); c++) {
        for (const auto &subchannel_core : soc_desc.dram_cores.at(c)) {
            channels_per_row.at(subchannel_core.y).push_back(c);
        }
    }

    return channels_per_row;
}

std::vector<std::unordered_set<int>> dram_channel_rows_from_soc_descriptor(const buda_SocDescriptor &soc_desc) {
    auto dram_channel_row_map = std::vector<std::unordered_set<int>>(soc_desc.get_num_dram_channels());
    for (int c = 0; c < soc_desc.get_num_dram_channels(); c++) {
        for (const auto &subchannel_core : soc_desc.dram_cores.at(c)) {
            dram_channel_row_map[c].insert(subchannel_core.y);
        }
    }

    return dram_channel_row_map;
}


std::unordered_map<unique_id_t, std::pair<std::string, int>> build_buffer_consumer_map(const Router &router) {
    auto buffer_consumer_map = std::unordered_map<unique_id_t, std::pair<std::string, int>>();
    for (const auto &[op_name, buffer_grid] : router.get_op_input_buf_map()) {
        for (const auto &[row, buffer_row] : buffer_grid) {
            for (const auto &[col, operand_index_buffers] : buffer_row) {
                for (const auto &[operand_index, buffer_id] : operand_index_buffers) {
                    buffer_consumer_map[buffer_id] = std::make_pair(op_name, operand_index);
                }
            }
        }
    }

    return buffer_consumer_map;
}

void report_failed_queue_channel_constraints(const Router &router, unique_id_t pipe_id, const std::unordered_map<unique_id_t, std::pair<std::string, int>> &buffer_consumer_map) {
    const auto &pipe = router.get_pipe(pipe_id);
    if (pipe.has_consumer()) {
        log_error("Pipe {} with consumer {} into consumer's input index {}", pipe_id, pipe.consumer_name(), pipe.consumer_input_index());
    } else {
        auto output_buf_id = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(0).at(0) : pipe.output_buffer_ids().at(0);
        if (buffer_consumer_map.find(output_buf_id) != buffer_consumer_map.end()) {
            const auto &[consumer_name, input_index] = buffer_consumer_map.at(output_buf_id);
            log_error("Pipe {} with consumer {} into consumer's input index {}", pipe_id, consumer_name, input_index);
        } else {
            log_error("Pipe {}. Couldn't find consumer ops", pipe_id);
        }
    }
}

// Returns true if this type of core can support relay buffers, not if this specific core has
// resources available to implement a given relay buffer
bool core_supports_relay_buffers(const buda_SocDescriptor &soc_desc, const tt_xy_pair &core_xy) {
    bool coordinate_has_functional_core = soc_desc.cores.find(core_xy) != soc_desc.cores.end() && soc_desc.cores.at(core_xy).type != CoreType::HARVESTED;
    if (not coordinate_has_functional_core) {
        return false;
    }

    bool coordinate_has_worker_core = soc_desc.cores.find(core_xy) != soc_desc.cores.end() && soc_desc.cores.at(core_xy).type == CoreType::WORKER;
    if (not coordinate_has_worker_core) {
        return false;
    }

    return true;
}

int count_dram_readers_that_will_relocate_to_other_rows(const Router &router, const std::vector<std::vector<int>> &noc_row_dram_channels, chip_id_t chip_id, int row, int col) {
    int count = 0;

    const auto &core_attributes = router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip_id, col, row));
    for (auto id : core_attributes.get_input_buffers()) {
        unique_id_t input_pipe_id = router.get_buffer_input_pipes().at(id);
        auto input_pipe = router.get_pipe(input_pipe_id);
        bool input_is_queue = router.is_queue_buffer(input_pipe.input_buffer_ids.at(0));
        if (input_is_queue) {
            auto input_queue_dram_channels = std::unordered_set<int>{};
            for (auto id : input_pipe.input_buffer_ids) {
                input_queue_dram_channels.insert(router.get_buffer(id).queue_info().get_allocation_info().channel);
            }
            auto core_row_dram_channels = std::unordered_set<int>(noc_row_dram_channels.at(row).begin(), noc_row_dram_channels.at(row).end());

            // if any producer is on a different channel then we'll move the reader stream
            bool dram_read_will_relocate = std::any_of(input_queue_dram_channels.begin(), input_queue_dram_channels.end(), [&core_row_dram_channels](auto id) {return core_row_dram_channels.find(id) == core_row_dram_channels.end(); });
            if (dram_read_will_relocate) {
                count ++;
                log_debug(tt::LogRouter, "dram_read_will_relocate");
            }
        }
    }

    TT_ASSERT(count <= 8);
    return count;
}

std::tuple<
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>
> get_number_of_available_dram_read_streams_per_core(const Router &router) {
    auto per_core_available_read_from_dram_streams = std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>();
    auto per_core_extra_streams_available = std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>();
    const auto &chip_ids = router.get_cluster_graph().cluster_description().get_all_chips();
    const auto &cluster_resource_model = router.get_cluster_resource_model();
    const auto &soc_desc = router.get_soc_descriptor();
    const auto &noc_row_dram_channels = get_noc_row_dram_channels(soc_desc);
    log_debug(tt::LogRouter, "Available dram read streams per row");
    for (chip_id_t chip_id : chip_ids) {
        log_debug(tt::LogRouter, "CHIP {}", chip_id);
        per_core_available_read_from_dram_streams.insert({chip_id,{}});
        per_core_extra_streams_available.insert({chip_id,{}});
        for (int r = 0; r < static_cast<int>(soc_desc.grid_size.y); r++) {
            for (int c = 0; c < static_cast<int>(soc_desc.grid_size.x); c++) {
                const auto &core_xy = tt_xy_pair(c,r);
                if (not core_supports_relay_buffers(soc_desc, core_xy)) {
                    continue;
                }

                const auto &core_attributes = cluster_resource_model.get_core_attributes(tt_cxy_pair(chip_id, c, r));

                // This amount only accounts for available streams in the current moment. It doesn't account for the 
                // possibility that some DRAM read streams will be moved to other rows
                //   - if a core on this row reads from DRAM then from the core resource tracker it will use a DRAM read stream.
                //     However, if it's reading from another row, then the DRAM read stream will eventually move to a relay
                //     buffer on another row
                int dram_read_streams_available = std::min(core_attributes.number_of_available_input_from_dram_slots(), core_attributes.number_of_available_extra_streams()); 
                int extra_streams_available = core_attributes.number_of_available_extra_streams();
                TT_ASSERT(core_attributes.get_max_extra_streams() - core_attributes.get_used_extra_streams() == extra_streams_available);

                per_core_available_read_from_dram_streams[chip_id][r][c] = dram_read_streams_available;
                per_core_extra_streams_available[chip_id][r][c] = extra_streams_available;

                // We shouldn't need to worry about (currently used) DRAM read streams being relocated to other cores by the introduction
                // of relay buffer, because those required relay buffers are already inserted as unplaced relay buffers into the dataflow
                // graph
                // dram_read_streams_available += count_dram_readers_that_will_relocate_to_other_rows(router, noc_row_dram_channels, chip_id, r, c);
                log_debug(tt::LogRouter, "\tr={},c={}: DRAM read streams available: {}, extra streams available {}", r, c, dram_read_streams_available, extra_streams_available);
            }
        }
    }

    return {per_core_available_read_from_dram_streams, per_core_extra_streams_available};
}


std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> get_number_of_available_bytes_per_core(const Router &router) {
    auto noc_core_total_l1_bytes_available = std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>();
    const auto &chip_ids = router.get_cluster_graph().cluster_description().get_all_chips();
    const auto &cluster_resource_model = router.get_cluster_resource_model();
    const auto &soc_desc = router.get_soc_descriptor();
    log_debug(tt::LogRouter, "Available L1 bytes per row");
    for (chip_id_t chip_id : chip_ids) {
        log_debug(tt::LogRouter, "CHIP {}", chip_id);
        noc_core_total_l1_bytes_available.insert({chip_id,{}});
        for (int r = 0; r < static_cast<int>(soc_desc.grid_size.y); r++) {
            for (int c = 0; c < static_cast<int>(soc_desc.grid_size.x); c++) {
                const auto &core_xy = tt_xy_pair(c,r);
                if (not core_supports_relay_buffers(soc_desc, core_xy)) {
                    continue;
                }

                const auto &core_attributes = cluster_resource_model.get_core_attributes(tt_cxy_pair(chip_id, c, r));
                int l1_bytes_available = core_attributes.available_l1_bytes();
                noc_core_total_l1_bytes_available[chip_id][r][c] = l1_bytes_available;
                // this should actually be asserted on in the core attribute getter but we can't assert there yet
                // since there are currently a handful of tests in regression that exceed the limit just from
                // kernel buffers defined in the netlist
                // TT_ASSERT(noc_core_total_l1_bytes_available[chip_id][r][c] >= 0);
                log_debug(tt::LogRouter, "\tr{},c{}={}", r, c, l1_bytes_available);
            }
        }
    }

    return noc_core_total_l1_bytes_available;
}


void log_available_resources_per_core(const Router &router, const std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>& per_core_resource_available, const std::string resource_type_string) {
    const auto &soc_desc = router.get_soc_descriptor();
    log_debug(tt::LogRouter, "Available {} per core", resource_type_string);
    const auto &chip_ids = router.get_cluster_graph().cluster_description().get_all_chips();
    for (chip_id_t chip_id : chip_ids) {
        log_debug(tt::LogRouter, "CHIP {}", chip_id);
        const auto &chip_map = per_core_resource_available.at(chip_id);
        for (int r = 0; r < static_cast<int>(soc_desc.grid_size.y); r++) {
            if (chip_map.find(r) == chip_map.end()) {
                continue;
            }
            const auto &row_map = chip_map.at(r);
            for (int c = 0; c < static_cast<int>(soc_desc.grid_size.x); c++) {
                if (row_map.find(c) == row_map.end()) {
                    continue;
                }
                log_debug(tt::LogRouter, "\tr{},c{}={}", r, c, row_map.at(c));
            }
        }
    }
}

std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> get_amount_available_resource_per_core(const Router &router, std::function<int(const HwCoreAttributes&)> hw_core_attribute_getter) {
    auto available_resource_per_core = std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>>();
    const auto &chip_ids = router.get_cluster_graph().cluster_description().get_all_chips();
    const auto &cluster_resource_model = router.get_cluster_resource_model();
    const auto &soc_desc = router.get_soc_descriptor();
    for (chip_id_t chip_id : chip_ids) {
        available_resource_per_core.insert({chip_id,{}});
        for (int r = 0; r < static_cast<int>(soc_desc.grid_size.y); r++) {
            for (int c = 0; c < static_cast<int>(soc_desc.grid_size.x); c++) {
                const auto &core_xy = tt_xy_pair(c,r);
                if (not core_supports_relay_buffers(soc_desc, core_xy)) {
                    continue;
                }

                const auto &core_attributes = cluster_resource_model.get_core_attributes(tt_cxy_pair(chip_id, c, r));
                int number_of_resource_available_per_core = hw_core_attribute_getter(core_attributes);//.number_of_available_active_dram_queues();
                available_resource_per_core[chip_id][r][c] = number_of_resource_available_per_core;
                // this should actually be asserted on in the core attribute getter but we can't assert there yet
                // since there are currently a handful of tests in regression that exceed the limit just from
                // kernel buffers defined in the netlist
                // TT_ASSERT(noc_core_total_l1_bytes_available[chip_id][r][c] >= 0);
            }
        }
    }

    return available_resource_per_core;
}


std::unordered_map<unique_id_t, std::vector<unique_id_t>> get_all_queue_scatter_buffers(const Router &router) {
    auto queue_scatter_buffers = std::unordered_map<unique_id_t, std::vector<unique_id_t>>();
    for (const auto &[buffer_id, router_buffer_info] : router.get_buffer_map()) {
        bool is_dram_queue = router.is_queue_buffer(buffer_id) &&
            router.get_queue(router_buffer_info.queue_info().get_parent_queue_id()).loc ==
                QUEUE_LOCATION::DRAM;
        if (is_dram_queue && router.is_buffer_scatter(buffer_id)) {
            unique_id_t base_buffer_id = router.get_scatter_buffer_base_id(buffer_id);
            queue_scatter_buffers[base_buffer_id].push_back(buffer_id);
        }
    }

    return queue_scatter_buffers;
}

void group_all_queues_that_have_same_consumers(
    const Router &router, 
    unique_id_t queue_buffer_id,
    int assigned_group_id,
    const std::unordered_map<unique_id_t, std::vector<unique_id_t>> &queue_scatter_buffers,
    std::unordered_map<unique_id_t, int> &queue_group_id, 
    std::unordered_map<int, std::unordered_set<unique_id_t>> &group_queues
) {
    auto visited_queues = std::unordered_set<unique_id_t>();
    auto visited_pipes = std::unordered_set<unique_id_t>();
    auto queues_to_visit = std::vector<unique_id_t>{queue_buffer_id};

    while (queues_to_visit.size() > 0) {
        unique_id_t q_id = queues_to_visit.back();
        queues_to_visit.pop_back();

        visited_queues.insert(q_id);
        queue_group_id.insert({q_id, assigned_group_id});
        group_queues[assigned_group_id].insert(q_id);

        std::unordered_set<unique_id_t> all_scatter_buf_output_pipes = {};
        bool is_scatter_buffer = router.is_buffer_scatter(q_id);
        bool is_scatter_buffer_parent = queue_scatter_buffers.find(q_id) != queue_scatter_buffers.end();
        bool visit_scatter_buffer_pipes = is_scatter_buffer_parent || is_scatter_buffer;
        if (visit_scatter_buffer_pipes) {
            unique_id_t parent_id = router.get_scatter_buffer_base_id(q_id);
            for (unique_id_t output_pipe_id : router.get_buffer_output_pipes().at(parent_id)) {
                all_scatter_buf_output_pipes.insert(output_pipe_id);
            }
            for (auto scatter_buf_id : queue_scatter_buffers.at(parent_id)) {
                for (unique_id_t output_pipe_id : router.get_buffer_output_pipes().at(scatter_buf_id)) {
                    all_scatter_buf_output_pipes.insert(output_pipe_id);
                }
                visited_queues.insert(scatter_buf_id);
                queue_group_id.insert({scatter_buf_id, assigned_group_id});
                group_queues[assigned_group_id].insert(scatter_buf_id);
            }

            for (unique_id_t output_pipe_id : router.get_buffer_output_pipes().at(q_id)) {
                if (visited_pipes.find(output_pipe_id) == visited_pipes.end()) {
                    visited_pipes.insert(output_pipe_id);
                    const auto &pipe_inputs = router.get_pipe(output_pipe_id).input_buffer_ids;
                    std::copy_if(
                        pipe_inputs.begin(), 
                        pipe_inputs.end(), 
                        std::back_inserter(queues_to_visit), 
                        [&visited_queues](unique_id_t other_producer_queue_id) { return visited_queues.find(other_producer_queue_id) == visited_queues.end(); } 
                    );
                }
            }
        }

        const auto &all_buf_output_pipes = visit_scatter_buffer_pipes ? all_scatter_buf_output_pipes : router.get_buffer_output_pipes().at(q_id);
        for (unique_id_t output_pipe_id : all_buf_output_pipes) {
            if (visited_pipes.find(output_pipe_id) == visited_pipes.end()) {
                visited_pipes.insert(output_pipe_id);
                const auto &pipe_inputs = router.get_pipe(output_pipe_id).input_buffer_ids;
                std::copy_if(
                    pipe_inputs.begin(), 
                    pipe_inputs.end(), 
                    std::back_inserter(queues_to_visit), 
                    [&visited_queues](unique_id_t other_producer_queue_id) { return visited_queues.find(other_producer_queue_id) == visited_queues.end(); } 
                );
            }
        }
        
    }
}

std::tuple<std::unordered_map<unique_id_t, int>, std::unordered_map<int, std::unordered_set<unique_id_t>>> group_queues_by_shared_consumers(
    const Router &router
) {
    const auto &queue_scatter_buffers = get_all_queue_scatter_buffers(router);

    int next_available_group_id = 0;
    auto queue_group_id = std::unordered_map<unique_id_t, int>();
    auto group_queues = std::unordered_map<int, std::unordered_set<unique_id_t>>();
    for (const auto &[buffer_id, router_buffer_info] : router.get_buffer_map()) {
        bool is_dram_queue = router.is_queue_buffer(buffer_id) &&
            router.get_queue(router_buffer_info.queue_info().get_parent_queue_id()).loc ==
                QUEUE_LOCATION::DRAM;
        bool is_producer_queue = router.get_buffer_output_pipes().find(buffer_id) != router.get_buffer_output_pipes().end();
        bool already_mapped_to_group = queue_group_id.find(buffer_id) != queue_group_id.end();
        if (is_dram_queue && is_producer_queue && !already_mapped_to_group) {
            queue_group_id.insert({buffer_id, next_available_group_id});

            group_all_queues_that_have_same_consumers(
                router, 
                buffer_id,
                next_available_group_id,
                queue_scatter_buffers,
                queue_group_id, 
                group_queues
            );

            next_available_group_id++;
        }
    }

    return {queue_group_id, group_queues};
}

void sort_queue_groups_by_max_relay_buffer_size(const Router &router, std::vector<std::tuple<std::vector<unique_id_t>, std::vector<unique_id_t>>> &queue_group_consumer_buffers) {
    auto group_max_buffer_sizes = std::vector<int>(queue_group_consumer_buffers.size());
    for (int i = 0; i < group_max_buffer_sizes.size(); i++) {
        int max_size = 0;
        for (auto id : std::get<1>(queue_group_consumer_buffers.at(i))) {
            int size = router.get_buffer(id).info().allocated_size_in_bytes(); 
            max_size = std::max(max_size, size);
        }
        group_max_buffer_sizes.at(i) = max_size;
    }
    auto indices = std::vector<int>(queue_group_consumer_buffers.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::sort(indices.rbegin(), indices.rend(), [&group_max_buffer_sizes] (auto idx0, auto idx1) { return group_max_buffer_sizes.at(idx0) < group_max_buffer_sizes.at(idx1); });
    TT_ASSERT(indices.size() == 0 || group_max_buffer_sizes.at(indices.front()) >= group_max_buffer_sizes.at(indices.back()));

    auto copy = queue_group_consumer_buffers;
    queue_group_consumer_buffers.clear();
    for (auto i : indices) {
        queue_group_consumer_buffers.push_back(copy.at(i));
    }
}

std::vector<std::tuple<std::vector<unique_id_t>, std::vector<unique_id_t>>> get_queue_group_consumer_buffers(const Router &router) {
    auto queue_group_consumer_buffers = std::vector<std::tuple<std::vector<unique_id_t>, std::vector<unique_id_t>>>();

    const auto &[queue_group_id, group_queues] = group_queues_by_shared_consumers(router);
    for (const auto &[group_id, queues] : group_queues) {
        auto consumer_buffers = std::unordered_set<unique_id_t>();
        for (auto queue_id : queues) {
            for (auto out_pipe_id : router.get_buffer_output_pipes().at(queue_id)) {
                for (auto buf_id : router.get_pipe(out_pipe_id).output_buffer_ids()) {
                    consumer_buffers.insert(buf_id);
                }
            }
        }

        queue_group_consumer_buffers.emplace_back(
            std::make_tuple(
                std::vector<unique_id_t>(queues.begin(), queues.end()),
                std::vector<unique_id_t>(consumer_buffers.begin(), consumer_buffers.end()))
        );
    }

    sort_queue_groups_by_max_relay_buffer_size(router, queue_group_consumer_buffers);

    return queue_group_consumer_buffers;
}

void remove_subchannels_on_non_worker_rows(const Router &router, int dram_channel, std::vector<int> &dram_subchannels) {
    const auto &routing_y_to_worker_y = router.get_soc_descriptor().routing_y_to_worker_y;
    const auto &subchannel_cores = router.get_soc_descriptor().dram_cores.at(dram_channel);
    dram_subchannels.erase(
        std::remove_if(
            dram_subchannels.begin(), 
            dram_subchannels.end(),
            [&routing_y_to_worker_y, &subchannel_cores](int subchannel) { 
                return routing_y_to_worker_y.find(subchannel_cores.at(subchannel).y) == routing_y_to_worker_y.end(); 
            }
        ),
        dram_subchannels.end()
    );
}

std::vector<int> get_consumer_buffer_valid_subchannels(
    const Router &router, const std::vector<unique_id_t> &consumer_buffer_ids, int dram_channel, bool is_prolog
) {
    auto buffer_is_placed_on_core = [&router](unique_id_t id) { return router.get_buffer(id).core_location_assigned(); };
    auto placed_consumer_iter = std::find_if(consumer_buffer_ids.begin(), consumer_buffer_ids.end(), buffer_is_placed_on_core);
    bool any_consumer_placed = (placed_consumer_iter != consumer_buffer_ids.end());
    if (any_consumer_placed) {
        const auto &soc_desc = router.get_soc_descriptor();
        const auto &subchannels = soc_desc.dram_cores.at(dram_channel);

        if (is_prolog) {
            auto valid_subchannels_set = std::set<int>();
            do  {
                auto consumer_noc_row = router.get_buffer_location(*placed_consumer_iter).y;
                for (int c = 0; c < static_cast<int>(subchannels.size()); c++) {
                    if (subchannels.at(c).y == consumer_noc_row) {
                        valid_subchannels_set.insert(c);
                        TT_ASSERT(c < 3);
                    }
                }

                placed_consumer_iter = std::find_if(std::next(placed_consumer_iter), consumer_buffer_ids.end(), buffer_is_placed_on_core);
            } while (placed_consumer_iter != consumer_buffer_ids.end());

            auto valid_subchannels = std::vector<int>(valid_subchannels_set.begin(), valid_subchannels_set.end());
            if (valid_subchannels.size() == 0) {
                valid_subchannels.resize(router.get_soc_descriptor().dram_cores.at(dram_channel).size());
                std::iota(valid_subchannels.begin(), valid_subchannels.end(),0);
            }
            return valid_subchannels;
        } else {
            const auto &consumer_core_xy = router.get_buffer(*placed_consumer_iter).core_location();
            int consumer_noc_row = consumer_core_xy.y;

            do  {
                TT_ASSERT(consumer_noc_row == static_cast<int>(router.get_buffer(*placed_consumer_iter).core_location().y), "Relay buffer should have been placed here");
                placed_consumer_iter = std::find_if(std::next(placed_consumer_iter), consumer_buffer_ids.end(), buffer_is_placed_on_core);
            } while (placed_consumer_iter != consumer_buffer_ids.end());
            for (int c = 0; c < static_cast<int>(subchannels.size()); c++) {
                if (static_cast<int>(subchannels.at(c).y) == consumer_noc_row) {
                    return {c};
                }
            }

            TT_ASSERT(false, "Relay buffer should have been placed between producer queue and consumer buffer " + std::to_string(*placed_consumer_iter));
            return {};
        }
    } else {
        auto subchannel_candidates = std::vector<int>(router.get_soc_descriptor().dram_cores.at(dram_channel).size());
        std::iota(subchannel_candidates.begin(), subchannel_candidates.end(),0);
        return subchannel_candidates;
    }
}

struct unplaced_buffer_spec_t {
    unique_id_t id;
    int required_bytes;
    int required_extra_streams;
    int required_active_dram_queues;
};

void sort_buffer_specs_by_size_descending(std::vector<unplaced_buffer_spec_t> &unplaced_buffer_specs) {
    std::sort(unplaced_buffer_specs.rbegin(), unplaced_buffer_specs.rend(), [](const auto &a, const auto &b){ return a.required_bytes < b.required_bytes; });
    TT_ASSERT(unplaced_buffer_specs.size() == 0 || unplaced_buffer_specs.front().required_bytes >= unplaced_buffer_specs.back().required_bytes);
}

std::vector<unplaced_buffer_spec_t> get_unplaced_buffer_specs(
    Router &router, 
    const std::vector<unique_id_t> &consumer_buffer_ids, 
    std::unordered_set<unique_id_t> &accounted_for_relay_buffers
) {
    auto buffer_core_unassigned = [&router](auto id) -> bool { return not router.get_buffer(id).core_location_assigned(); };

    auto unplaced_buffer_specs = std::vector<unplaced_buffer_spec_t>{};
    auto seen_ids = std::unordered_set<unique_id_t>{};
    for (auto consumer_id : consumer_buffer_ids) {
        TT_ASSERT(accounted_for_relay_buffers.find(consumer_id) == accounted_for_relay_buffers.end());
        if (buffer_core_unassigned(consumer_id) && seen_ids.find(consumer_id) == seen_ids.end()) {
            seen_ids.insert(consumer_id);
            int allocated_bytes = router.get_buffer(consumer_id).info().allocated_size_in_bytes();

            // Might need to refine this to a set of input pipes that we iterate through
            unique_id_t pipe_id = router.get_buffer_input_pipes().at(consumer_id);
            int required_extra_streams =  relay_buffer_required_extra_streams(router, pipe_id);
            int required_active_dram_queues = get_number_of_active_dram_queues(router, pipe_id);
            allocated_bytes += compute_pipe_segment_gather_extra_input_buffering_size(router, pipe_segment_id_t{.pipe_id=pipe_id, .segment=0});
            log_debug(tt::LogRouter, "- Require {} B for relay buffer {}. {} extra streams required. Pipe: {}", allocated_bytes, consumer_id, required_extra_streams, pipe_id);

            unplaced_buffer_specs.push_back({.id=consumer_id, .required_bytes=allocated_bytes, .required_extra_streams=required_extra_streams, .required_active_dram_queues=required_active_dram_queues});
        }
    }

    sort_buffer_specs_by_size_descending(unplaced_buffer_specs);

    return unplaced_buffer_specs;
}

std::tuple<int, std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>>> select_subchannel(
    Router &router,
    int dram_channel,
    chip_id_t chip,
    const std::vector<unplaced_buffer_spec_t> &unplaced_buffers,
    const buda_SocDescriptor &soc_desc, 
    std::vector<int> &subchannel_candidates,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_available_read_from_dram_streams,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_extra_streams_available,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &noc_cores_total_l1_bytes_available,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_available_active_dram_queues
) {
    if (unplaced_buffers.size() == 0) {
        // TT_ASSERT(queue_info.is_prolog() || subchannel_candidates.size() == 1);
        return {subchannel_candidates.front(), {}};
    }

    auto buffer_placements = std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>>{};

    std::vector<int> selected_noc_cols = {};// for each relay buffer, specify which col in the row to map to so we can do L1 accounting properly
    int selected_subchannel_extra_available_l1 = -1;
    int selected_subchannel = -1;

    log_debug(tt::LogRouter, "Finding best subchannel amongst subchannel candidates: {}", fmt::join(subchannel_candidates, ", "));
    for (int subchannel : subchannel_candidates) {
        log_trace(tt::LogRouter,"\t-Checking subchannel {}", subchannel);
        const tt_xy_pair &dram_core_xy = soc_desc.dram_cores.at(dram_channel).at(subchannel);
        int noc_row = dram_core_xy.y;

        auto row_cores_l1_available = noc_cores_total_l1_bytes_available.at(chip).at(noc_row);
        auto row_cores_extra_streams_available = per_core_extra_streams_available.at(chip).at(noc_row);
        auto row_cores_dram_read_streams_available = per_core_available_read_from_dram_streams.at(chip).at(noc_row);
        auto row_cores_active_dram_queues_available = per_core_available_active_dram_queues.at(chip).at(noc_row);

        std::vector<int> noc_cols = {};// for each relay buffer, specify which col in the row to map to so we can do L1 accounting properly
        bool all_fit = true;
        int subchannel_extra_available_l1 = 0;
        for (int i = 0; (i < unplaced_buffers.size()) && all_fit; i++) {
            const auto &spec = unplaced_buffers.at(i);
            log_trace(tt::LogRouter,"\t\t-Checking buffer {}", spec.id);
            int bytes_required = spec.required_bytes;
            int extra_streams_required = spec.required_extra_streams;
            int active_dram_queues_required = spec.required_active_dram_queues;
            int highest_capacity_col = -1;
            int highest_remaining_capacity = -1;
            for (int noc_col = 0; noc_col < soc_desc.grid_size.x; noc_col++) {
                if (not core_supports_relay_buffers(soc_desc, tt_xy_pair(noc_col, noc_row))) {
                    continue;
                }
                
                int bytes_available = row_cores_l1_available.at(noc_col);
                int enough_l1_space_on_core = bytes_required <= bytes_available;
                int enough_dram_streams_on_core = 1 <= row_cores_dram_read_streams_available.at(noc_col);
                int enough_extra_sreams_on_core = extra_streams_required <= row_cores_extra_streams_available.at(noc_col);
                int enough_active_dram_queues = active_dram_queues_required <= row_cores_active_dram_queues_available.at(noc_col);

                bool fits_on_core = enough_l1_space_on_core && enough_dram_streams_on_core && enough_extra_sreams_on_core && enough_active_dram_queues;
                if (fits_on_core && bytes_available >= highest_remaining_capacity) {
                    log_trace(tt::LogRouter, "\t\t\t-Found good core at col {}", noc_col);
                    highest_capacity_col = noc_col;
                } else {
                    log_trace(tt::LogRouter, "\t\t\t-Col {} invalid. Bytes available? {}. Required={}, Available={}; Extra streams available? {}. Required={}, Available={}; dram read streams available? {}", 
                        noc_col, enough_l1_space_on_core, bytes_required, bytes_available, enough_extra_sreams_on_core, extra_streams_required, row_cores_extra_streams_available.at(noc_col), enough_dram_streams_on_core
                    );
                }
            }
            all_fit = all_fit && (highest_capacity_col >= 0);
            if (highest_capacity_col >= 0) {
                row_cores_l1_available.at(highest_capacity_col) -= bytes_required;
                row_cores_dram_read_streams_available.at(highest_capacity_col) -= 1;
                row_cores_extra_streams_available.at(highest_capacity_col) -= extra_streams_required;
                row_cores_active_dram_queues_available.at(highest_capacity_col) -= active_dram_queues_required;

                noc_cols.push_back(highest_capacity_col);
                subchannel_extra_available_l1 += noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(highest_capacity_col);
                log_trace(
                    tt::LogRouter, 
                    "\t\t-Buffer {} mapped to core (c={},y={},x={}) for subchannel {}. {} B remaining on core. {} extra streams remaining on core. subchannel_extra_available_l1: {}", 
                    spec.id, chip, noc_row, highest_capacity_col, subchannel, row_cores_l1_available.at(highest_capacity_col), row_cores_extra_streams_available.at(highest_capacity_col), subchannel_extra_available_l1
                );

            } else {
                TT_ASSERT(!all_fit);
            }
        }
        if (all_fit && subchannel_extra_available_l1 > selected_subchannel_extra_available_l1) {
            log_trace(tt::LogRouter, "\t-subchannel {} is current selected candidate", subchannel);
            selected_subchannel = subchannel;
            selected_subchannel_extra_available_l1 = subchannel_extra_available_l1;
            selected_noc_cols = noc_cols;
        }
    }

    if (selected_subchannel >= 0) {
        TT_ASSERT(selected_noc_cols.size() == unplaced_buffers.size());
        const tt_xy_pair &dram_core_xy = soc_desc.dram_cores.at(dram_channel).at(selected_subchannel);
        int noc_row = dram_core_xy.y;
        for (int i = 0; i < selected_noc_cols.size(); i++) {
            int col = selected_noc_cols.at(i);
            const auto &spec = unplaced_buffers.at(i);
            buffer_placements.push_back({spec, tt_cxy_pair(chip, col, noc_row)});


            per_core_available_read_from_dram_streams.at(chip).at(noc_row).at(col) -= 1;
            TT_ASSERT(per_core_available_read_from_dram_streams.at(chip).at(noc_row).at(col) >= 0);
            int old_extra_stream_count = per_core_extra_streams_available.at(chip).at(noc_row).at(col);
            per_core_extra_streams_available.at(chip).at(noc_row).at(col) -= spec.required_extra_streams;
            TT_ASSERT(per_core_extra_streams_available.at(chip).at(noc_row).at(col) >= 0);

            noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(col) -= spec.required_bytes;
            TT_ASSERT(noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(col) >= 0);

            per_core_available_active_dram_queues.at(chip).at(noc_row).at(col) -= spec.required_active_dram_queues;
            TT_ASSERT(per_core_available_active_dram_queues.at(chip).at(noc_row).at(col) >= 0);

            log_trace(tt::LogRouter, "Assigned buffer {} to core (c={},y={},x={}). {} extra streams previously available, {} extra streams used, {} remaining available (permanantly)", spec.id, chip, noc_row, col, old_extra_stream_count, spec.required_extra_streams, per_core_extra_streams_available.at(chip).at(noc_row).at(col));
        }

    }

    return {selected_subchannel, buffer_placements};
}


void place_relay_buffers_on_core_for_wha0(Router &router, std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>> &unplaced_relay_buffers) {
    log_debug(tt::LogRouter, "Assigning relay buffers to cores");
    // std::sort(
    //     unplaced_relay_buffers.begin(), 
    //     unplaced_relay_buffers.end(), 
    //     [&router](auto a, auto b) { return router.get_buffer(a.first.id).info().allocated_size_in_bytes() > router.get_buffer(b.first.id).info().allocated_size_in_bytes(); }
    // );
    // TT_ASSERT(unplaced_relay_buffers.size() == 0 || router.get_buffer(unplaced_relay_buffers.front().first.id).info().allocated_size_in_bytes() >= router.get_buffer(unplaced_relay_buffers.back().first.id).info().allocated_size_in_bytes());

    for (const auto &[buffer_spec, location] : unplaced_relay_buffers) {
        unique_id_t relay_buffer_id = buffer_spec.id;
        // place it
        auto &unplaced_buf = router.get_buffer(relay_buffer_id);
        TT_ASSERT(!unplaced_buf.core_location_assigned());
        // if (unplaced_buf.core_location_assigned()) {
        //     continue;
        // }
        const auto &output_pipes = router.get_buffer_output_pipes().at(relay_buffer_id);
        unique_id_t relay_buf_input_pipe_id = router.get_buffer_input_pipes().at(relay_buffer_id);
        TT_ASSERT(output_pipes.size() == 1, "Relay buffers shouldn't have multiple output pipes at this point");
        
        unique_id_t output_pipe_id = *(output_pipes.begin());
        const pipe_t &output_pipe = router.get_pipe(output_pipe_id);
        
        unique_id_t input_pipe_id = router.get_buffer_input_pipes().at(relay_buffer_id);
        const pipe_t &input_pipe = router.get_pipe(input_pipe_id);
        auto producer_queue_id = input_pipe.input_buffer_ids.at(0);
        auto queue_dram_core = router.get_buffer_location(producer_queue_id);
        int noc_row = queue_dram_core.y;

        const tt_cxy_pair &selected_relay_buffer_core = location;
        log_debug(tt::LogRouter, "Assigning buffer {} to core (c={},y={},x={})", relay_buffer_id, selected_relay_buffer_core.chip, selected_relay_buffer_core.y, selected_relay_buffer_core.x);
        log_debug(tt::LogRouter, "Available extra streams before assignment: {}", router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(selected_relay_buffer_core)).number_of_available_extra_streams());

        auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(selected_relay_buffer_core);
        bool core_has_capacity_for_relay_buffer = 
            core_attrs.has_available_input_from_dram_slot() && 
            core_attrs.has_available_relay_buffer_slot() && 
            core_attrs.can_allocate_bytes(buffer_spec.required_bytes) && 
            core_attrs.has_extra_streams_available(buffer_spec.required_extra_streams);
        TT_ASSERT(core_has_capacity_for_relay_buffer);
        int old_extra_streams_available = core_attrs.number_of_available_extra_streams();
        int old_used_extra_streams = core_attrs.get_used_extra_streams();

        router.assign_buffer_to_core(relay_buffer_id, selected_relay_buffer_core);


        if (not output_pipe.core_location_assigned()) {
            const auto &pipe_location = router.get_buffer_location(output_pipe.output_buffer_ids().at(0));
            log_debug(tt::LogRouter, "Assigning output pipe {} to core (c={},y={},x={})", output_pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            router.assign_pipe_to_core(output_pipe_id, pipe_location);//selected_relay_buffer_core);
        } else {
            log_trace(tt::LogRouter,"Output pipe {} already assigned to core (c={},y={},x={})", output_pipe_id, output_pipe.core_location().chip, output_pipe.core_location().y, output_pipe.core_location().x);
        }
        if (not input_pipe.core_location_assigned()) {
            log_debug(tt::LogRouter, "Assigning input pipe {} to core (c={},y={},x={})", input_pipe_id, selected_relay_buffer_core.chip, selected_relay_buffer_core.y, selected_relay_buffer_core.x);
            router.assign_pipe_to_core(input_pipe_id, selected_relay_buffer_core);
        } else {
            log_trace(tt::LogRouter,"Input pipe {} already assigned to core (c={},y={},x={})", input_pipe_id, input_pipe.core_location().chip, input_pipe.core_location().y, input_pipe.core_location().x);
        }

        log_debug(tt::LogRouter, "Relay buffer {} assigned to buffer (c={},y={},x={}). Remaining extra streams = {}, remaining L1 bytes = {}", 
            relay_buffer_id, selected_relay_buffer_core.chip, selected_relay_buffer_core.y, selected_relay_buffer_core.x, core_attrs.get_used_extra_streams(), core_attrs.available_l1_bytes()
        );
        int new_extra_streams_available = core_attrs.number_of_available_extra_streams();
        int new_used_extra_streams = core_attrs.get_used_extra_streams();
        log_debug(tt::LogRouter, "Available extra streams after assignment: {}", router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(selected_relay_buffer_core)).number_of_available_extra_streams());
        if (!(old_extra_streams_available - buffer_spec.required_extra_streams <= new_extra_streams_available)) {
            log_error("Extra stream counts mismatched between core_attributes and side book-keeping when inserting relay buffer {}. Spec required {} extra streams but {} were used", relay_buffer_id, buffer_spec.required_extra_streams, old_extra_streams_available - new_extra_streams_available);
            TT_ASSERT(false);
        }
        TT_ASSERT(new_used_extra_streams - (old_extra_streams_available - new_extra_streams_available) == old_used_extra_streams);
    }
}

void assign_subchannel_to_queue(
    Router &router,
    unique_id_t queue_buffer_id,
    chip_id_t chip,
    int dram_channel,
    int selected_subchannel,
    const buda_SocDescriptor &soc_desc,
    const std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>> &buffer_placements,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_available_read_from_dram_streams,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_extra_streams_available,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &noc_cores_total_l1_bytes_available,

    std::unordered_set<unique_id_t> &accounted_for_relay_buffers
) {
    // log_debug("Selected noc cols: {}", fmt::join(selected_noc_cols, "B , "));

    const tt_xy_pair &dram_core_xy = soc_desc.dram_cores.at(dram_channel).at(selected_subchannel);
    int noc_row = dram_core_xy.y;

    router.update_dram_queue_core_location(queue_buffer_id, tt_cxy_pair(chip, dram_core_xy));
    for (const auto &[buffer_spec, location] : buffer_placements) {
        TT_ASSERT(noc_row == location.y);
        // per_core_available_read_from_dram_streams.at(chip).at(noc_row).at(location.x) -= 1;
        // TT_ASSERT(per_core_available_read_from_dram_streams.at(chip).at(noc_row).at(location.x) >= 0);
        // per_core_extra_streams_available.at(chip).at(noc_row).at(location.x) -= buffer_spec.required_extra_streams;
        // TT_ASSERT(per_core_extra_streams_available.at(chip).at(noc_row).at(location.x) >= 0);
        

        // noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(location.x) -= buffer_spec.required_bytes;
        // TT_ASSERT(noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(location.x) >= 0);
        accounted_for_relay_buffers.insert(buffer_spec.id);

        // auto unplaced_buffers = std::vector<unique_id_t>{buffer_spec.id};
        // place_relay_buffers_on_core_for_wha0(router, unplaced_buffers);
    }
}

int assign_dram_subchannel(
    Router &router, 
    unique_id_t queue_buffer_id, 
    const std::vector<unique_id_t> &consumer_buffer_ids,
    const buda_SocDescriptor &soc_desc, 
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_available_read_from_dram_streams,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_extra_streams_available,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &noc_cores_total_l1_bytes_available,
    std::unordered_map<chip_id_t, std::unordered_map<int, std::unordered_map<int, int>>> &per_core_available_active_dram_queues,
    std::unordered_set<unique_id_t> &accounted_for_relay_buffers,
    std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>> &buffers_to_assign_to_cores
) {
    const auto &queue_buffer = router.get_buffer(queue_buffer_id);
    const auto &queue_info = queue_buffer.queue_info();
    chip_id_t chip = router.get_queue(queue_info.get_parent_queue_id()).target_device;
    
    auto unplaced_buffer_specs = get_unplaced_buffer_specs(router, consumer_buffer_ids, accounted_for_relay_buffers);
    TT_ASSERT(unplaced_buffer_specs.size() == 0 || unplaced_buffer_specs.front().required_bytes >= unplaced_buffer_specs.back().required_bytes);

    int dram_channel = queue_info.get_allocation_info().channel;
    auto subchannel_candidates = get_consumer_buffer_valid_subchannels(router, consumer_buffer_ids, dram_channel, queue_info.is_prolog());
    remove_subchannels_on_non_worker_rows(router, dram_channel, subchannel_candidates);
    
    auto unplaced_buffer_sizes = std::vector<int>{};
    auto unplaced_buffer_ids = std::vector<unique_id_t>{};
    std::transform(unplaced_buffer_specs.begin(), unplaced_buffer_specs.end(), std::back_inserter(unplaced_buffer_sizes), [](auto bs) { return bs.required_bytes; });
    std::transform(unplaced_buffer_specs.begin(), unplaced_buffer_specs.end(), std::back_inserter(unplaced_buffer_ids), [](auto bs) { return bs.id; });
    log_debug(tt::LogRouter, "Required L1 bytes: {}", fmt::join(unplaced_buffer_sizes, "B , "));

    const auto &[selected_subchannel, buffer_placements] = select_subchannel(
        router, dram_channel, chip, unplaced_buffer_specs, soc_desc, subchannel_candidates, 
        per_core_available_read_from_dram_streams, per_core_extra_streams_available, noc_cores_total_l1_bytes_available, per_core_available_active_dram_queues
    );

    if (selected_subchannel >= 0) {
        assign_subchannel_to_queue(router, queue_buffer_id, chip, dram_channel, selected_subchannel, soc_desc, buffer_placements,
            per_core_available_read_from_dram_streams, per_core_extra_streams_available, noc_cores_total_l1_bytes_available, accounted_for_relay_buffers
        );

        std::copy(buffer_placements.begin(), buffer_placements.end(), std::back_inserter(buffers_to_assign_to_cores));

        return selected_subchannel;
    }

    // Error reporting code -- if we get here, we couldn't find a viable solution
    log_error("Couldn't find valid subchannel to assign to for dram channel {} on chip {}. {} buffers require {} of space each. ids: {} ", dram_channel, chip, unplaced_buffer_sizes.size(), fmt::join(unplaced_buffer_sizes, " B , "), fmt::join(unplaced_buffer_ids,","));
    for (int subchannel : subchannel_candidates) {
        const tt_xy_pair &dram_core_xy = soc_desc.dram_cores.at(dram_channel).at(subchannel);
        int noc_row = dram_core_xy.y;
        // int available_dram_read_streams = per_core_available_read_from_dram_streams.at(chip).at(noc_row);
        // int available_extra_streams = per_core_extra_streams_available.at(chip).at(noc_row);
        // log_error("\tSubchannel DRAM read streams available: {}. Extra streams available {}", available_dram_read_streams, available_extra_streams);

        auto cores_dram_read_streams_available_ss = std::stringstream();
        auto cores_dram_queues_available_ss = std::stringstream();
        auto cores_extra_streams_available_ss = std::stringstream();
        auto cores_l1_available_ss = std::stringstream();
        cores_l1_available_ss << "chip=" << chip << ",row=" << noc_row << ": ";
        cores_dram_read_streams_available_ss << "chip=" << chip << ",row=" << noc_row << ": ";
        cores_dram_queues_available_ss << "chip=" << chip << ",row=" << noc_row << ": ";
        cores_extra_streams_available_ss << "chip=" << chip << ",row=" << noc_row << ": ";
        for (int noc_col = 0; noc_col < soc_desc.grid_size.x; noc_col++) {
            if (not core_supports_relay_buffers(soc_desc, tt_xy_pair(noc_col, noc_row))) {
                continue;
            }
            cores_l1_available_ss << "col=" << noc_col << ": " << noc_cores_total_l1_bytes_available.at(chip).at(noc_row).at(noc_col) << " B, ";
            cores_dram_read_streams_available_ss << "col=" << noc_col << ": " << per_core_available_read_from_dram_streams.at(chip).at(noc_row).at(noc_col) << " B, ";
            cores_dram_queues_available_ss << "col=" << noc_col << ": " << per_core_available_active_dram_queues.at(chip).at(noc_row).at(noc_col) << " B, ";
            cores_extra_streams_available_ss << "col=" << noc_col << ": " << per_core_extra_streams_available.at(chip).at(noc_row).at(noc_col) << " B, ";
        }
        log_error("\tCore L1 capacity available: {}", cores_l1_available_ss.str());
        log_error("\tCore DRAM read streams available: {}", cores_dram_read_streams_available_ss.str());
        log_error("\tCore extra streams available: {}", cores_extra_streams_available_ss.str());
        log_error("\tCore active DRAM queues available: {}", cores_dram_queues_available_ss.str());
    }
    log_fatal("Failed to insert relay buffers for WH A0");
    return -1;
}

tt_cxy_pair select_dram_subchan_core_with_least_buffers(router::Router &router, unique_id_t dram_buffer_id) {
    int channel = router.get_buffer(dram_buffer_id).queue_info().get_allocation_info().channel;
    const auto &valid_subchan_cores = router.get_soc_descriptor().dram_cores.at(channel);
    chip_id_t chip = router.get_buffer(dram_buffer_id).chip_location();

    auto compare_buffer_count = [&router, &chip](const tt_xy_pair &core1, const tt_xy_pair &core2) -> bool {
        return router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core1)).get_buffer_count() <
               router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core2)).get_buffer_count();
    };
    const auto least_used_dram_subchan_core =
        *std::min_element(valid_subchan_cores.begin(), valid_subchan_cores.end(), compare_buffer_count);

    return tt_cxy_pair(chip, least_used_dram_subchan_core);
}

void assign_dram_read_subchannels_for_wha0(Router &router, const std::vector<unique_id_t> &unplaced_relay_buffers) {
    const auto &soc_desc = router.get_soc_descriptor();
    TT_ASSERT (soc_desc.arch == tt::ARCH::WORMHOLE);

    auto accounted_for_relay_buffers = std::unordered_set<unique_id_t>();
    auto [per_core_available_read_from_dram_streams, per_core_extra_streams_available] = get_number_of_available_dram_read_streams_per_core(router);
    // auto per_core_available_active_dram_queues = get_number_of_available_active_dram_queues_per_core(router); // not an exact measure but perhaps good enough for now
    auto per_core_available_active_dram_queues = get_amount_available_resource_per_core(router, [](const HwCoreAttributes& core_attrs) -> int { return core_attrs.number_of_available_active_dram_queues(); } );
    log_available_resources_per_core(router, per_core_available_active_dram_queues, "DRAM Active Queues");
    auto noc_row_total_l1_bytes_available = get_amount_available_resource_per_core(router, [](const HwCoreAttributes& core_attrs) -> int { return core_attrs.available_l1_bytes(); } );
    log_available_resources_per_core(router, noc_row_total_l1_bytes_available, "L1 Bytes");
    // auto noc_row_total_l1_bytes_available = get_number_of_available_bytes_per_core(router); // not an exact measure but perhaps good enough for now
    const auto &queue_group_consumer_buffers = get_queue_group_consumer_buffers(router);

    for (const auto &[chip, core_xy_extra_streams_available] : per_core_extra_streams_available) {
        for (const auto &[row, col_extra_streams_available] : core_xy_extra_streams_available) {
            for (const auto &[col, extra_streams_available] : col_extra_streams_available) {
                const auto core_attrs = router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, col, row));
                // TT_ASSERT(core_attrs.available_l1_bytes() == noc_row_total_l1_bytes_available.at(chip).at(row).at(col)); // snijjar RE-ENABLE
                // TT_ASSERT(core_attrs.get_num_re== per_core_available_read_from_dram_streams.at(chip, row, col));
                TT_ASSERT(core_attrs.number_of_available_extra_streams() >= extra_streams_available);
            }
        }
    }

    auto unplaced_relay_buffers_set = std::unordered_set<unique_id_t>(unplaced_relay_buffers.begin(), unplaced_relay_buffers.end());
    auto buffers_assigned = std::unordered_set<unique_id_t>();
    // Counting too aggressively - need to only increment on read stream per pipe
    for (const auto &[grouped_queue_buffer_ids, consumer_buffer_ids] : queue_group_consumer_buffers) {
        bool subchannel_assigned = false;
        int assigned_subchannel = -1;
        std::vector<std::pair<unplaced_buffer_spec_t, tt_cxy_pair>> buffers_to_assign_to_cores = {};
        for (const auto &[chip, core_xy_l1_available] : noc_row_total_l1_bytes_available) {
            for (const auto &[row, col_l1_available] : core_xy_l1_available) {
                for (const auto &[col, l1_available] : col_l1_available) {
                    const auto core_attrs = router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, col, row));
                    // TT_ASSERT(core_attrs.available_l1_bytes() == noc_row_total_l1_bytes_available.at(chip).at(row).at(col)); // snijjar RE-ENABLE
                    // TT_ASSERT(core_attrs.get_num_re== per_core_available_read_from_dram_streams.at(chip, row, col));
                    if(!(core_attrs.number_of_available_extra_streams() >= per_core_extra_streams_available.at(chip).at(row).at(col))) {
                        log_error("Mismatch in internal book-keeping of extra streams during relay buffer insertion for WHA0 on core (c={},y={},x={})", chip, row, col);
                        TT_ASSERT(false);
                    }
                }
            }
        }
        for (unique_id_t queue_buffer_id : grouped_queue_buffer_ids) {


            buffers_assigned.insert(queue_buffer_id);
            unique_id_t base_buffer_id = router.is_buffer_scatter(queue_buffer_id) ? router.get_scatter_buffer_base_id(queue_buffer_id) : queue_buffer_id;
            const auto &base_buffer = router.get_buffer(base_buffer_id);
            int base_queue_buffer_channel = base_buffer.queue_info().get_allocation_info().channel;
            if (not subchannel_assigned) {
                int selected_subchannel = assign_dram_subchannel(
                    router, 
                    base_buffer_id, 
                    consumer_buffer_ids, 
                    soc_desc, 
                    per_core_available_read_from_dram_streams, 
                    per_core_extra_streams_available,
                    noc_row_total_l1_bytes_available, 
                    per_core_available_active_dram_queues,
                    accounted_for_relay_buffers,
                    buffers_to_assign_to_cores);
                assigned_subchannel = selected_subchannel;
                TT_ASSERT(assigned_subchannel >= 0);
                subchannel_assigned = true;

            }

            TT_ASSERT(subchannel_assigned && assigned_subchannel >= 0);
            const auto &location = tt_cxy_pair(base_buffer.chip_location(), soc_desc.dram_cores.at(base_queue_buffer_channel).at(assigned_subchannel));
            log_debug(tt::LogRouter, "Assigned buffer queue {} to subchannel {}", queue_buffer_id, assigned_subchannel);
            router.update_dram_queue_core_location(queue_buffer_id, location);

        }
        place_relay_buffers_on_core_for_wha0(router, buffers_to_assign_to_cores);
        buffers_to_assign_to_cores.clear();
    }

    TT_ASSERT(std::all_of(unplaced_relay_buffers.begin(), unplaced_relay_buffers.end(), [&accounted_for_relay_buffers](unique_id_t id) { return accounted_for_relay_buffers.find(id) != accounted_for_relay_buffers.end(); }));

    for (const auto &[buffer_id, router_buffer_info] : router.get_buffer_map()) {
        bool is_dram_queue = router.is_queue_buffer(buffer_id) &&
            router.get_queue(router_buffer_info.queue_info().get_parent_queue_id()).loc ==
                QUEUE_LOCATION::DRAM;
            bool is_final_output_buffer = router.get_buffer_output_pipes().find(buffer_id) == router.get_buffer_output_pipes().end();
        if (is_dram_queue && is_final_output_buffer) {
            TT_ASSERT(buffers_assigned.find(buffer_id) == buffers_assigned.end());
            const auto &router_selected_dram_core = select_dram_subchan_core_with_least_buffers(router, buffer_id);
            router.update_dram_queue_core_location(buffer_id, router_selected_dram_core);
            log_debug(tt::LogRouter, "Assigned buffer queue {} to core (c={},y={},x={})", buffer_id, router_selected_dram_core.chip, router_selected_dram_core.y, router_selected_dram_core.x);
        }
    }

    for (unique_id_t id : unplaced_relay_buffers) {
        TT_ASSERT(accounted_for_relay_buffers.find(id) != accounted_for_relay_buffers.end());
    }
}


void assign_dram_read_subchannels_and_place_relay_buffers_on_cores(Router &router, std::vector<unique_id_t> &unplaced_relay_buffers) {
    // For each queue - identify the consumers, if any of the consumers are already placed, assign to that subchannel otherwise, choose the subchannel closest
    // to the most relay buffer consumers

    // If this pass is called again, we don't need to choose subchannels
    bool dram_queues_need_subchannel_assignment = unplaced_relay_buffers.size() > 0;//false;
    for (const auto &[id, buf] : router.get_buffer_map()) {
        if (buf.is_queue() && !buf.core_location_assigned()) {
            dram_queues_need_subchannel_assignment = true;
            break;
        }
    }
    if (dram_queues_need_subchannel_assignment) {
        assign_dram_read_subchannels_for_wha0(router, unplaced_relay_buffers);
    }
    
    // place_relay_buffers_on_core_for_wha0(router, unplaced_relay_buffers);
}

std::unordered_set<unique_id_t> identify_queues_with_consumers_spanning_multiple_noc_rows(const Router &router) {
    auto queue_with_consumers_spanning_multiple_rows = std::unordered_set<unique_id_t>();
    auto queue_consumer_noc_rows = std::unordered_map<unique_id_t, std::unordered_set<int>>();

    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.is_queue())
            continue;
        
        
        unique_id_t base_id = router.is_buffer_scatter(id) ? router.get_scatter_buffer_base_id(id) : id;
        const auto &buffer_output_pipes = router.get_buffer_output_pipes();
        if (buffer_output_pipes.find(id) != buffer_output_pipes.end()) {
            for (auto out_pipe_id : buffer_output_pipes.at(id)) {
                for (auto buf_id : router.get_pipe(out_pipe_id).output_buffer_ids()) {
                    queue_consumer_noc_rows[base_id].insert(router.get_buffer_location(buf_id).y);
                }
            }
        }
    }

    for (const auto &[base_queue_id, consumer_noc_rows] : queue_consumer_noc_rows) {
        if (consumer_noc_rows.size() > 1) {
            queue_with_consumers_spanning_multiple_rows.insert(base_queue_id);
        }
    }

    return queue_with_consumers_spanning_multiple_rows;
}


void log_invalid_pipes_and_exit(const Router &router, const std::vector<unique_id_t> &invalid_pipes) {
    log_error("The following pipes that read from DRAM queues did not have their input queue conditions satisfied.");
    log_error("Expect all input queue buffers to be located on the same channel if pipe inputs or outputs are scatter buffers.");
    const auto &buffer_consumer_map = build_buffer_consumer_map(router);
    for (auto id : invalid_pipes) {
        report_failed_queue_channel_constraints(router, id, buffer_consumer_map);
    }
    log_fatal("Exiting.");
}

std::vector<unique_id_t> get_pipe_input_queues(const Router &router, const pipe_t &pipe) {
    std::vector<unique_id_t> input_queues = {};
    for (auto input_buffer_id : pipe.input_buffer_ids) {
        // TMs are done on packer side, so we know we won't have have to deal with a pipe that is input from DRAM that has multiple timesteps
        if (!pipe.has_multiple_timesteps() && router.is_queue_buffer(input_buffer_id)) {
            const auto &buffer = router.get_buffer(input_buffer_id);
            if (not buffer.queue_info().is_prolog()) {
                input_queues.push_back(input_buffer_id);
            }
        }
    }

    return input_queues;
}

/*
 * Wormhole a0 has a bug where DRAM reads from one noc row to a destination on another noc row has a potential to hang. This transformation
 * identifies these DRAM reads in the model, if they exist. FOr those DRAM reads, relay buffers are inserted somewhere on the same noc row
 * as an intermediate buffer between the DRAM buffer and the original consumer buffer, to avoid the hang
 */ 
void insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(Router &router) {
    auto invalid_pipes = std::vector<unique_id_t>(); // Used strictly for error reporting
    auto unplaced_relay_buffers = std::vector<unique_id_t>();

    const auto &queues_with_multiple_consumer_noc_rows = identify_queues_with_consumers_spanning_multiple_noc_rows(router);
    const auto &dram_channel_rows = dram_channel_rows_from_soc_descriptor(router.get_soc_descriptor());
    const auto pipe_list_copy = router.get_pipes();
    for (const auto &[pipe_id, pipe] : pipe_list_copy) {
        int idx = 0;
        std::vector<unique_id_t> input_queues = get_pipe_input_queues(router, pipe);

        if (input_queues.size() > 0) {
            bool input_queue_channel_constraints_satisfied = (not has_input_or_output_scatter_buffers(router, pipe)) || queues_on_same_channel(router, input_queues);
            if (not input_queue_channel_constraints_satisfied) {
                invalid_pipes.push_back(pipe_id);
            }

            if (invalid_pipes.size() == 0) {
                // Don't bother applying the transformation if we've found invalid pipes. If we've found invalid pipes, just look for the rest of them and report them
                const auto &inserted_relay_buffers = create_unplaced_relay_buffers_to_setup_all_dram_reads_to_be_on_same_row(
                    router, 
                    pipe_id, 
                    input_queues, 
                    idx++, 
                    dram_channel_rows, 
                    queues_with_multiple_consumer_noc_rows);
                std::copy(inserted_relay_buffers.begin(), inserted_relay_buffers.end(), std::back_inserter(unplaced_relay_buffers));

            }
        }
    }

    if (invalid_pipes.size() > 0) {
        log_invalid_pipes_and_exit(router, invalid_pipes);
    }

    log_debug(tt::LogRouter, "{} unplaced_relay_buffers", unplaced_relay_buffers.size());
    assign_dram_read_subchannels_and_place_relay_buffers_on_cores(router, unplaced_relay_buffers);

    validate_all_buffers_and_pipes_assigned_to_cores(router);
}

void assign_dram_subchan(router::Router &router) {
    // Note: this choice may be overridden by the final router_pass_op_queue_routing. 
    // In particular, it ignores the read_port and write_port fields in the netlist, 
    // and treats the buffer as having a single location for all access. 
    const char *enable_relay_bufs_env_var = std::getenv("WHA0_ENABLE_RELAY_BUFS");
    bool enable_relay_buffer_insertion = router.get_soc_descriptor().arch == tt::ARCH::WORMHOLE &&
        (enable_relay_bufs_env_var != nullptr &&
        std::stoi(enable_relay_bufs_env_var) != 0);

    tt::ARCH arch = router.get_soc_descriptor().arch;
    for (const auto &[buffer_id, router_buffer_info] : router.get_buffer_map()) {
        if (router.is_queue_buffer(buffer_id)) {
            unique_id_t parent_id = router_buffer_info.queue_info().get_parent_queue_id();
            bool no_real_associated_parent_queue = parent_id == router::NO_ASSOCIATED_QUEUE_OBJECT_ID;
            if (no_real_associated_parent_queue || router.get_queue(parent_id).loc == QUEUE_LOCATION::DRAM) {
                tt_cxy_pair router_selected_dram_core;
                if (arch == tt::ARCH::WORMHOLE && enable_relay_buffer_insertion) {
                    TT_ASSERT(
                        false, "DRAM subchannel assignment handled as part of relay buffer insertion pass for WH A0");
                } else {
                    router_selected_dram_core =
                        no_real_associated_parent_queue
                            ? tt_cxy_pair(
                                  router_buffer_info.chip_location(),
                                  router.get_soc_descriptor()
                                      .dram_cores.at(router_buffer_info.queue_info().get_allocation_info().channel)
                                      .at(0))
                            : select_dram_subchan_core_with_least_buffers(router, buffer_id);
                }
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

    if (arch != tt::ARCH::WORMHOLE  || !enable_relay_buffer_insertion) {
        validate_all_buffers_and_pipes_assigned_to_cores(router);
    }
}

};  // namespace router
