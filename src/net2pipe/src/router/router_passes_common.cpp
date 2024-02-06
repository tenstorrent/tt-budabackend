// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "router/router_passes_common.h"
#include "net2pipe.h"


#include <vector>
#include <unordered_map>
#include <sstream>
#include <string>
namespace router {

void validate_all_buffers_and_pipes_assigned_to_cores(const router::Router &router) {
    auto unassigned_buffers = std::vector<unique_id_t>();
    auto unassigned_pipes = std::vector<unique_id_t>();
    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.core_location_assigned()) {
            unassigned_buffers.push_back(id);
        }
    }
    for (const auto &[id, pipe] : router.get_pipes()) {
        if (not pipe.core_location_assigned()) {
            unassigned_pipes.push_back(id);
        }
        if (pipe.has_multiple_timesteps()) {
            TT_ASSERT(pipe.core_locations().size() == pipe.time_multiplexed_output_buffer_ids().size());
        } else {
            TT_ASSERT(pipe.core_locations().size() == 1);
        }
    }
    for ( auto id : unassigned_buffers) {
        log_error("Buffer {}  was not assigned a core location", id);
    }
    for (auto id : unassigned_pipes) {
        log_error("Pipe {} was not assigned a core location", id);
    }

    TT_ASSERT(unassigned_buffers.size() == 0 && unassigned_pipes.size() == 0);
}

void validate_all_buffers_assigned_to_streams(const router::Router &router) {
    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.is_queue()) {
            TT_ASSERT(buffer.info().stream_id() >= 0, "Buffer " + std::to_string(id) + " was not assigned a stream");
        }
    }
}

std::unordered_set<int> core_locations_to_rows(const std::vector<tt_cxy_pair> &core_locations) {
    std::unordered_set<int> rows = {};
    std::transform(
        core_locations.begin(), 
        core_locations.end(), 
        std::inserter(rows, rows.begin()), 
        [](const tt_cxy_pair &location) { return location.y; }
    );
    return rows;
}

bool is_forked_dram_buffer(const Router &router, unique_id_t buf_id) {
    const auto &buf = router.get_buffer(buf_id);
    bool buffer_has_output_pipes = router.get_buffer_output_pipes().find(buf_id) != router.get_buffer_output_pipes().end();
    bool is_forked_buffer = buffer_has_output_pipes && router.get_buffer_output_pipes().at(buf_id).size() > 1;
    return (buf.is_queue() && is_forked_buffer);
}

std::vector<unique_id_t> get_forked_dram_buffers(const Router &router) {
    auto forked_dram_buffers = std::vector<unique_id_t>{};

    for (const auto &[id, buf] : router.get_buffer_map()) {
        if (is_forked_dram_buffer(router,id)) {
            forked_dram_buffers.push_back(id);
        }
    }

    return forked_dram_buffers;
}

bool is_forked_buffer(const Router &router, unique_id_t buf_id) {
    bool buffer_has_output_pipes = router.get_buffer_output_pipes().find(buf_id) != router.get_buffer_output_pipes().end();
    bool is_forked_buffer = buffer_has_output_pipes && router.get_buffer_output_pipes().at(buf_id).size() > 1;
    return is_forked_buffer;
}

std::vector<unique_id_t> get_forked_buffers(const Router &router) {
    auto forked_buffers = std::vector<unique_id_t>{};

    for (const auto &[id, buf] : router.get_buffer_map()) {
        if (is_forked_buffer(router,id)) {
            forked_buffers.push_back(id);
        }
    }

    return forked_buffers;
}

