// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "validators.h"
#include "common/model/assert.hpp"
#include "net2pipe.h"
#include "router_types.h"
#include "router/router_passes_common.h"
#include "size_lib.hpp"

using router::unique_id_t;
namespace validate {

void unique_id_range_covers_all_buffers(
    std::map<std::uint64_t, router::router_buffer_info_t> const &buffer_map,
    std::unordered_map<std::uint64_t, tt_queue_info> const &queue_unique_id_info_map) {
    auto buffer_size_in_tiles = [&queue_unique_id_info_map](router::router_buffer_info_t const &buf) {
        if (buf.is_queue()) {
            unique_id_t parent_queue_id = buf.queue_info().get_parent_queue_id();
            if (parent_queue_id != router::NO_ASSOCIATED_QUEUE_OBJECT_ID) {
                tt_queue_info const &queue_info = queue_unique_id_info_map.at(parent_queue_id);
                tt_dim_info const &dims = queue_info.dim;
                return tt::size::get_entry_size_in_tiles(
                    dims.ublock_ct, dims.ublock_rt, dims.mblock_m, dims.mblock_n, dims.t);
            } else {
                return 0;
            }
        } else {
            return buf.info().size_in_tiles();
        }
    };

    for (const auto &[id, buf] : buffer_map) {
        int size_in_tiles = buffer_size_in_tiles(buf);
        bool buffer_representable_with_unique_id = size_in_tiles <= n2p::UNIQUE_ID_ALIGN;
        if (!buffer_representable_with_unique_id) {
            ERROR(
                "Buffer " << id << " is too large to be represented with a unique id. It requires " << size_in_tiles
                          << " but UNIQUE_ID_ALIGN is only " << n2p::UNIQUE_ID_ALIGN);
        }
    }
}

bool netlist_fits_on_arch(const netlist_parser &netlist, const buda_SocDescriptor &soc_descriptor) {
    
    for (const auto &[name, graph] : netlist.graph_map) {
        for (const auto &[op_name, op_info] : graph.op_map) {
            if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
                if (op_info.attributes.ethernet_datacopy_attr.egress_channels.size() > 0) {
                    // ethernet datacopy ops don't live on the tensix grid so we can only check if the specified
                    // ethernet channels exist
                    // (... if the ethernet channels are explicitly specified on the op; they don't have to be)
                    if (std::any_of(
                            op_info.attributes.ethernet_datacopy_attr.egress_channels.begin(),
                            op_info.attributes.ethernet_datacopy_attr.egress_channels.end(),
                            [&](const auto &egress_channel) {
                                return egress_channel >= soc_descriptor.ethernet_cores.size();
                            })) {
                        return false;
                    }
                    if (std::any_of(
                            op_info.attributes.ethernet_datacopy_attr.ingress_channels.begin(),
                            op_info.attributes.ethernet_datacopy_attr.ingress_channels.end(),
                            [&](const auto &ingress_channel) {
                                return ingress_channel >= soc_descriptor.ethernet_cores.size();
                            })) {
                        return false;
                    }
                }
            } else {
                bool exceeds_grid_x_bound = soc_descriptor.worker_grid_size.x < (uint32_t) (op_info.grid_loc_x() + op_info.grid_size_x());
                bool exceeds_grid_y_bound = soc_descriptor.worker_grid_size.y < (uint32_t) (op_info.grid_loc_y() + op_info.grid_size_y());
                bool exceeds_grid_bounds = exceeds_grid_x_bound || exceeds_grid_y_bound;
                if (exceeds_grid_bounds) {
                    return false;
                }
            }
        }
    }

    const uint32_t num_channels = soc_descriptor.dram_cores.size();
    for (const auto &[name, queue] : netlist.queue_map) {
        for (const auto &allocation : queue.alloc_info) {
            if (queue.loc == QUEUE_LOCATION::DRAM) {
                bool arch_has_dram_channel = allocation.channel < num_channels;
                if (!arch_has_dram_channel) {
                    return false;
                }
            }
        }

    }

    return true;
}

void validate_netlist_fits_on_arch(const netlist_parser &netlist, const buda_SocDescriptor &soc_descriptor) {
    bool netlist_valid = netlist_fits_on_arch(netlist, soc_descriptor);

    if (!netlist_valid) {
        TT_ASSERT(false, "Provided netlist is invalid - it does not fit on to the specified device. If this is versim, consider using FORCE_FULL_SOC_DESC=1 to avoid reduced soc_desc.");
    }
}