tt_cxy_pair choose_location_for_buffer(
    const router::Router &router, 
    chip_id_t chip, 
    const std::vector<tt_cxy_pair> &preferred_locations,  
    int size_tiles, 
    DataFormat data_format, 
    bool include_tile_headers,
    const BufferAttributes &buffer_attrs,
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) {
    // For now we have a placeholder algorithm for determining the best core to insert on. In the future we may want to have something more optimal.
    // Placeholder: Prefer to gather on one of the input cores, then try gather on any worker core that can support it, then try gather on any ethernet core
    for (const auto &preferred_location : preferred_locations) {
        const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(preferred_location);
        if (core_attrs.can_add_buffer(size_tiles, data_format, include_tile_headers, buffer_attrs, ignorable_allocation_failure_reasons)) {
            return preferred_location;
        }
    }

    std::optional<tt_cxy_pair> first_available_core;
    std::optional<tt_cxy_pair> first_available_core_without_op;

    for (const tt_xy_pair &worker_xy : router.get_soc_descriptor().workers) {
        const auto &worker_chip_location = tt_cxy_pair(chip, worker_xy);
        const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(worker_chip_location);
        if (core_attrs.can_add_buffer(size_tiles, data_format, include_tile_headers, buffer_attrs, ignorable_allocation_failure_reasons)) {
            bool core_has_extra_streams = core_attrs.get_used_extra_streams() > 0;
            bool core_has_op = core_attrs.get_input_buffers().size() > 0;
            if (!core_has_extra_streams && !core_has_op) {
                return worker_chip_location;
            }
            if (!first_available_core_without_op.has_value() && !core_has_op) {
                first_available_core_without_op = worker_chip_location;
            }
            if (!first_available_core.has_value()) {
                first_available_core = worker_chip_location;
            }
        }
    }

    if (first_available_core_without_op.has_value()) {
        return first_available_core_without_op.value();
    }
    if (first_available_core.has_value()) {
        return first_available_core.value();
    }

    for (const tt_xy_pair &ethernet_xy : router.get_soc_descriptor().ethernet_cores) {
        const auto &ethernet_chip_location = tt_cxy_pair(chip, ethernet_xy);
        const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(ethernet_chip_location);
        if (core_attrs.can_add_buffer(size_tiles, data_format, include_tile_headers, buffer_attrs, ignorable_allocation_failure_reasons)) {
            return ethernet_chip_location;
        }
    }

    TT_ASSERT(false, "Couldn't find viable core to perform gather on");
    return {0,0,0};
}

tt_cxy_pair choose_location_for_buffer(
    const router::Router &router, 
    chip_id_t chip,  
    const std::vector<tt_cxy_pair> &preferred_locations, 
    unique_id_t buffer_id,
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) {
    return choose_location_for_buffer(
        router, 
        chip, 
        preferred_locations, 
        router.get_buffer(buffer_id).info().allocated_size_in_tiles(), 
        router.get_buffer_data_format(buffer_id), 
        router.is_buffer_tilized(buffer_id),
        router.identify_buffer_attributes(buffer_id),
        ignorable_allocation_failure_reasons
    );
}


tt_cxy_pair choose_location_for_gather_buffer(
    const router::Router &router, chip_id_t chip, 
    const std::vector<tt_cxy_pair> &input_buf_locations, 
    const tt_cxy_pair &dest_location, 
    unique_id_t buffer_id
) {
    return choose_location_for_buffer(router, chip, input_buf_locations, buffer_id, {BufferAllocationFailureReason::Insufficient_Ethernet_Streams});
}

tt_cxy_pair choose_location_for_multicast_buffer(
    const router::Router &router, 
    chip_id_t chip, 
    const tt_cxy_pair &src_location, 
    const std::vector<tt_cxy_pair> &dest_buf_locations, 
    unique_id_t buffer_id
) {
    return choose_location_for_buffer(router, chip, dest_buf_locations, buffer_id, {BufferAllocationFailureReason::Insufficient_Ethernet_Streams});
}


tt::buffer_info create_relay_buffer_info(int stream_id, int num_allocated_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_allocated_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return tt::buffer_info(RouterBufferType::Relay, stream_id, 1, num_allocated_tiles, scatter_gather_num_tiles, num_epoch_tiles, 0, data_format, tile_dims, false);
}

tt::buffer_info create_relay_buffer_info(int num_allocated_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_allocated_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return tt::buffer_info(RouterBufferType::Relay, -1, 1, num_allocated_tiles, scatter_gather_num_tiles, num_epoch_tiles, 0, data_format, tile_dims, false);
}

router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, std::variant<tt_cxy_pair,chip_id_t> location) {
    if (std::holds_alternative<tt_cxy_pair>(location)) {
        return create_mutable_relay_buffer(num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims, std::get<tt_cxy_pair>(location));
    } else {
        return create_mutable_relay_buffer(num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims, std::get<chip_id_t>(location));
    }
}

router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, chip_id_t chip) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return router_buffer_info_t::create_mutable(chip, create_relay_buffer_info(num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims));
}

router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return router_buffer_info_t::create_mutable(location, create_relay_buffer_info(num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims));
}

router_buffer_info_t create_immutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return router_buffer_info_t::create_immutable(location, create_relay_buffer_info(num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims));
}

router_buffer_info_t create_mutable_relay_buffer_with_explicit_stream(int stream_id, int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location) {
    TT_ASSERT(num_epoch_tiles > 0);
    TT_ASSERT(num_tiles > 0);
    TT_ASSERT(data_format != DataFormat::Invalid);
    return router_buffer_info_t::create_mutable(location, create_relay_buffer_info(stream_id, num_tiles, scatter_gather_num_tiles, num_epoch_tiles, data_format, tile_dims));
}

static std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> get_intermediate_and_output_buffers_per_core(const Router &router) {
    auto output_and_intermediate_core_buffers = std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>{};
    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.is_queue()) {
            bool is_scatter_buffer_but_not_base_id = router.is_buffer_scatter(id) and router.get_scatter_buffer_base_id(id) != id;
            bool is_output_or_intermediate = buffer.info().type() == RouterBufferType::Output || buffer.info().type() == RouterBufferType::Intermediate;
            if (is_output_or_intermediate and !is_scatter_buffer_but_not_base_id) {
                output_and_intermediate_core_buffers[buffer.core_location()].push_back(id);
            }
        }
    }

    return output_and_intermediate_core_buffers;
}

static std::unordered_map<unique_id_t, std::string> build_buffer_op_map(const Router &router) {
    auto op_buffer_map = std::unordered_map<unique_id_t, std::string>{};

    for (const auto &[name, op_operand_grid_buffers] : router.get_op_input_buf_map()) {
        for (const auto &[i, grid_buffers] : op_operand_grid_buffers) {
            for (const auto &[j, buffers] : grid_buffers) {
                for (const auto &[k, buffer_id] : buffers) {
                    op_buffer_map[buffer_id] = name;
                }
            }
        }
    }
    for (const auto &[name, op_index_grid_buffers] : router.get_op_intermediate_buf_map()) {
        for (const auto &[i, grid_buffers] : op_index_grid_buffers) {
            for (const auto &[j, buffers] : grid_buffers) {
                for (const auto &[k, buffer_id] : buffers) {
                    op_buffer_map[buffer_id] = name;
                }
            }
        }
    }
    for (const auto &[name, op_grid_buffers] : router.get_op_output_buf_map()) {
        for (const auto &[i, grid_buffers] : op_grid_buffers) {
            for (const auto &[j, buffer_id] : grid_buffers) {
                op_buffer_map[buffer_id] = name;
            }
        }
    }

    return op_buffer_map;
}