void validate_netlist_fits_on_cluster(const netlist_parser &netlist, const tt_ClusterDescriptor &cluster_descriptor) {
    auto target_devices = std::unordered_set<chip_id_t>();
    const auto &available_devices = cluster_descriptor.get_all_chips();
    auto invalid_queues = std::vector<std::string>();
    auto invalid_graphs = std::vector<std::string>();
    for (const auto &[name, queue] : netlist.queue_map) {
        if (available_devices.find(queue.target_device) == available_devices.end()) {
            invalid_queues.push_back(name);
        }
    }

    for (const auto &[name, graph] : netlist.graph_map) {
        if (available_devices.find(graph.target_device) == available_devices.end()) {
            invalid_graphs.push_back(name);
        }
    }


    bool netlist_invalid = invalid_queues.size() > 0 || invalid_graphs.size() > 0;
    if (netlist_invalid) {
        auto err_ss = std::stringstream();
        err_ss << "Available devices: {";
        for (auto c : available_devices) {
            err_ss << c << ",";
        }
        err_ss << "}\n";

        err_ss << "Queues on invalid devices: {\n";
        for (auto q_name : invalid_queues) {
            err_ss << "\t" << q_name << " on device " << netlist.queue_map.at(q_name).target_device << "\n";
        }
        err_ss << "}\n";

        err_ss << "Graphs on invalid devices: {\n";
        for (auto graph_name : invalid_graphs) {
            err_ss << "\t" << graph_name << " on device " << netlist.graph_map.at(graph_name).target_device << "\n";
        }
        err_ss << "}\n";


        log_fatal("Provided netlist is invalid - it does not fit on to the specified cluster. \n{}", err_ss.str());
    }
}




}; // namespace validator

void Net2Pipe::post_router_validate_all_pipes_placed_on_same_chip_as_inputs_and_outputs(const temporal_epoch_context& epoch_context) const {
    if (this->soc_descriptor->ethernet_cores.size() == 0) {
        return;
    }
    
    for (const auto &[pipe_id, pipe] : epoch_context.pipes) {
        const auto pipe_chip = pipe.has_multiple_timesteps() ? pipe.scatter_segment_core_location(0).chip : pipe.core_location().chip;
        
        if (pipe.has_multiple_timesteps()) {
            int num_timesteps = pipe.time_multiplexed_output_buffer_ids().size();
            for (int i = 0; i < num_timesteps; i++) {
                TT_ASSERT(pipe.scatter_segment_core_location(i).chip == pipe_chip);
            }
        }

        if (!is_unicast_pipe(pipe)) {
            for (const auto id : pipe.input_buffer_ids) {
                const auto &in_buf = epoch_context.buffer_map.at(id);
                TT_ASSERT(pipe_chip == in_buf.core_location().chip);
            }
        }
        
        if (pipe.has_multiple_timesteps()) {
            for (const auto &out_buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
                for (const auto id : out_buf_ids) {
                    const auto &in_buf = epoch_context.buffer_map.at(id);
                    TT_ASSERT(pipe_chip == in_buf.core_location().chip);
                }
            }
        } else {
            for (const auto id : pipe.output_buffer_ids()) {
                const auto &out_buf = epoch_context.buffer_map.at(id);
                if (out_buf.is_queue()) {
                    const auto &queue_info = epoch_context.queue_unique_id_info_map.at(out_buf.queue_info().get_parent_queue_id());
                    TT_ASSERT((queue_info.loc == QUEUE_LOCATION::HOST) || (pipe_chip == out_buf.core_location().chip));
                } else {
                    TT_ASSERT(pipe_chip == out_buf.core_location().chip);
                }
            }
        }
    }
}

void validate::l1_usage_in_sync(const Router &router) {
    auto core_l1_mem_usage = compute_l1_use_from_buffers(router);
    
    bool error_in_reported_memory_usage = false;
    for (const auto &[core_loc, used_bytes] : core_l1_mem_usage) {
        const auto &core_attrs = router.get_cluster_resource_model().get_core_attributes(core_loc);
        int core_attrs_used_bytes = core_attrs.get_num_allocated_bytes();
        TT_ASSERT(core_attrs.available_l1_bytes() >= 0);
        if (used_bytes != core_attrs_used_bytes) {
            error_in_reported_memory_usage = true;
            log_error("Num allocated bytes in core resource tracker for core (c={},y={},x={}) shows {}B used, which mismatches the counted total of {}B from all the buffers on the core", core_loc.chip, core_loc.y, core_loc.x, core_attrs_used_bytes, used_bytes);
        }
    }
    if (error_in_reported_memory_usage) {
        log_fatal("Core resource tracker has memory use out of sync w/ the actual buffers mapped to that core");
    }
}