void add_output_and_intermediate_buffer_l1_contribution_at_core(
    const router_buffer_info_t &buffer,
    const Router &router,
    const std::unordered_map<router::unique_id_t, std::string> &buffer_op_map,
    std::unordered_map<tt_cxy_pair, int> &core_l1_mem_usage,
    const std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> &output_and_intermediate_buffers_per_core,
    std::unordered_set<tt_cxy_pair> &cores_processed_for_output_intermediate_buffers
) {
    auto buffer_size = [&router](unique_id_t id) -> int {
        TT_ASSERT(!router.is_queue_buffer(id));
        return router.get_buffer(id).info().allocated_size_in_bytes();
    };

    const auto &core_location = buffer.core_location();
    const auto &output_intermediate_buffers = output_and_intermediate_buffers_per_core.at(core_location);
    
    bool already_counted = cores_processed_for_output_intermediate_buffers.find(core_location) != cores_processed_for_output_intermediate_buffers.end();
    if (!already_counted) {
        cores_processed_for_output_intermediate_buffers.insert(core_location);
        const auto &owning_op_name = buffer_op_map.at(output_intermediate_buffers.at(0));
        const auto &owning_op = router.get_op_info_map().at(owning_op_name);
        bool op_intermediate_and_output_buffers_overlap_in_l1 = n2p::op_output_buffer_shared_with_intermediate(owning_op);
        
        if (!op_intermediate_and_output_buffers_overlap_in_l1) {
            for (auto id : output_intermediate_buffers) {
                int allocated_size = buffer_size(id);
                core_l1_mem_usage[core_location] += allocated_size;

                log_debug(
                    tt::LogRouter,
                    "Adding buffer {} of op to to core: (c={},y={},x={}), with allocation size {} B",
                    id,
                    core_location.chip,
                    core_location.y,
                    core_location.x,
                    allocated_size
                );
            }

        } else {
            // otherwise, if it's not a fused op, then intermediate and output buffers can overlap
            int allocated_size = 0;
            for (auto id : output_intermediate_buffers) {
                allocated_size = std::max(allocated_size, buffer_size(id));
            }
            
            core_l1_mem_usage[core_location] += allocated_size;

            auto ids_ss = std::stringstream();
            for (auto id : output_intermediate_buffers) {
                ids_ss << id << ",";
            }
            log_debug(
                tt::LogRouter,
                "Adding overlapping intermediate and output buffers ({}) to to core: (c={},y={},x={}), with allocation size {} B",
                ids_ss.str(),
                core_location.chip,
                core_location.y,
                core_location.x,
                allocated_size
            );
        }
    }
}


std::unordered_map<tt_cxy_pair, int> compute_l1_use_from_buffers(const Router &router) {

    log_debug(tt::LogRouter, "compute_l1_use_from_buffers");
    const auto &output_and_intermediate_buffers_per_core = get_intermediate_and_output_buffers_per_core(router);
    const auto &buffer_op_map = build_buffer_op_map(router);
    std::unordered_set<tt_cxy_pair> cores_processed_for_output_intermediate_buffers;
    auto core_l1_mem_usage = std::unordered_map<tt_cxy_pair, int>{};

    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.is_queue()) {
            bool is_scatter_buffer_but_not_base_id = router.is_buffer_scatter(id) and router.get_scatter_buffer_base_id(id) != id;
            if (is_scatter_buffer_but_not_base_id) {
                continue;
            }

            // There is a potential for overlap between output and intermediate buffers so we treat them specially
            bool is_output_or_intermediate = buffer.info().type() == tt::RouterBufferType::Intermediate || buffer.info().type() == tt::RouterBufferType::Output;
            if(is_output_or_intermediate) {
                add_output_and_intermediate_buffer_l1_contribution_at_core(
                    buffer,
                    router,
                    buffer_op_map,
                    core_l1_mem_usage,
                    output_and_intermediate_buffers_per_core,
                    cores_processed_for_output_intermediate_buffers
                );
                
            } else {
                int size_bytes = buffer.info().allocated_size_in_bytes();
                core_l1_mem_usage[buffer.core_location()] += size_bytes;

                log_debug(
                    tt::LogRouter,
                    "Adding buffer {} of size {} B to buffer count at core: (c={},y={},x={})",
                    id,
                    size_bytes,
                    buffer.core_location().chip,
                    buffer.core_location().y,
                    buffer.core_location().x
                );
            }
        }
    }

    return core_l1_mem_usage;
}

std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> get_cores_used_by_buffers(const Router &router) {
    auto core_buffers = std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>{};

    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (not buffer.is_queue()) {
            core_buffers[buffer.core_location()].push_back(id);
        }
    }

    return core_buffers;
}

};