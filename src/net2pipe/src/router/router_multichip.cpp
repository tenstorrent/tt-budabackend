// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "buda_soc_descriptor.h"
#include "device/tt_xy_pair.h"
#include "netlist_utils.hpp"
#include "common/tt_cluster_graph.hpp"

#include "router/router_passes.h"
#include "router/router_multichip_routing_algorithms.h"
#include "router.hpp"
#include "router_types.h"
#include "router/router_passes_common.h"

#include "net2pipe_common.h"
#include "device/tt_cluster_descriptor.h"
#include "tt_core.hpp"
#include "utils/logger.hpp"

#include <boost/functional/hash.hpp>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <sstream>
#include <unordered_map>

using tt::ClusterGraph;
namespace router {

template <typename Container> // we can make this generic for any container [1]
struct unique_ids_hasher {
    std::size_t operator()(Container const& c) const {
        return boost::hash_range(std::begin(c), std::end(c));
    }
};

void print_pipe_verbose(const Router &router, unique_id_t id, tt::Logger::Level log_level) {
    const auto &pipe = router.get_pipes().at(id);

    // TODO(snijjar): Update to support pipes without assigned locations
    if (pipe.core_location_assigned()) {
        if (pipe.has_multiple_timesteps()) {
            log_custom(log_level, tt::LogRouter, "PIPE {}: locations[0]:(chip={},y={},x={})", id, pipe.scatter_segment_core_location(0).chip, pipe.scatter_segment_core_location(0).y, pipe.scatter_segment_core_location(0).x);
        } else {
            log_custom(log_level, tt::LogRouter, "PIPE {}: locations[0]:(chip={},y={},x={})", id, pipe.core_location().chip, pipe.core_location().y, pipe.core_location().x);
        }
    } else {
        log_custom(log_level, tt::LogRouter, "PIPE {}: <no assigned location yet>", id);
    }
    log_custom(log_level, tt::LogRouter, "\tInputs: ");
    for (unique_id_t buf_id : pipe.input_buffer_ids) {
        const auto &buf = router.get_buffer_map().at(buf_id);
        if (buf.core_location_assigned()) {
            const auto &loc = buf.core_location();
            log_custom(log_level, tt::LogRouter, "\t\t{} - location: (chip={},y={},x={}) ", buf_id, loc.chip, loc.y, loc.x);
        } else {
            log_custom(log_level, tt::LogRouter, "\t\t{} - location: (unplaced)(chip={}) ", buf_id, buf.chip_location());
        }
    }
    log_custom(log_level, tt::LogRouter, "\tOutputs: ");
    if (pipe.has_multiple_timesteps()) {
        for (const auto &buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
            log_custom(log_level, tt::LogRouter, "\t\t[");
            for (unique_id_t buf_id : buf_ids) {
                const auto &buf = router.get_buffer_map().at(buf_id);
                if (buf.core_location_assigned()) {
                    const auto &loc = buf.core_location();
                    log_custom(log_level, tt::LogRouter, "\t\t\t{} - location: (chip={},y={},x={}) ", buf_id, loc.chip, loc.y, loc.x);
                } else {
                    log_custom(log_level, tt::LogRouter, "\t\t\t{} - location: (unplaced)(chip={}) ", buf_id, buf.chip_location());
                }
            }
            log_custom(log_level, tt::LogRouter, "\t\t]");
        }
    } else {
        for (unique_id_t buf_id : pipe.output_buffer_ids()) {
            const auto &buf = router.get_buffer_map().at(buf_id);
            if (buf.core_location_assigned()) {
                const auto &loc = buf.core_location();
                log_custom(log_level, tt::LogRouter, "\t\t{} - location: (chip={},y={},x={}) ", buf_id, loc.chip, loc.y, loc.x);
            } else {
                log_custom(log_level, tt::LogRouter, "\t\t{} - location: (unplaced)(chip={}) ", buf_id, buf.chip_location());
            }
        }
    }
}


template <class T>
bool all_buffers_on_same_chip(const router::Router &router, const T &buffer_ids) {
    if (buffer_ids.size() == 0) {
        return true;
    }

    auto chip = router.get_buffer_map().at(*std::begin(buffer_ids)).chip_location();
    return std::all_of(
        std::begin(buffer_ids), 
        std::end(buffer_ids), 
        [&router, &chip](unique_id_t buf_id) -> bool { return router.get_buffer_map().at(buf_id).chip_location() == chip; }
    );
}

template <class T>
bool all_buffers_on_specified_chip(const router::Router &router, const T &buffer_ids, chip_id_t chip) {
    if (buffer_ids.size() == 0) {
        return true;
    }

    return std::all_of(
        std::begin(buffer_ids), 
        std::end(buffer_ids), 
        [&router, &chip](unique_id_t buf_id) -> bool { return static_cast<int>(router.get_buffer_map().at(buf_id).chip_location()) == chip; }
    );
}

std::optional<tt_cxy_pair> choose_valid_pipe_and_buffer_location_from_restricted_core_set(const Router &router, pipe_segment_id_t const& pipe_segment_id, unique_id_t buffer_id, const std::vector<tt_cxy_pair> &core_candidates, bool balanced_mode = false) {
    std::vector<tt_cxy_pair> valid_cores = {};
    for (const auto &c : core_candidates) {
        if (router.can_assign_connected_pipe_and_buffer_to_core(pipe_segment_id, buffer_id, c)) {
            if (!balanced_mode) {
                return c;
            }
            valid_cores.push_back(c);
        }
    }

    if (valid_cores.size()) {
        std::sort(valid_cores.begin(), valid_cores.end(), [&router](tt_cxy_pair &a, tt_cxy_pair &b) { return router.get_cluster_resource_model().get_core_attributes(a).available_l1_bytes() < router.get_cluster_resource_model().get_core_attributes(b).available_l1_bytes(); });
        TT_ASSERT(router.get_cluster_resource_model().get_core_attributes(valid_cores.front()).available_l1_bytes() <= router.get_cluster_resource_model().get_core_attributes(valid_cores.back()).available_l1_bytes(), "Sorted wrong order.");
        return valid_cores.front();
    } else {
        return std::nullopt;
    }
}

// Overlap between preferred_cores and invalid_cores currently is undefined behaviour
std::optional<tt_cxy_pair> choose_valid_pipe_and_buffer_location(const Router &router, pipe_segment_id_t const& pipe_segment_id, unique_id_t buffer_id, chip_id_t chip, const std::vector<tt_cxy_pair> &preferred_cores, const std::unordered_set<tt_cxy_pair> &invalid_cores = {}, bool balanced_mode = false) {
    const auto &solution = choose_valid_pipe_and_buffer_location_from_restricted_core_set(router, pipe_segment_id, buffer_id, preferred_cores, balanced_mode);    
    if (solution.has_value()) {
        return solution;
    }

    const auto &tried_cores = std::unordered_set<tt_cxy_pair>(preferred_cores.begin(), preferred_cores.end());
    std::vector<tt_cxy_pair> other_cores_to_try;
    for (const auto &c : router.get_soc_descriptor(chip).workers) {
        const auto &core = tt_cxy_pair(chip, c.x, c.y);
        if (invalid_cores.find(core) == invalid_cores.end() && tried_cores.find(core) == tried_cores.end()) {
            other_cores_to_try.push_back(core);
        }
    }

    return choose_valid_pipe_and_buffer_location_from_restricted_core_set(router, pipe_segment_id, buffer_id, other_cores_to_try, balanced_mode);
}

std::optional<tt_cxy_pair> choose_valid_pipe_location_from_restricted_core_set(const Router &router, pipe_segment_id_t const& pipe_segment_id, const std::vector<tt_cxy_pair> &core_candidates, bool balanced_mode = false) {
    std::unordered_set<tt_cxy_pair> visited = {};
    std::vector<tt_cxy_pair> valid_cores = {};
    for (const auto &c : core_candidates) {
        if(visited.insert(c).second == false) continue;
        if (router.can_assign_connected_pipe_to_core(pipe_segment_id, c)) {
            if (!balanced_mode) {
                return c;
            }
            valid_cores.push_back(c);
        }
    }

    if (valid_cores.size()) {
        std::sort(valid_cores.begin(), valid_cores.end(), [&router](tt_cxy_pair &a, tt_cxy_pair &b) { return router.get_cluster_resource_model().get_core_attributes(a).available_l1_bytes() < router.get_cluster_resource_model().get_core_attributes(b).available_l1_bytes(); });
        TT_ASSERT(router.get_cluster_resource_model().get_core_attributes(valid_cores.front()).available_l1_bytes() <= router.get_cluster_resource_model().get_core_attributes(valid_cores.back()).available_l1_bytes(), "Sorted wrong order.");
        return valid_cores.front();
    } else {
        return std::nullopt;
    }

}

// Overlap between preferred_cores and invalid_cores currently is undefined behaviour
std::optional<tt_cxy_pair> choose_valid_pipe_location(const Router &router, pipe_segment_id_t const& pipe_segment_id, chip_id_t chip, const std::vector<tt_cxy_pair> &preferred_cores, const std::unordered_set<tt_cxy_pair> &invalid_cores = {}, bool balanced_mode = false) {
    const auto &solution = choose_valid_pipe_location_from_restricted_core_set(router, pipe_segment_id, preferred_cores, balanced_mode);    
    if (solution.has_value()) {
        return solution;
    }

    const auto &tried_cores = std::unordered_set<tt_cxy_pair>(preferred_cores.begin(), preferred_cores.end());
    std::vector<tt_cxy_pair> other_cores_to_try;
    for (const auto &c : router.get_soc_descriptor(chip).workers) {
        const auto &core = tt_cxy_pair(chip, c.x, c.y);
        if (invalid_cores.find(core) == invalid_cores.end() && tried_cores.find(core) == tried_cores.end()) {
            other_cores_to_try.push_back(core);
        }
    }

    return choose_valid_pipe_location_from_restricted_core_set(router, pipe_segment_id, other_cores_to_try, balanced_mode);
}


bool is_chip_to_chip_pipe(const pipe_t &pipe, const router::Router &router) {
    std::unordered_set<chip_id_t> input_chips = {};

    for (const auto buf_id : pipe.input_buffer_ids) {
        input_chips.insert(router.get_buffer_map().at(buf_id).chip_location());
    }
    
    if (pipe.has_multiple_timesteps()) {
        for (const auto &timestep_buffers : pipe.time_multiplexed_output_buffer_ids()) {
            for (const auto buf_id : timestep_buffers) {
                const auto &buffer = router.get_buffer_map().at(buf_id);
                if (input_chips.find(buffer.chip_location()) == input_chips.end()) {
                    return true;
                }
            }
        }
    } else {
        for (const auto buf_id : pipe.output_buffer_ids()) {
            const auto &buffer = router.get_buffer_map().at(buf_id);
            if (input_chips.find(buffer.chip_location()) == input_chips.end()) {
                return true;
            }
        }

    }
    
    return false;
}

void emit_chip_to_chip_pipe_ops(std::ostream &os, const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids) {

    auto buffer_producer_names = std::unordered_map<unique_id_t, std::string>{};
    for (const auto &[op_name, buffer_grid] : router.get_op_output_buf_map()) {
        for (const auto &[row, buffer_row] :  buffer_grid) {
            for (const auto &[col, buffer_id] :  buffer_row) {
                buffer_producer_names[buffer_id] = op_name;
            }
        }
    }

    auto op_consumer_names = std::unordered_map<std::string, std::unordered_set<std::string>>{};

    for (auto id : chip_to_chip_pipe_ids) {
        const auto &pipe = router.get_pipe(id);
        auto producer_buffer_id = pipe.input_buffer_ids.at(0);
        if (router.is_buffer_scatter(producer_buffer_id)) {
            producer_buffer_id = router.get_scatter_buffer_base_id(producer_buffer_id);
        }
        const auto &producer_buffer = router.get_buffer(producer_buffer_id);
        const auto &producer_name = 
            producer_buffer.is_queue()          ? router.get_queue(producer_buffer.queue_info().get_parent_queue_id()).name : 
            producer_buffer.info().is_scatter() ? buffer_producer_names.at(router.get_scatter_buffer_base_id(producer_buffer_id)) :
            buffer_producer_names.find(producer_buffer_id) == buffer_producer_names.end() ? std::to_string(producer_buffer_id) : buffer_producer_names.at(producer_buffer_id);
        op_consumer_names[producer_name].insert(pipe.consumer_name());
    }

    for (const auto &[producer_name, consumers] : op_consumer_names) {
        os << producer_name << " ---> " << consumers.size() << " consumers: {";
        for (const auto &c : consumers) {
            os << c << ", ";
        }
        os << "}\n";
    }

}


std::vector<unique_id_t> collect_chip_to_chip_pipes(const router::Router &router) {
    std::vector<unique_id_t> chip_to_chip_pipes = {};

    for (const auto &[id, p]: router.get_pipes()) {
        bool is_chip_to_chip = is_chip_to_chip_pipe(p, router);
        if (is_chip_to_chip) {
            chip_to_chip_pipes.push_back(id);
        }
    }

    return chip_to_chip_pipes;
}

DataFormat get_buffer_data_format(const router::Router &router, unique_id_t buffer_id) {
    const auto &buffer = router.get_buffer_map().at(buffer_id);
    return buffer.is_queue() ? router.get_queue(buffer.queue_info().get_parent_queue_id()).data_format : buffer.info().data_format();
}

bool single_core_source_gather_pipe(router::Router const&router, unique_id_t pipe_id) {
    auto const& input_buf_ids = router.get_pipe(pipe_id).input_buffer_ids;
    tt_cxy_pair const& first_core = router.get_buffer(input_buf_ids.at(0)).core_location();
    return std::all_of(std::begin(input_buf_ids), std::end(input_buf_ids), [&first_core, &router](unique_id_t buf_id) -> bool { return router.get_buffer(buf_id).core_location() == first_core; });
};  

/*
 * This function returns whether or not this pipe 
 */
bool is_chip_to_chip_pipe_non_relocatable(router::Router const& router, unique_id_t id) {
    const auto &p = router.get_pipes().at(id);
    if (is_scatter_pipe(p)) {
        const auto src_core = router.get_buffer_location(p.input_buffer_ids.at(0));
        bool single_core_producer = std::all_of(
            std::begin(p.input_buffer_ids),
            std::end(p.input_buffer_ids),
            [&src_core, &router](unique_id_t b) { return src_core == router.get_buffer_location(b);}
        );
        return single_core_producer;
    } else if (is_gather_pipe(p) || is_gather_multicast_pipe(p)) {
        chip_id_t src_chip = router.get_buffer(p.input_buffer_ids.at(0)).chip_location();
        // If net2pipe decides to map the pipe mcast_core_rc to the producer chip, where the gather
        // part of the decomposed pipe will be implemented, we must respect its choice and reuse that location
        bool single_source_gather_pipe = router.get_buffer(p.input_buffer_ids.at(0)).is_queue() ? false : single_core_source_gather_pipe(router, id);
        bool net2pipe_expected_pipe_on_source_chip = p.core_location().chip == src_chip;
        bool reuse_pipe_location_for_producer_side_gather = net2pipe_expected_pipe_on_source_chip || single_source_gather_pipe;
        return reuse_pipe_location_for_producer_side_gather;
    }

    return false;
}

std::vector<unique_id_t> collect_non_relocatable_chip_to_chip_pipes(router::Router const& router) {
    std::vector<unique_id_t> non_relocatable_chip_to_chip_pipe_ids = {};
    for (const auto &[id, p]: router.get_pipes()) {
        bool is_chip_to_chip = is_chip_to_chip_pipe(p, router);
        if (is_chip_to_chip && is_chip_to_chip_pipe_non_relocatable(router, id)) {
            non_relocatable_chip_to_chip_pipe_ids.push_back(id);
        }
    }

    return non_relocatable_chip_to_chip_pipe_ids;
}


std::vector<unique_id_t> collect_chip_to_chip_pipes_dram_to_l1(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids) {
    std::vector<unique_id_t> pipes = {};

    for (const auto id: chip_to_chip_pipe_ids) {
        const auto &p = router.get_pipe(id);
        bool is_dram_to_l1 = router.get_buffer(p.input_buffer_ids.at(0)).is_queue() && 
            (p.has_multiple_timesteps() ? 
                not router.get_buffer(p.time_multiplexed_output_buffer_ids().at(0).at(0)).is_queue() :
                not router.get_buffer(p.output_buffer_ids().at(0)).is_queue()
            );
        if (is_dram_to_l1) {
            pipes.push_back(id);
        }
    }

    return pipes;
}
std::vector<unique_id_t> collect_chip_to_chip_pipes_l1_to_l1(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids) {
    std::vector<unique_id_t> pipes = {};

    for (const auto id: chip_to_chip_pipe_ids) {
        const auto &p = router.get_pipe(id);
        bool is_l1_to_l1 = not router.get_buffer(p.input_buffer_ids.at(0)).is_queue() && 
            (p.has_multiple_timesteps() ? 
                not router.get_buffer(p.time_multiplexed_output_buffer_ids().at(0).at(0)).is_queue() :
                not router.get_buffer(p.output_buffer_ids().at(0)).is_queue()
            );
        if (is_l1_to_l1) {
            pipes.push_back(id);
        }
    }

    return pipes;
}
std::vector<unique_id_t> collect_chip_to_chip_pipes_l1_to_dram(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids) {
    std::vector<unique_id_t> pipes = {};

    for (const auto id: chip_to_chip_pipe_ids) {
        const auto &p = router.get_pipe(id);
        bool is_l1_to_dram = not router.get_buffer(p.input_buffer_ids.at(0)).is_queue() && 
            (p.has_multiple_timesteps() ? 
                router.get_buffer(p.time_multiplexed_output_buffer_ids().at(0).at(0)).is_queue() :
                router.get_buffer(p.output_buffer_ids().at(0)).is_queue()
            );
        if (is_l1_to_dram) {
            pipes.push_back(id);
        }
    }

    return pipes;
}

std::unordered_set<unique_id_t> collect_forked_buffers(const router::Router &router) {
    auto forked_buffers = std::unordered_set<unique_id_t>{};
    const auto &buffer_output_pipes_map = router.get_buffer_output_pipes();
    for (const auto &[id, buffer] : router.get_buffer_map()) {
        if (buffer_output_pipes_map.find(id) != buffer_output_pipes_map.end() && buffer_output_pipes_map.at(id).size() > 1) {
            forked_buffers.insert(id);
        }
    }

    return forked_buffers;
}

std::vector<unique_id_t> collect_chip_to_chip_pipes_from_forked_buffers(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids) {
    const auto &forked_buffers = collect_forked_buffers(router);
    
    auto pipes = std::vector<unique_id_t>{};
    for (auto pipe_id : chip_to_chip_pipe_ids) {
        const auto &pipe = router.get_pipe(pipe_id);
        bool from_forked_buffer = forked_buffers.find(pipe.input_buffer_ids.at(0)) != forked_buffers.end();
        if (from_forked_buffer) {
            pipes.push_back(pipe_id);
        }
    }

    return pipes;
}

std::unordered_set<unique_id_t> collect_chip_to_chip_pipes_from_forked_dram_buffers_producers(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids_from_forked_buffers) {
    const auto &forked_buffers = collect_forked_buffers(router);
    auto buffers = std::unordered_set<unique_id_t>{};
    for (auto pipe_id : chip_to_chip_pipe_ids_from_forked_buffers) {
        const auto &pipe = router.get_pipe(pipe_id);
        bool from_dram = router.get_buffer(pipe.input_buffer_ids.at(0)).is_queue();
        if (from_dram) {
            buffers.insert(pipe.input_buffer_ids.at(0));
        }
    }

    return buffers;
}

std::vector<unique_id_t> collect_chip_to_chip_pipes_from_forked_dram_buffers(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids_from_forked_buffers) {
    auto pipes = std::vector<unique_id_t>{};

    for (auto pipe_id : chip_to_chip_pipe_ids_from_forked_buffers) {
        const auto &p = router.get_pipe(pipe_id);
        bool from_dram = router.get_buffer(p.input_buffer_ids.at(0)).is_queue();
        if (from_dram) {
            pipes.push_back(pipe_id);
        }
    }

    return pipes;
}

std::unordered_set<unique_id_t> collect_chip_to_chip_pipes_from_forked_l1_buffers_producers(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids_from_forked_buffers) {
    const auto &forked_buffers = collect_forked_buffers(router);
    auto buffers = std::unordered_set<unique_id_t>{};
    for (auto pipe_id : chip_to_chip_pipe_ids_from_forked_buffers) {
        const auto &pipe = router.get_pipe(pipe_id);
        bool from_dram = not router.get_buffer(pipe.input_buffer_ids.at(0)).is_queue();
        if (from_dram) {
            buffers.insert(pipe.input_buffer_ids.at(0));
        }
    }

    return buffers;
}

std::vector<unique_id_t> collect_chip_to_chip_pipes_from_forked_l1_buffers(const router::Router &router, const std::vector<unique_id_t> &chip_to_chip_pipe_ids_from_forked_buffers) {
    auto pipes = std::vector<unique_id_t>{};
    for (auto pipe_id : chip_to_chip_pipe_ids_from_forked_buffers) {
        const auto &p = router.get_pipe(pipe_id);
        bool from_l1 = not router.get_buffer(p.input_buffer_ids.at(0)).is_queue();
        if (from_l1) {
            pipes.push_back(pipe_id);
        }
    }

    return pipes;
}

void dump_ethernet_core_resources(const Router &router, chip_id_t chip) {
    const auto &soc_descriptor = router.get_soc_descriptor(chip);
    const auto &cluster_resource_model = router.get_cluster_resource_model();
    log_debug(tt::LogRouter, "-------------------------------------------------------------------------------------------------------");
    log_debug(tt::LogRouter, "{:19} | {:18} | {:18} | {:18} | {:26} | {:26} | {:18} ", "Eth Core Location", "L1 Size", "L1 Available", "(Eth) Streams Used", "(DRAM Input) Streams Used", "(DRAM Output) Streams Used", "(All) Streams Used");
    for (const auto &eth_core_xy : soc_descriptor.ethernet_cores) {
        const auto &core_attrs = cluster_resource_model.get_core_attributes(tt_cxy_pair(chip, eth_core_xy));
        auto loc_strstr = std::stringstream();
        loc_strstr << "(chip=" << chip << ",y=" << eth_core_xy.y << ",x=" << eth_core_xy.x <<")";
        log_debug(tt::LogRouter, " {:18} | {:17}B | {:17}B | {:18} | {:26} | {:26} | {:18} ", loc_strstr.str(), core_attrs.total_l1_size(), core_attrs.get_num_allocated_bytes(), core_attrs.get_used_ethernet_stream_count(), core_attrs.get_used_input_from_dram_streams(), core_attrs.get_used_output_to_dram_streams(), core_attrs.get_buffer_count());
    }
    log_debug(tt::LogRouter, "-------------------------------------------------------------------------------------------------------");
}


static void log_ethernet_channel_pair_allocation_failure_messages(
    const std::vector<std::tuple<int, int>> &directly_connected_channels,
    const Router& router, 
    chip_id_t sender_chip,
    const tt::buffer_info &sender_buffer_info, 
    const BufferAttributes &sender_buffer_attributes,
    chip_id_t receiver_chip,
    const tt::buffer_info &receiver_buffer_info, 
    const BufferAttributes &receiver_buffer_attributes
) {
    auto log_allocation_failure_reason = [&router](const tt::buffer_info &buffer_info, const BufferAttributes &buffer_attributes, chip_id_t chip, ethernet_channel_t channel) -> void  {
        const tt_xy_pair &core = router.get_soc_descriptor(chip).ethernet_cores.at(channel);
        const HwCoreAttributes &core_attributes = router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core));
        const auto failure_reasons = core_attributes.reasons_for_buffer_allocation_failure(buffer_info.allocated_size_in_bytes(), buffer_info.tile_size_in_bytes(), buffer_attributes);
        if (failure_reasons.has_value()) {
            log_debug(tt::LogRouter, "core available L1: {} B. Used eth streams: {}. Total: {}. Used mcast streams: {}", core_attributes.available_l1_bytes(), core_attributes.get_used_ethernet_stream_count(), core_attributes.get_max_num_ethernet_streams_total(), core_attributes.get_used_mcast_stream_count());
            log_buffer_allocation_failure_reasons(core_attributes, failure_reasons.value());
        }
    };

    for (const auto &[sender_channel, receiver_channel] : directly_connected_channels) {
        log_debug(tt::LogRouter, "Failure reasons for chip {} channel {} -> chip {} channel {}. Sender buffer size == {} B. Receiver buffer size == {} B.", sender_chip, sender_channel, receiver_chip, receiver_channel, sender_buffer_info.allocated_size_in_bytes(), receiver_buffer_info.allocated_size_in_bytes());
        log_allocation_failure_reason(sender_buffer_info, sender_buffer_attributes, sender_chip, sender_channel);
        log_allocation_failure_reason(receiver_buffer_info, receiver_buffer_attributes, receiver_chip, receiver_channel);
    }

}

/* Given a prioritized list of ethernet links `prioritized_directly_connected_channels`
 * iterate through the list in order until we find one that can fit the resource needs for
 * this buffer that is to be sent over ethernet.
 */
static std::optional<std::pair<ethernet_channel_t, ethernet_channel_t>> get_ethernet_channel_pair_able_to_allocate_ethernet_buffer(
    const std::vector<std::tuple<int, int>> &directly_connected_channels,
    const Router &router,
    // const RouterClusterResourceModel &cluster_resource_model,
    chip_id_t sender_chip,
    const tt::buffer_info &sender_buffer_info, 
    const BufferAttributes &sender_buffer_attributes,
    chip_id_t receiver_chip,
    const tt::buffer_info &receiver_buffer_info, 
    const BufferAttributes &receiver_buffer_attributes//,
    // const buda_SocDescriptor &soc_descriptor
) {
    auto sender_buffer_attrs_with_eth = sender_buffer_attributes; sender_buffer_attrs_with_eth.set_ethernet_io_streams(1);
    auto receiver_buffer_attrs_with_eth = receiver_buffer_attributes; receiver_buffer_attrs_with_eth.set_ethernet_io_streams(1);
    auto can_fit_buffer_on_eth_channel = [&router](chip_id_t chip, const tt::buffer_info &buffer_info, const BufferAttributes &buffer_attributes, ethernet_channel_t channel) -> bool {
        buda_SocDescriptor const& soc_descriptor = router.get_soc_descriptor(chip);
        const RouterClusterResourceModel &cluster_resource_model = router.get_cluster_resource_model();
        const tt_xy_pair &core = soc_descriptor.ethernet_cores.at(channel);
        const HwCoreAttributes &core_attributes = cluster_resource_model.get_core_attributes(tt_cxy_pair(chip, core));
        return core_attributes.can_add_buffer(buffer_info, buffer_attributes);
    };
    auto can_fit_buffer_on_sender_channel = std::bind(can_fit_buffer_on_eth_channel, sender_chip, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    auto can_fit_buffer_on_receiver_channel = std::bind(can_fit_buffer_on_eth_channel, receiver_chip, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    for (const auto &[sender_channel, receiver_channel] : directly_connected_channels) {
        bool can_add_buffer_to_sender_side = can_fit_buffer_on_sender_channel(sender_buffer_info, sender_buffer_attrs_with_eth, sender_channel);
        log_debug(tt::LogRouter, "Can't add buffer to sender size on channel {}", sender_channel);
        bool can_add_buffer_to_receiver_side = can_fit_buffer_on_receiver_channel(receiver_buffer_info, receiver_buffer_attrs_with_eth, receiver_channel);
        log_debug(tt::LogRouter, "Can't add buffer to receiver size on channel {}", receiver_channel);
        if (can_add_buffer_to_sender_side && can_add_buffer_to_receiver_side) {
            return std::pair<ethernet_channel_t,ethernet_channel_t>{sender_channel, receiver_channel};
        }
    }

    log_ethernet_channel_pair_allocation_failure_messages(
        directly_connected_channels, router, sender_chip, sender_buffer_info, sender_buffer_attrs_with_eth, receiver_chip, receiver_buffer_info, receiver_buffer_attrs_with_eth
    );

    return std::nullopt;
}

void reorder_ethernet_links_by_priority(
    const Router &router,
    std::vector<std::tuple<int, int>> &directly_connected_channels,
    chip_id_t sender_chip,
    chip_id_t receiver_chip
     /*FUTURE - add priorities - e.g. prioritize by l1 usage, buffer count, noc usage, etc.)*/
) {

    auto get_channel_allocated_buffer_count = [&router](chip_id_t chip, int channel) -> int {
        const buda_SocDescriptor& soc_descriptor = router.get_soc_descriptor(chip);
        const tt_xy_pair &core = soc_descriptor.ethernet_cores.at(channel);
        return router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip, core)).get_buffer_count();
    };

    std::sort(
        directly_connected_channels.begin(), 
        directly_connected_channels.end(), 
        [&](const std::tuple<int,int> &a, const std::tuple<int,int> &b) {
            // std::sort sorts in non-descending order with operator <. Since we want to put the least used channel pairs first,
            // then we want to sort on availability, which means if max_buffers_a < max_buffers_b, it has more availability. But operator
            // < will consider to that to be "less". Therefore we flip and use operator >.
            int max_buffers_a = std::max(get_channel_allocated_buffer_count(sender_chip, std::get<0>(a)), get_channel_allocated_buffer_count(receiver_chip, std::get<1>(a)));
            int max_buffers_b = std::max(get_channel_allocated_buffer_count(sender_chip, std::get<0>(b)), get_channel_allocated_buffer_count(receiver_chip, std::get<1>(b)));
            return max_buffers_a < max_buffers_b;
        }
    );
}


/*
 * Decomposes chip-to-chip gather pipes
 * e.g. 
 * input buffers       multicast targets (chip-to-chip)
 * [buf0 (chip0), buf1 (chip0)]  ->  [buf2 (chip1)]
 *
 * decomposes to:
 *
 * Original pipe: no longer chip-to-chip.
 * [buf0 (chip0), buf1 (chip0)] -> [*new* buf3 (chip0)]
 *
 * New pipe (chip-to-chip unicast)
 * [*new* buf3 (chip0)]  ->  [buf2 (chip1)]
 *
 */
void decompose_chip_to_chip_gather_into_unicast_chip_to_chips(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t pipe_id, std::vector<unique_id_t> &buffers_to_delete) {
    constexpr int CONSUMER_CHIP_GATHER_ENABLE_INPUT_COUNT = 16;
    const auto &gather_pipe = router.get_pipes().at(pipe_id);
    const int consumer_tile_granularity = gather_pipe.consumer_tile_granularity;
    const auto old_gather_location = gather_pipe.core_location();

    std::vector<tt_cxy_pair> l1_input_core_locations = {};
    const auto gather_input_buf_ids = gather_pipe.input_buffer_ids;
    for (const auto id : gather_input_buf_ids) {
        if(router.is_l1_buffer(id)) {
            l1_input_core_locations.push_back(router.get_buffer(id).core_location());
        }
    }
    
    bool producer_is_dram = router.get_buffer(gather_pipe.input_buffer_ids.at(0)).is_queue();
    bool is_single_core_source_gather = producer_is_dram ? false : single_core_source_gather_pipe(router, pipe_id);
    chip_id_t src_chip = router.get_buffer(gather_input_buf_ids.at(0)).chip_location();
    bool reuse_pipe_location_for_producer_side_gather = old_gather_location.chip == src_chip;
    TT_ASSERT(all_buffers_on_specified_chip(router, gather_input_buf_ids, src_chip), "Currently only support multi-chip gather where all inputs are on the same chip");

    DataFormat data_format = get_buffer_data_format(router, gather_pipe.input_buffer_ids.at(0));
    unique_id_t gather_receiver_buf_id = gather_pipe.output_buffer_ids().at(0);
    const auto &gather_receiver_buf = router.get_buffer(gather_receiver_buf_id);
    bool do_gather_on_producer_chip = true;
    if (do_gather_on_producer_chip) {
        TT_ASSERT(is_gather_pipe(gather_pipe));
        
        int num_eth_gather_buf_tiles = 1;
        const auto &eth_buffer_info = create_relay_buffer_info(
            num_eth_gather_buf_tiles, 
            router.get_scatter_gather_granularity(gather_input_buf_ids.at(0)),
            router.get_buffer_total_epoch_tiles(gather_receiver_buf_id), 
            data_format,
            router.get_buffer_tile_dims(gather_receiver_buf_id)
        );

        router.disconnect_pipe(pipe_id);
        router.remove_pipe(pipe_id);

        auto src_chip_gather_buffer_id = router.add_new_buffer(eth_buffer_info, src_chip);
        buffers_to_delete.push_back(src_chip_gather_buffer_id);

        // create the gather side pipe
        auto gather_pipe_id = router.create_gather_pipe_connection(gather_input_buf_ids, src_chip_gather_buffer_id, consumer_tile_granularity);
        // Choose core locations
        const std::optional<tt_cxy_pair> &gather_pipe_location =
            !reuse_pipe_location_for_producer_side_gather
                ? (producer_is_dram
                       ? (is_single_core_source_gather ? choose_valid_pipe_and_buffer_location_from_restricted_core_set(
                                                             router,
                                                             pipe_segment_id_t{.pipe_id = gather_pipe_id, .segment = 0},
                                                             src_chip_gather_buffer_id,
                                                             l1_input_core_locations)
                                                       : choose_valid_pipe_and_buffer_location(
                                                             router,
                                                             pipe_segment_id_t{.pipe_id = gather_pipe_id, .segment = 0},
                                                             src_chip_gather_buffer_id,
                                                             src_chip,
                                                             l1_input_core_locations))
                       : (is_single_core_source_gather ? choose_valid_pipe_location_from_restricted_core_set(
                                                             router,
                                                             pipe_segment_id_t{.pipe_id = gather_pipe_id, .segment = 0},
                                                             l1_input_core_locations)
                                                       : choose_valid_pipe_location(
                                                             router,
                                                             pipe_segment_id_t{.pipe_id = gather_pipe_id, .segment = 0},
                                                             src_chip,
                                                             l1_input_core_locations)))
                : old_gather_location;
        TT_ASSERT(gather_pipe_location.has_value(), "Couldn't find a valid location for gather pipe");
        TT_ASSERT(!router.is_dram_core(tt_xy_pair(gather_pipe_location.value().x, gather_pipe_location.value().y)));

        TT_ASSERT(router.get_buffer_input_pipes().find(gather_receiver_buf_id) == router.get_buffer_input_pipes().end());
        auto id2 = router.create_unicast_pipe_connection_at_core(src_chip_gather_buffer_id, gather_receiver_buf_id, gather_pipe_location.value(), consumer_tile_granularity);
        log_trace(tt::LogRouter, "\tCreated gather-pipe {} from original producer(s) to new producer chip gather buffer {}", gather_pipe_id, src_chip_gather_buffer_id);
        log_trace(tt::LogRouter, "\tCreated pipe {} from gather buffer {} on chip {} to original gather receiver buffer {} on core (c={},y={},x={})", id2, src_chip_gather_buffer_id, src_chip, gather_receiver_buf_id, gather_receiver_buf.core_location().chip, gather_receiver_buf.core_location().y, gather_receiver_buf.core_location().x);
        TT_ASSERT(!router.get_soc_descriptor(gather_pipe_location.value().chip).is_ethernet_core(gather_pipe_location.value()));
        
        router.assign_pipe_to_core(gather_pipe_id, gather_pipe_location.value());
        const auto &src_chip_gather_core_location =
            producer_is_dram ? gather_pipe_location.value()
                             : choose_location_for_gather_buffer(
                                   router, src_chip, l1_input_core_locations, {}, src_chip_gather_buffer_id);
        router.assign_buffer_to_core(src_chip_gather_buffer_id, src_chip_gather_core_location);
        TT_ASSERT(router.get_cluster_resource_model().get_core_attributes(src_chip_gather_core_location).number_of_available_extra_streams() >= 0);
        TT_ASSERT(!router.get_soc_descriptor(src_chip_gather_core_location.chip).is_ethernet_core(src_chip_gather_core_location));

        log_debug(tt::LogRouter, "------------>");
        print_pipe_verbose(router, gather_pipe_id, tt::Logger::Level::Debug);
        print_pipe_verbose(router, id2, tt::Logger::Level::Debug);
    }
}

/*
 * Decomposes chip-to-chip multicast pipes
 * e.g.
 * input buffers       multicast targets (chip-to-chip)
 * [buf0 (chip0)]  ->  [buf1 (chip1), buf2 (chip1)]
 *
 * decomposes to:
 *
 * New pipe (chip-to-chip unicast)
 * [buf0 (chip0)]  ->  [*new* buf3 (chip1)]
 *
 * Original pipe: no longer chip-to-chip. Unicast is performed locally only
 * [*new* buf3 (chip1)] -> [buf1 (chip1), buf2 (chip1)]
 *
 */
void decompose_chip_to_chip_multicast_into_unicast_chip_to_chips(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t pipe_id, std::vector<unique_id_t> &buffers_to_delete) {
    constexpr int PRODUCER_CHIP_MULTICAST_ENABLE_INPUT_COUNT = 16;
    const auto &old_pipe = router.get_pipes().at(pipe_id);
    const auto old_pipe_location = old_pipe.core_location();
    const int consumer_tile_granularity = old_pipe.consumer_tile_granularity;
    // const auto old_pipe_location = old_pipe.core_location();
    bool is_scatter = old_pipe.is_scatter();
    TT_ASSERT(!is_scatter);
    bool prefer_multicast_or_scatter_on_consumer_chip = PRODUCER_CHIP_MULTICAST_ENABLE_INPUT_COUNT < old_pipe.output_buffer_ids().size();
    if (prefer_multicast_or_scatter_on_consumer_chip) {
        log_warning(tt::LogRouter, "A cross chip multicast with > 16 inputs has been detected. It may be more performant to implement this as a multicast on the producer chip. multicast performed on consumer chip");
    }

    std::vector<tt_cxy_pair> output_core_locations = {};
    auto core_location_of_buffer_id = [&router](unique_id_t buf_id) -> tt_cxy_pair { return router.get_buffer_map().at(buf_id).core_location(); };
    const auto output_buf_ids = old_pipe.output_buffer_ids();
    std::transform(std::begin(output_buf_ids), std::end(output_buf_ids), std::back_inserter(output_core_locations), core_location_of_buffer_id);
    chip_id_t dest_chip = output_core_locations.at(0).chip;
    TT_ASSERT(all_buffers_on_specified_chip(router, output_buf_ids, dest_chip), "Currently only support multi-chip multicast where all inputs are on the same chip");

    DataFormat data_format = get_buffer_data_format(router, old_pipe.input_buffer_ids.at(0));
    bool do_multicast_on_consumer_chip = true;
    if (do_multicast_on_consumer_chip) {
        TT_ASSERT(
            old_pipe_location.chip == dest_chip,
            "mcast_core_rc for chip-to-chip multicast pipe " + std::to_string(pipe_id) +
                " must already be on the consumer chip");
        // will probably have to resize/support 2d
        int num_eth_buf_tiles = 1;
        const auto dest_num_epoch_tiles = router.get_buffer_map().at(output_buf_ids.at(0)).info().total_epoch_tiles();
        const auto &eth_buffer_info = create_relay_buffer_info(
            num_eth_buf_tiles, 
            router.get_scatter_gather_granularity(old_pipe.input_buffer_ids.at(0)),
            dest_num_epoch_tiles, 
            data_format,
            router.get_buffer_tile_dims(old_pipe.input_buffer_ids.at(0))
        );

        TT_ASSERT(is_multicast_pipe(old_pipe));
        unique_id_t multicast_sender_buf_id = old_pipe.input_buffer_ids.at(0);

        router.disconnect_pipe(pipe_id);
        router.remove_pipe(pipe_id);

        auto dest_chip_sender_buffer_id = router.add_new_buffer(eth_buffer_info, dest_chip);
        buffers_to_delete.push_back(dest_chip_sender_buffer_id);

        // create the multicast side pipe
        auto multicast_side_pipe_id = router.create_unicast_pipe_connection(multicast_sender_buf_id, dest_chip_sender_buffer_id, consumer_tile_granularity);
        auto mcast_pipe_location = old_pipe_location;
        mcast_pipe_location.chip = dest_chip;
        router.create_multicast_pipe_connection_at_core(dest_chip_sender_buffer_id, output_buf_ids, mcast_pipe_location, consumer_tile_granularity);
        const auto &dest_chip_sender_core_location = choose_location_for_multicast_buffer(router, dest_chip, {}, output_core_locations, dest_chip_sender_buffer_id);
        router.assign_buffer_to_core(dest_chip_sender_buffer_id, dest_chip_sender_core_location);
        router.assign_pipe_to_core(multicast_side_pipe_id, dest_chip_sender_core_location);
    }
}

BufferAttributes collect_merged_buffer_attributes_for_buffers(const router::Router &router, const std::vector<unique_id_t> &buffer_ids) {
    auto buf_attrs = std::vector<BufferAttributes>(buffer_ids.size());
    std::transform(
        std::begin(buffer_ids), 
        std::end(buffer_ids), 
        std::back_inserter(buf_attrs), 
        [&router](unique_id_t id) { return router.identify_buffer_attributes(id); }
    );

    auto merged_attrs = BufferAttributes();
    for (const auto &buffer_attrs : buf_attrs) {
        merged_attrs = merge(buffer_attrs, merged_attrs);
    }
    
    return merged_attrs;
}

/*
 * Assumes buffer will not be placed on ethernet core for receiving over ethernet
 */
BufferAttributes collect_buffer_attrs_for_multicast_side_of_gather_multicast_pipe(const router::Router &router, const pipe_t &gather_multicast_pipe) {
    const auto &input_buf_ids = gather_multicast_pipe.output_buffer_ids();
    auto buf_attrs = std::vector<BufferAttributes>(input_buf_ids.size());
    std::transform(std::begin(input_buf_ids), std::end(input_buf_ids), std::back_inserter(buf_attrs), [&router](unique_id_t id) { return router.identify_buffer_attributes(id); });

    auto merged_buffer_attrs = collect_merged_buffer_attributes_for_buffers(router, input_buf_ids);//BufferAttributes();
    merged_buffer_attrs.set_ethernet_io_streams(0);
    TT_ASSERT(merged_buffer_attrs.get_num_ethernet_io_streams() == 0);
    merged_buffer_attrs.remove_input_from_dram();
    merged_buffer_attrs.set_multicast(1);
    return merged_buffer_attrs;
}


/*
 * Assumes buffer will not be placed on ethernet core for transmission over ethernet
 */
BufferAttributes collect_buffer_attrs_for_gather_side_of_gather_multicast_pipe(const router::Router &router, const pipe_t &gather_multicast_pipe) {
    const auto &output_buf_ids = gather_multicast_pipe.input_buffer_ids;
    auto buf_attrs = std::vector<BufferAttributes>(output_buf_ids.size());
    std::transform(std::begin(output_buf_ids), std::end(output_buf_ids), std::back_inserter(buf_attrs), [&router](unique_id_t id) { return router.identify_buffer_attributes(id); } );

    auto merged_buffer_attrs = collect_merged_buffer_attributes_for_buffers(router, output_buf_ids);//BufferAttributes();
    merged_buffer_attrs.set_multicast(1); // For the gather operation - maybe we should add a get gather method
    merged_buffer_attrs.set_ethernet_io_streams(0);
    TT_ASSERT(merged_buffer_attrs.get_num_ethernet_io_streams() == 0);
    merged_buffer_attrs.set_outputs_to_dram(0);
    // merged_buffer_attrs.set_ethernet_io_streams(1);
    return merged_buffer_attrs;
}

/*
 * Decomposes chip-to-chip gather-multicast pipes into local gather -> unicast (chip-to-chip) -> local multicast
 * e.g.
 * input buffers       multicast targets (chip-to-chip)
 * [buf0 (chip0), buf1(chip0)]  ->  [buf2 (chip1), buf3 (chip1)]
 *
 * decomposes to:
 *
 * Gather side pipe: no longer chip-to-chip.
 * [buf0 (chip0), buf1(chip0)] -> [*new* buf4 (chip0)]
 *
 * Unicast: chip-to-chip
 * [*new* buf4 (chip0)] -> [*new* buf5 (chip1)]
 *
 * Multicast side pipe: no longer chip-to-chip.
 * [*new* buf5 (chip1)] -> [buf2 (chip1), buf3 (chip1)]
 *
 */
void decompose_chip_to_chip_gather_multicast_into_unicast_chip_to_chips(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t pipe_id, std::vector<unique_id_t> &buffers_to_delete) {
    const auto old_pipe = router.get_pipes().at(pipe_id);
    const auto old_pipe_location = old_pipe.core_location();
    const int consumer_tile_granularity = old_pipe.consumer_tile_granularity;
    bool is_scatter = old_pipe.is_scatter();
    TT_ASSERT(!is_scatter);
    TT_ASSERT(is_gather_multicast_pipe(old_pipe));

    const auto input_buf_ids = old_pipe.input_buffer_ids;

    bool is_dram_producer = router.get_buffer(input_buf_ids.at(0)).is_queue();
    bool is_single_core_source_gather = is_dram_producer ? false : single_core_source_gather_pipe(router, pipe_id);
    const auto output_buf_ids = old_pipe.output_buffer_ids();
    std::vector<tt_cxy_pair> input_core_locations = {};
    std::vector<tt_cxy_pair> output_core_locations = {};
    auto core_location_of_buffer_id = [&router](unique_id_t buf_id) -> tt_cxy_pair { return router.get_buffer_map().at(buf_id).core_location(); };
    std::transform(std::begin(input_buf_ids), std::end(input_buf_ids), std::back_inserter(input_core_locations), core_location_of_buffer_id);
    std::transform(std::begin(output_buf_ids), std::end(output_buf_ids), std::back_inserter(output_core_locations), core_location_of_buffer_id);
    chip_id_t src_chip = input_core_locations.at(0).chip;
    chip_id_t dest_chip = output_core_locations.at(0).chip;
    auto buf_chip_equals_src_chip = [&src_chip](const tt_cxy_pair &core_location) -> bool { return static_cast<int>(core_location.chip) == src_chip; };
    auto buf_chip_equals_dest_chip = [&dest_chip](const tt_cxy_pair &core_location) -> bool { return static_cast<int>(core_location.chip) == dest_chip; };
    TT_ASSERT(std::all_of(std::begin(input_core_locations), std::end(input_core_locations), buf_chip_equals_src_chip), "Currently only support multi-chip gather where all inputs are on the same chip");
    TT_ASSERT(std::all_of(std::begin(output_core_locations), std::end(output_core_locations), buf_chip_equals_dest_chip), "Currently only support multi-chip multicast where all inputs are on the same chip");

    std::vector<tt_cxy_pair> l1_input_core_locations = {};
    for (const auto id : input_buf_ids) {
        if(router.is_l1_buffer(id)) {
            l1_input_core_locations.push_back(router.get_buffer(id).core_location());
        }
    }

    // only one unique input_core_location for single_core_source_gather
    TT_ASSERT(
        !is_single_core_source_gather ||
        l1_input_core_locations.size() == 1 ||
        std::all_of(l1_input_core_locations.begin(), l1_input_core_locations.end(), [&l1_input_core_locations](const tt_cxy_pair &loc) {
            return loc == l1_input_core_locations.at(0);
            }
        ));
    
    DataFormat data_format = get_buffer_data_format(router, input_buf_ids.at(0));
    int num_eth_buf_tiles = 1;
    int num_epoch_tiles = router.get_buffer_map().at(output_buf_ids.at(0)).info().total_epoch_tiles();
    const auto &eth_buffer_info = create_relay_buffer_info(
        num_eth_buf_tiles, 
        router.get_scatter_gather_granularity(input_buf_ids.at(0)),
        num_epoch_tiles,
        data_format,
        router.get_buffer_tile_dims(input_buf_ids.at(0))
    );
    
    router.disconnect_pipe(pipe_id);
    router.remove_pipe(pipe_id);

    auto src_chip_gather_buffer_id = router.add_new_buffer(eth_buffer_info, src_chip);
    auto dest_chip_sender_buffer_id = router.add_new_buffer(eth_buffer_info, dest_chip);
    buffers_to_delete.push_back(src_chip_gather_buffer_id);
    buffers_to_delete.push_back(dest_chip_sender_buffer_id);

    // create the multicast side pipe
    auto gather_side_pipe_id = router.create_gather_pipe_connection(input_buf_ids, src_chip_gather_buffer_id, consumer_tile_granularity);
    auto id1 = router.create_unicast_pipe_connection(src_chip_gather_buffer_id, dest_chip_sender_buffer_id, consumer_tile_granularity);
    auto multicast_side_pipe =
        router.create_multicast_pipe_connection(dest_chip_sender_buffer_id, output_buf_ids, consumer_tile_granularity);
    bool choose_new_gather_side_pipe_location = old_pipe_location.chip != src_chip;
    bool choose_new_multicast_side_pipe_location = old_pipe_location.chip != dest_chip;

    auto mcast_pipe_location = old_pipe_location;
    
    TT_ASSERT(old_pipe_location.chip == dest_chip, "Net2Pipe must specify the mcast_core_rc for chip-to-chip pipes with mcast components");
    std::vector<tt_cxy_pair> mcast_pipe_location_vec = {mcast_pipe_location};
    const auto &multicast_side_pipe_location = choose_valid_pipe_location(router, pipe_segment_id_t{.pipe_id=multicast_side_pipe, .segment=0}, dest_chip, mcast_pipe_location_vec);
    TT_ASSERT(multicast_side_pipe_location.has_value(), "Couldn't find valid core location for pipe");
    router.assign_pipe_to_core(multicast_side_pipe, multicast_side_pipe_location.value());

    // Assign buffers/pipes to cores
    const auto &gather_pipe_location =
        choose_new_gather_side_pipe_location
            ? (is_dram_producer ? (is_single_core_source_gather
                                       ? choose_valid_pipe_and_buffer_location_from_restricted_core_set(
                                             router,
                                             pipe_segment_id_t{.pipe_id = gather_side_pipe_id, .segment = 0},
                                             src_chip_gather_buffer_id,
                                             l1_input_core_locations)
                                       : choose_valid_pipe_and_buffer_location(
                                             router,
                                             pipe_segment_id_t{.pipe_id = gather_side_pipe_id, .segment = 0},
                                             src_chip_gather_buffer_id,
                                             src_chip,
                                             l1_input_core_locations))
                                : (is_single_core_source_gather
                                       ? choose_valid_pipe_location_from_restricted_core_set(
                                             router,
                                             pipe_segment_id_t{.pipe_id = gather_side_pipe_id, .segment = 0},
                                             l1_input_core_locations)
                                       : choose_valid_pipe_location(
                                             router,
                                             pipe_segment_id_t{.pipe_id = gather_side_pipe_id, .segment = 0},
                                             src_chip,
                                             l1_input_core_locations)))
            : old_pipe_location;
    TT_ASSERT(gather_pipe_location.has_value(), "Couldn't find valid core location for pipe");
    router.assign_pipe_to_core(gather_side_pipe_id, gather_pipe_location.value());
    const auto &src_chip_gather_core_location =
        is_dram_producer ? gather_pipe_location.value()
                         : choose_location_for_gather_buffer(
                               router, src_chip, l1_input_core_locations, {}, src_chip_gather_buffer_id);
    router.assign_buffer_to_core(src_chip_gather_buffer_id, src_chip_gather_core_location);

    const auto &dest_chip_sender_core_location = choose_location_for_multicast_buffer(router, dest_chip, {}, mcast_pipe_location_vec, dest_chip_sender_buffer_id);
    router.assign_buffer_to_core(dest_chip_sender_buffer_id, dest_chip_sender_core_location);
    router.assign_pipe_to_core(id1, dest_chip_sender_core_location);

    log_debug(tt::LogRouter,"---------->");
    print_pipe_verbose(router, gather_side_pipe_id, tt::Logger::Level::Debug);
    print_pipe_verbose(router, id1, tt::Logger::Level::Debug);
    print_pipe_verbose(router, multicast_side_pipe, tt::Logger::Level::Debug);
    log_debug(tt::LogRouter, "");
}

void decompose_chip_to_chip_tm_into_unicast_chip_to_chips(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t pipe_id, std::vector<unique_id_t> &buffers_to_delete) {
    log_debug(tt::LogRouter, "Decomposing chip-to-chip TM pipe {}", pipe_id);
    const auto &old_pipe = router.get_pipes().at(pipe_id);
    const int consumer_tile_granularity = old_pipe.consumer_tile_granularity;

    DataFormat data_format = get_buffer_data_format(router, old_pipe.input_buffer_ids.at(0));
    chip_id_t src_chip = router.get_buffer(old_pipe.input_buffer_ids.at(0)).chip_location();
    TT_ASSERT(all_buffers_on_same_chip(router, old_pipe.input_buffer_ids), "Currently don't support a TM pipe where the input buffers are on different chips");
    int num_eth_buf_tiles = 1;
    const auto timestep_output_buf_ids = old_pipe.time_multiplexed_output_buffer_ids();

    const auto src_core = router.get_buffer_location(old_pipe.input_buffer_ids.at(0));
    bool single_core_producer = std::all_of(
        std::begin(old_pipe.input_buffer_ids),
        std::end(old_pipe.input_buffer_ids),
        [&src_core, &router](unique_id_t b) { return src_core == router.get_buffer_location(b);}
    );

    std::unordered_map<std::vector<unique_id_t>, unique_id_t, unique_ids_hasher<std::vector<unique_id_t>>> seen_scatter_segment_targets_buffer_map = {};

    int num_scatter_segments = timestep_output_buf_ids.size();
    for (int s = 0; s < num_scatter_segments; s++) {
        const auto output_buffer_ids = timestep_output_buf_ids[s];
        tt_cxy_pair const &pipe_scatter_location = old_pipe.scatter_segment_core_location(s);
        bool chip_to_chip = !all_buffers_on_specified_chip(router, output_buffer_ids, src_chip);
        if (!chip_to_chip) {
            log_debug(tt::LogRouter, "\ttimestep {} is not chip-to-chip", s);
        } else {

            bool scatter_target_group_not_seen_yet = seen_scatter_segment_targets_buffer_map.find(output_buffer_ids) == seen_scatter_segment_targets_buffer_map.end();
            if (scatter_target_group_not_seen_yet) {

                TT_ASSERT(all_buffers_on_same_chip(router, output_buffer_ids), "Currently don't support when multicasting to targets that span multiple targets");
                // The TM must remain implemented on the producer
                // Replace with producer pipe -> unicast pipe (producer chip -> consumer_chip) -> multicast pipe (consumer_chip)
                // Create multicast buffer on consumer chip
                auto new_buffer_attrs = BufferAttributes();
                new_buffer_attrs.set_multicast(1);
                const auto total_epoch_tiles = router.get_buffer(output_buffer_ids.at(0)).info().total_epoch_tiles();
                const auto scatter_gather_granularity = router.get_scatter_gather_granularity(output_buffer_ids.at(0)); // all the outputs should have the same granularity so we should be able to pick any one
                const auto &multicast_buffer_info = create_relay_buffer_info(
                    num_eth_buf_tiles, 
                    scatter_gather_granularity,
                    total_epoch_tiles, 
                    data_format,
                    router.get_buffer_tile_dims(output_buffer_ids.at(0))
                );

                unique_id_t new_scatter_segment_target_buffer_id = router.add_new_buffer(multicast_buffer_info, src_chip);
                // buffers_to_delete.push_back(new_scatter_segment_target_buffer_id); // Don't enable until we can remove the buffers that are outputs of multi-timestep pipes
                log_debug(tt::LogRouter, "\tCreated new intermediate output buffer {} for output scatter segment {}", new_scatter_segment_target_buffer_id, s);
                router.replace_scatter_pipe_outputs_at_segment(pipe_segment_id_t{.pipe_id=pipe_id, .segment=s}, {new_scatter_segment_target_buffer_id});
                if (output_buffer_ids.size() == 1) {
                    tt_cxy_pair const &pipe_location = router.get_buffer(output_buffer_ids.at(0)).core_location();
                    auto added_pipe_id = router.create_unicast_pipe_connection_at_core(new_scatter_segment_target_buffer_id, output_buffer_ids.at(0), pipe_location, consumer_tile_granularity);
                    log_debug(tt::LogRouter, "\tunicast pipe {} from {} to {} @(c={},y={},x={})", added_pipe_id, new_scatter_segment_target_buffer_id, output_buffer_ids.at(0), pipe_location.chip, pipe_location.y, pipe_location.x);
                } else {
                    auto added_pipe_id = router.create_multicast_pipe_connection_at_core(
                        new_scatter_segment_target_buffer_id,
                        output_buffer_ids,
                        pipe_scatter_location,
                        consumer_tile_granularity);
                    log_debug(tt::LogRouter, "\tmulticast pipe {} from {} to {}", added_pipe_id, new_scatter_segment_target_buffer_id, fmt::join(output_buffer_ids, ", "));
                }

                const auto &new_buffer_location = choose_location_for_buffer(router, src_chip, {}, new_scatter_segment_target_buffer_id, {BufferAllocationFailureReason::Insufficient_Ethernet_Streams});
                router.assign_buffer_to_core(new_scatter_segment_target_buffer_id, new_buffer_location);

                seen_scatter_segment_targets_buffer_map.insert({output_buffer_ids, new_scatter_segment_target_buffer_id});
            } else {
                unique_id_t new_scatter_segment_target_buffer_id = seen_scatter_segment_targets_buffer_map.at(output_buffer_ids);
                log_debug(tt::LogRouter, "\tReusing intermediate output buffer {} for output scatter segment {}", new_scatter_segment_target_buffer_id, s);
                router.replace_scatter_pipe_outputs_at_segment(pipe_segment_id_t{pipe_id, s}, {new_scatter_segment_target_buffer_id});
            }
            // For now we always assign the pipe location at this timestep to be one of the source cores. In case there is a TM gather, it
            // will always be safe to give the core-rc to pipegen as the source core, even if there as a gather operation for the TM.
            // It may be optimal to move the gather core-rc to the receiving relay buffer's core. We don't do it yet in case that receiver
            // is on the ethernet core since resources are limited on ethernet. If we do wish to do that, we should do it at a later step
            // after all chip-to-chip streams have been implemented.
            std::optional<tt_cxy_pair> pipe_scatter_segment_core =
                single_core_producer ? src_core : 
                choose_valid_pipe_location(router, pipe_segment_id_t{.pipe_id=pipe_id,.segment=s}, src_chip, {src_core});
            if (!pipe_scatter_segment_core.has_value()) {
                log_error("Couldn't find valid core location for chip-to-chip pipe scatter segment {} for pipe {}", s, pipe_id);
            }
            router.assign_pipe_scatter_segment_to_core(pipe_id, s, pipe_scatter_segment_core.value());
        }
    }

}

/*
 * This function decomposes scatter pipes with chip-to-chip scatter segments such that each scatter segment only sends
 * to the local chip.
 * e.g.
 * input buffers          scatter segment 1 (chip-to-chip)    scatter segment 2 (not chip-to-chip)
 * [buf0 (chip0)]  ->  [ [buf1 (chip1), buf2 (chip1)],        [ buf3 (chip0), buf4 (chip0)]          ]
 *
 * decomposes to:

 * Original pipe (no longer chip-to-chip)
 * [buf0 (chip0)]  ->  [ [*new* buf5 (chip0)],        [ buf3 (chip0), buf4 (chip0)]          ]
 *
 * New pipe (chip-to-chip - multicast in this case) - this can be decomposed and routed in later decomposition steps
 * [buf5 (chip0)]  ->  [ buf1 (chip1), buf2 (chip1) ]
 *
 */
void decompose_interchip_scatter_pipes_to_non_scatter_interchip_pipes(
    router::Router &router,
    const ClusterGraph &cluster_graph,
    std::vector<unique_id_t> &buffers_to_delete,
    std::function < std::vector<unique_id_t>(router::Router const &)> chip_to_chip_pipe_getter_func) {
    const auto chip_to_chip_pipe_ids = chip_to_chip_pipe_getter_func(router);
    for (const unique_id_t id : chip_to_chip_pipe_ids) {
        const auto &p = router.get_pipes().at(id);
        if (is_scatter_pipe(p)) {
            log_info(tt::LogRouter, "Decomposing chip-to-chip scatter pipe {}", id);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            decompose_chip_to_chip_tm_into_unicast_chip_to_chips(router, cluster_graph, id, buffers_to_delete);
        }
    }
}

/*
 * Decomposes chip-to-chip pipes such that after the decomposition stage, only unicasts are routed chip-to-chip
 */
std::vector<unique_id_t> decompose_interchip_pipes_to_unicast_interchip_pipes(
    router::Router &router,
    const ClusterGraph &cluster_graph,
    std::function<std::vector<unique_id_t>(router::Router const &)> chip_to_chip_pipe_getter_func) {
    std::vector<unique_id_t> buffers_to_delete = {};
    decompose_interchip_scatter_pipes_to_non_scatter_interchip_pipes(router, cluster_graph, buffers_to_delete, chip_to_chip_pipe_getter_func);

    const auto chip_to_chip_pipe_ids = chip_to_chip_pipe_getter_func(router);
    for (const unique_id_t id : chip_to_chip_pipe_ids) {
        const auto &p = router.get_pipes().at(id);
        if (is_multicast_pipe(p)) {
            log_info(tt::LogRouter, "Decomposing multicast or scatter pipe {} @(c={},y={},x={})", id, p.core_location().chip, p.core_location().y, p.core_location().x);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            decompose_chip_to_chip_multicast_into_unicast_chip_to_chips(router, cluster_graph, id, buffers_to_delete);
        } else if (is_gather_pipe(p)) {
            log_info(tt::LogRouter, "Decomposing gather pipe {} @(c={},y={},x={})", id, p.core_location().chip, p.core_location().y, p.core_location().x);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            decompose_chip_to_chip_gather_into_unicast_chip_to_chips(router, cluster_graph, id, buffers_to_delete);
        } else if (is_gather_multicast_pipe(p)) {
            log_info(tt::LogRouter, "Decomposing gather-multicast or gather-scatter pipe {} @(c={},y={},x={})", id, p.core_location().chip, p.core_location().y, p.core_location().x);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            decompose_chip_to_chip_gather_multicast_into_unicast_chip_to_chips(router, cluster_graph, id, buffers_to_delete);
        } else if (is_scatter_pipe(p)) {
            TT_ASSERT(false, "Should have already been decomposed");
            log_info(tt::LogRouter, "Decomposing TM or scatter pipe {}", id);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            decompose_chip_to_chip_tm_into_unicast_chip_to_chips(router, cluster_graph, id, buffers_to_delete);
        } else {
            TT_ASSERT(is_unicast_pipe(p), "Hit a pipe type that wasn't properly handled above");
        }
    }

    return buffers_to_delete;
}

/*
 * Returns chip-to-chip pipe IDs sorted ascending by distance (minimum # hops needed) between producer and consumer
 * chips
 */
std::vector<unique_id_t> collect_chip_to_chip_pipe_ids_by_distance(router::Router &router, const ClusterGraph &cluster_graph) {

    // TODO(snijjar): Change to all-pairs shortest path algorithm 
    // TODO(snijjar): Provide a hashing function so we don't need to nest unordered_maps
    std::unordered_map<chip_id_t, std::unordered_map<chip_id_t, int>> chip_to_chip_distances = {};

    auto number_of_chip_to_chip_hops = [&router, &cluster_graph, &chip_to_chip_distances](unique_id_t pipe_id) -> int {
        const auto &p = router.get_pipe(pipe_id);
        TT_ASSERT(is_unicast_pipe(p));

        const auto &src_chip = router.get_buffer(p.input_buffer_ids.at(0)).chip_location();
        const auto &dest_chip = router.get_buffer(p.output_buffer_ids().at(0)).chip_location();
        TT_ASSERT(src_chip != dest_chip);
        static_assert(std::is_integral_v<std::remove_reference_t<decltype(src_chip)>>, "distance calculation currently implemented for integer types. For 2D chip IDs, this needs updating.");
        static_assert(std::is_integral_v<std::remove_reference_t<decltype(dest_chip)>>, "distance calculation currently implemented for integer types. For 2D chip IDs, this needs updating.");

        if (chip_to_chip_distances.find(src_chip) == chip_to_chip_distances.end() ||
            chip_to_chip_distances.at(src_chip).find(dest_chip) == chip_to_chip_distances.at(src_chip).end()) {
            int min_hop_count = find_shortest_path_chip_to_chip_hop_count(router, cluster_graph, pipe_id);
            chip_to_chip_distances[src_chip][dest_chip] = min_hop_count;
            chip_to_chip_distances[dest_chip][src_chip] = min_hop_count;
        }

        return chip_to_chip_distances.at(src_chip).at(dest_chip);
    };

    auto chip_to_chip_pipe_ids = collect_chip_to_chip_pipes(router);
    std::sort(chip_to_chip_pipe_ids.begin(), chip_to_chip_pipe_ids.end(), [&](unique_id_t p1, unique_id_t p2) { return number_of_chip_to_chip_hops(p1) < number_of_chip_to_chip_hops(p2); } );
    TT_ASSERT(chip_to_chip_pipe_ids.size() == 0 || number_of_chip_to_chip_hops(chip_to_chip_pipe_ids.front()) <= number_of_chip_to_chip_hops(chip_to_chip_pipe_ids.back()));
    return chip_to_chip_pipe_ids;
}

/* Currently implemented for 1D clusters. When implemented for 2D, there will be multiple potential paths, so we'll also
 * want to update the routing algorithm to choose a path with (readily) available eth resources in the event that one
 * path has all ethernet streams "occupied" and there is another path available, with the same distance, that has
 * ethernet resources available.
 */
std::vector<std::pair<chip_id_t, chip_id_t>> generate_chip_to_chip_route_for_pipe(router::Router &router, const ClusterGraph &cluster_graph, unique_id_t chip_to_chip_pipe_id) {
    // Insert calls to whatever routing algorithm you want - here
    return find_shortest_path_chip_to_chip_route_ignoring_bandwidth(router, cluster_graph, chip_to_chip_pipe_id);   
}

/*
 * Given a chip-to-chip unicast and a chosen chip-to-chip route, explicitly choose the ethernet channels that the
 * route will be implemented on.
 */
std::vector<std::pair<ethernet_channel_t, ethernet_channel_t>> choose_ethernet_channels_along_route(
    router::Router &router, 
    const ClusterGraph &cluster_graph, 
    unique_id_t original_chip_to_chip_pipe_id,
    const tt::buffer_info &eth_buffer_info,
    const std::vector<std::pair<chip_id_t,chip_id_t>> &route
) {
    auto selected_sender_received_channels = std::vector<std::pair<ethernet_channel_t, ethernet_channel_t>>{};
    const pipe_t &original_pipe = router.get_pipe(original_chip_to_chip_pipe_id);
    const unique_id_t original_src_buf_id = original_pipe.input_buffer_ids.at(0);
    const unique_id_t original_dest_buf_id = original_pipe.output_buffer_ids().at(0);

    for (size_t i = 0; i < route.size(); i++) {
        bool is_first_link = i == 0;
        bool is_last_link = i == route.size() - 1;
        const auto &[sender_chip, receiver_chip] = route.at(i);
        auto src_eth_buffer_attributes = BufferAttributes::requires_ethernet_io();
        if (is_first_link) {
            if (router.is_queue_buffer(original_src_buf_id)) {
                src_eth_buffer_attributes.set_input_from_dram();
            }
        }
        auto dest_eth_buffer_attributes = BufferAttributes::requires_ethernet_io();
        if (is_last_link) {
            dest_eth_buffer_attributes.set_outputs_to_dram(router.is_queue_buffer(original_dest_buf_id));
        }
        auto src_dest_channel_pairs = cluster_graph.cluster_description().get_directly_connected_ethernet_channels_between_chips(sender_chip, receiver_chip);
	    reorder_ethernet_links_by_priority(router, src_dest_channel_pairs, sender_chip, receiver_chip);
        const auto &chosen_channel_pair = get_ethernet_channel_pair_able_to_allocate_ethernet_buffer(
            src_dest_channel_pairs,
            router,
            // router.get_cluster_resource_model(),
            sender_chip,
            eth_buffer_info, 
            src_eth_buffer_attributes,
            receiver_chip,
            eth_buffer_info, 
            dest_eth_buffer_attributes
        );
        if (chosen_channel_pair == std::nullopt) {
            log_error("Couldn't find viable ethernet link to transmit pipe {} over because of a lack of resources", original_chip_to_chip_pipe_id);
            std::stringstream route_string = {};
            for (const auto &[src,dest] : route) { route_string << "(" << src << "->" << dest << "), "; }
            log_error("\tFull routed path: {}", route_string.str());
            log_error("\tCurrent route link: {} -> {}", sender_chip, receiver_chip);

            // We can add more diagnostic messaging here by collecting the reasons for each core why buffer allocation would fail when 
            // it becomes useful
            log_error("Sender chip ({}) ethernet resources", sender_chip);
            dump_ethernet_core_resources(router, sender_chip);
            log_error("Receiver chip ({}) ethernet resources", receiver_chip);
            dump_ethernet_core_resources(router, receiver_chip);
            ERROR("Failed to allocate ethernet buffers needed to implement a chip to chip pipe");
        }

        selected_sender_received_channels.push_back(chosen_channel_pair.value());
    }

    return selected_sender_received_channels;
}

/*
 * Helper function
 */
unique_id_t generate_eth_link_endpoint_buffer(
    router::Router &router,
    const buda_SocDescriptor &soc_descriptor,
    const tt::buffer_info &eth_buffer_info,
    chip_id_t chip,
    ethernet_channel_t channel) {
    const auto &eth_core_location = tt_cxy_pair(chip, soc_descriptor.ethernet_cores.at(channel));
    auto buffer_stream = router.get_available_relay_buffer_stream_at_core(eth_core_location);
    TT_ASSERT(buffer_stream.has_value() && *buffer_stream >= 0);
    const auto &buffer_attrs = router_buffer_info_t::create_mutable(
        eth_core_location, tt::buffer_info(RouterBufferType::Relay, buffer_stream.value(), eth_buffer_info));
    auto buf_id = router.add_new_buffer_to_core(eth_core_location, buffer_attrs.info());
    log_trace(
        tt::LogRouter,
        "Added sender ethernet buffer. ID={}, location=(c={},y={},x={})",
        buf_id,
        chip,
        eth_core_location.y,
        eth_core_location.x);

    return buf_id;
};

/*
 * returns IDs of sender and receiver buffers, respectively
 */
std::tuple<unique_id_t, unique_id_t> create_eth_link_pipe_and_endpoint_buffers(
    router::Router &router, 
    const pipe_t &original_pipe_copy,
    const chip_id_t sender_chip, 
    const ethernet_channel_t sender_chan, 
    const chip_id_t receiver_chip, 
    const ethernet_channel_t receiver_chan,
    const tt::buffer_info &eth_buffer_info, 
    const int consumer_tile_granularity
) {
    // Ethernet datacopy ops present the possibility that the sender or receiver buffers are already placed on the ethernet core on either end of the link.
    // If this is the case, we don't want to create a new buffer, but instead use the existing one.
    bool sender_side_buffer_already_there =
        original_pipe_copy.input_buffer_ids.size() == 1 &&
        router.buffer_is_on_ethernet_core(original_pipe_copy.input_buffer_ids[0], sender_chip, sender_chan);
    auto sender_buf_id = sender_side_buffer_already_there ? 
        original_pipe_copy.input_buffer_ids[0] :
        generate_eth_link_endpoint_buffer(router, router.get_soc_descriptor(sender_chip), eth_buffer_info, sender_chip, sender_chan);

    bool receiver_side_buffer_already_there =
        !original_pipe_copy.has_multiple_timesteps() && original_pipe_copy.output_buffer_ids().size() == 1 &&
        router.buffer_is_on_ethernet_core(original_pipe_copy.output_buffer_ids().at(0), receiver_chip, receiver_chan);
    auto receiver_buf_id = receiver_side_buffer_already_there ? 
        original_pipe_copy.output_buffer_ids().at(0) :
        generate_eth_link_endpoint_buffer(router, router.get_soc_descriptor(receiver_chip), eth_buffer_info, receiver_chip, receiver_chan);

    if (!sender_side_buffer_already_there || !receiver_side_buffer_already_there) {
        router.create_unicast_pipe_connection_at_core(sender_buf_id, receiver_buf_id, router.get_buffer_location(receiver_buf_id), consumer_tile_granularity);
    }

    return {sender_buf_id, receiver_buf_id};
}

struct sender_receiver_t { 
    unique_id_t sender; 
    unique_id_t receiver;
};



/*
 * Helper function
 */
void insert_relay_buffer_on_input_pipe_to_ethernet_route(
    router::Router &router,
    unique_id_t pipe_id,
    std::vector<unique_id_t> &removable_buffers
) {
    const pipe_t &pipe = router.get_pipe(pipe_id);
    TT_ASSERT(pipe.input_buffer_ids.size() == 1 && !pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1);
    const int consumer_tile_granularity = pipe.consumer_tile_granularity;

    const unique_id_t input_buffer_id = pipe.input_buffer_ids.at(0);
    const auto &input_buffer = router.get_buffer(input_buffer_id);
    const unique_id_t output_buffer_id = pipe.output_buffer_ids().at(0);
    const auto &output_buffer = router.get_buffer(output_buffer_id);
    bool input_is_scatter_buffer = router.is_buffer_scatter(input_buffer_id);

    TT_ASSERT(input_buffer.chip_location() == output_buffer.chip_location());
    int relay_buffer_size_tiles = (input_is_scatter_buffer) ? router.get_scatter_gather_granularity(input_buffer_id) : 1;

    router.disconnect_pipe(pipe_id);
    router.remove_pipe(pipe_id);

    chip_id_t chip_id = input_buffer.chip_location();
    auto relay_buffer_id = router.add_new_buffer(output_buffer.info(), chip_id);
    removable_buffers.push_back(relay_buffer_id);

    // create the multicast side pipe
    auto input_buffer_to_relay_pipe_id = router.create_unicast_pipe_connection(input_buffer_id, relay_buffer_id, consumer_tile_granularity);
    auto relay_to_ethernet_pipe_id = router.create_unicast_pipe_connection_at_core(relay_buffer_id, output_buffer_id, output_buffer.core_location(), consumer_tile_granularity);

    const auto &pipe_location = choose_valid_pipe_and_buffer_location(router, pipe_segment_id_t{.pipe_id=input_buffer_to_relay_pipe_id, .segment=0}, relay_buffer_id, chip_id, {}, {}, true);
    TT_ASSERT(pipe_location.has_value(), "Couldn't find a valid pipe location, needed for a relay buffer on tensix core to buffer a DRAM read rather than buffering on ethernet");
    router.assign_pipe_to_core(input_buffer_to_relay_pipe_id, pipe_location.value());
    router.assign_buffer_to_core(relay_buffer_id, pipe_location.value());
}

static void insert_relay_buffer_on_output_pipe_from_ethernet_route(
    router::Router &router,
    unique_id_t pipe_id,
    std::vector<unique_id_t> &removable_buffers
) {
    const pipe_t &pipe = router.get_pipe(pipe_id);
    TT_ASSERT(pipe.input_buffer_ids.size() == 1 && !pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1);
    const int consumer_tile_granularity = pipe.consumer_tile_granularity;

    const unique_id_t input_buffer_id = pipe.input_buffer_ids.at(0);
    const auto &input_buffer = router.get_buffer(input_buffer_id);
    const unique_id_t output_buffer_id = pipe.output_buffer_ids().at(0);
    const auto &output_buffer = router.get_buffer(output_buffer_id);
    bool input_is_scatter_buffer = router.is_buffer_scatter(input_buffer_id);

    TT_ASSERT(input_buffer.chip_location() == output_buffer.chip_location());
    int relay_buffer_size_tiles = (input_is_scatter_buffer) ? router.get_scatter_gather_granularity(input_buffer_id) : 1;

    router.disconnect_pipe(pipe_id);
    router.remove_pipe(pipe_id);

    chip_id_t chip_id = input_buffer.chip_location();
    auto relay_buffer_id = router.add_new_buffer(input_buffer.info(), chip_id);
    removable_buffers.push_back(relay_buffer_id);

    // create the multicast side pipe
    auto ethernet_to_relay_pipe_id = router.create_unicast_pipe_connection_at_core(input_buffer_id, relay_buffer_id, input_buffer.core_location(), consumer_tile_granularity);
    auto relay_to_output_pipe_id = router.create_unicast_pipe_connection(relay_buffer_id, output_buffer_id, consumer_tile_granularity);

    const auto &pipe_location_optional = choose_valid_pipe_location(router, pipe_segment_id_t{.pipe_id=relay_to_output_pipe_id, .segment=0}, chip_id, {}, {}, true);
    TT_ASSERT(pipe_location_optional.has_value(), "Couldn't find a valid pipe location, needed for a relay buffer on tensix core to buffer a DRAM read rather than buffering on ethernet");
    tt_cxy_pair const& pipe_location = pipe_location_optional.value();
    router.assign_pipe_to_core(relay_to_output_pipe_id, pipe_location);
    const auto &relay_buffer_location = choose_location_for_buffer(router, chip_id, {pipe_location}, relay_buffer_id);
    router.assign_buffer_to_core(relay_buffer_id, relay_buffer_location);
}


/*
 * Helper function
 */
void create_pipes_between_receiver_and_sender_buffers_within_same_chip(
    router::Router &router, const std::vector<sender_receiver_t> &sender_receiver_bufs_per_route_step, const int consumer_tile_granularity
) {
    for (size_t i = 1; i < sender_receiver_bufs_per_route_step.size(); i++) {
        unique_id_t receiver_buf_id = sender_receiver_bufs_per_route_step.at(i - 1).receiver;
        unique_id_t sender_buf_id = sender_receiver_bufs_per_route_step.at(i).sender;
        router.create_unicast_pipe_connection_at_core(receiver_buf_id, sender_buf_id, router.get_buffer_location(sender_buf_id), consumer_tile_granularity);
    }
}

/*
 * Helper function
 */
void link_original_sender_and_receiver_buffers_to_route(
    router::Router &router, 
    const pipe_t &original_pipe_copy,
    const std::vector<sender_receiver_t> &sender_receiver_bufs_per_route_step,
    const int consumer_tile_granularity,
    std::vector<unique_id_t> &logically_redundant_buffers
) {
    const unique_id_t original_src_buf_id = original_pipe_copy.input_buffer_ids.at(0);
    const unique_id_t original_dest_buf_id = original_pipe_copy.output_buffer_ids().at(0);
    const auto original_src_buf = router.get_buffer(original_src_buf_id);
    const auto original_src_buf_is_scatter = !original_src_buf.is_queue() && original_src_buf.info().is_scatter();

    unique_id_t first_eth_sender_buf = sender_receiver_bufs_per_route_step.front().sender;
    bool same_first_buffer = first_eth_sender_buf == original_src_buf_id;
    std::optional<unique_id_t> source_buffer_output_pipe_id;
    if (!same_first_buffer) {
        const auto &src_to_eth_pipe_location = original_src_buf_is_scatter ? original_src_buf.core_location() : router.get_buffer_location(first_eth_sender_buf);
        source_buffer_output_pipe_id = router.create_unicast_pipe_connection_at_core(original_src_buf_id, first_eth_sender_buf, src_to_eth_pipe_location, consumer_tile_granularity);
    } else {
        const auto &pipe_to_src = router.get_buffer(original_src_buf_id);
        if (router.get_buffer_input_pipes().find(original_src_buf_id) != router.get_buffer_input_pipes().end()) {
            source_buffer_output_pipe_id = router.get_buffer_input_pipes().at(original_src_buf_id);
        }
    }

    bool fw_eth_streams_enabled = env_var("TT_ENABLE_FW_ETH_STREAMS", false) || env_var("TT_FORCE_USE_FW_ETH_STREAMS", false);
    unique_id_t last_eth_receiver_buf = sender_receiver_bufs_per_route_step.back().receiver;
    bool same_last_buffer = last_eth_receiver_buf == original_dest_buf_id;
    if (!same_last_buffer) {
        unique_id_t output_pipe_id = router.create_unicast_pipe_connection_at_core(last_eth_receiver_buf, original_dest_buf_id, router.get_buffer_location(last_eth_receiver_buf), consumer_tile_granularity);
        bool requires_extra_hop_on_receiver_side_after_ethernet = fw_eth_streams_enabled && router.is_queue_buffer(original_dest_buf_id);
        if (requires_extra_hop_on_receiver_side_after_ethernet) {
            insert_relay_buffer_on_output_pipe_from_ethernet_route(router, output_pipe_id, logically_redundant_buffers);
        }
    }
    // If the source pipe requires any additional buffering beyond the prescribed buffers at the endpoints (e.g. to account for read latency),
    // Then we want to insert another relay on a different tensix core to save on buffering capacity that would otherwise be required on the
    // ethernet core (because ethernet L1 is very limited)
    bool requires_extra_buffering_than_available_on_core = source_buffer_output_pipe_id.has_value() && compute_pipe_segment_gather_extra_input_buffering_size(router, pipe_segment_id_t{.pipe_id=source_buffer_output_pipe_id.value(), .segment=0}) > 0;
    bool requires_extra_hop_on_sender_side_before_ethernet = requires_extra_buffering_than_available_on_core || (fw_eth_streams_enabled && router.is_queue_buffer(original_src_buf_id));
    if (requires_extra_hop_on_sender_side_before_ethernet) {
        TT_ASSERT(router.is_queue_buffer(original_src_buf_id), 
            "Only expected additional pipe buffering for unicast pipe on local chip to be for DRAM reads");
        insert_relay_buffer_on_input_pipe_to_ethernet_route(router, source_buffer_output_pipe_id.value(), logically_redundant_buffers);
    }

    
    if (!same_first_buffer && !same_last_buffer) {
        log_trace(tt::LogRouter, "New chip to chip path: original_src_buf_id {} -> first_eth_sender_buf {}. last_eth_receiver_buf {} -> original_dest_buf_id {}", original_src_buf_id, first_eth_sender_buf, last_eth_receiver_buf, original_dest_buf_id);
    } else if (!same_first_buffer) {
        log_trace(tt::LogRouter, "New chip to chip path: original_src_buf_id {} -> first_eth_sender_buf {}", original_src_buf_id, first_eth_sender_buf);
    } else if (!same_last_buffer) {
        log_trace(tt::LogRouter, "New chip to chip path: last_eth_receiver_buf {} -> original_dest_buf_id {}", last_eth_receiver_buf, original_dest_buf_id);
    }

}

/* For ethernet datacopy ops, the input buffer is actually the buffer on the sender side ethernet
 * channel and not the "unpacker" buffer for the ethernet datacopy
 * ...
 * So what we want to do is to grab the downstream buffer from it and mark it as the unpacker buffer
 * If the two buffers aren't already directly connected (i.e. placed on directly connected ethernet
 * channels), then for each of them, we will insert a new buffer on the other end of the link, and
 * update the pipes accordingly. The basic transformation looks like this: Pre-transformation:
 *   {input_buffer} -> pipe -> {"unpacker" buffer}
 *
 * Post-transformation:
 *   {input_buffer} -> new pipe ->
 *      {new_buffer(other end of link from input buffer)} -> new pipe ->
 *          {new buffer (other end of link from "unpacker" buffer)} -> new pipe -> {"unpacker" buffer}
 */
void create_implicit_buffers_for_ethernet_datacopy_op(
    router::Router &router, const std::string &op_name, unique_id_t input_buffer_id) {
    const auto &cluster_graph = router.get_cluster_graph().cluster_description();
    const auto &input_buffer_output_pipes_old = router.get_buffer_output_pipes().at(input_buffer_id);
    TT_ASSERT(input_buffer_output_pipes_old.size() == 1);
    unique_id_t old_pipe_id = *(input_buffer_output_pipes_old.begin());
    unique_id_t unpacker_buffer_id = router.get_pipe(old_pipe_id).output_buffer_ids().at(0);

    const auto &input_buffer = router.get_buffer(input_buffer_id);
    const auto &unpacker_buffer = router.get_buffer(unpacker_buffer_id);
    const auto &producer_consumer_chip_connected_channels =
        cluster_graph.get_directly_connected_ethernet_channels_between_chips(
            input_buffer.chip_location(), unpacker_buffer.chip_location());

    bool buffers_already_directly_connected = producer_consumer_chip_connected_channels.size() > 0;

    if (!buffers_already_directly_connected) {
        // We need to add buffers to the other end of the link for each of input and unpacker buffer
        // and update the pipes accordingly
        const auto &input_chip_soc_descriptor = router.get_soc_descriptor(input_buffer.chip_location());
        ethernet_channel_t input_buffer_channel =
            input_chip_soc_descriptor.get_channel_of_ethernet_core(input_buffer.core_location());
        const auto &[input_connected_chip, input_connected_channel] =
            cluster_graph.get_chip_and_channel_of_remote_ethernet_core(
                input_buffer.chip_location(), input_buffer_channel);
        const auto& input_buffer_neighbour_soc_descriptor = router.get_soc_descriptor(input_connected_chip);
        unique_id_t input_buffer_neighbour_buffer_id = generate_eth_link_endpoint_buffer(
            router, input_buffer_neighbour_soc_descriptor, input_buffer.info(), input_connected_chip, input_connected_channel);

        const auto &output_chip_soc_descriptor = router.get_soc_descriptor(unpacker_buffer.chip_location());
        ethernet_channel_t unpacker_buffer_channel =
            output_chip_soc_descriptor.get_channel_of_ethernet_core(tt_xy_pair(unpacker_buffer.core_location().x, unpacker_buffer.core_location().y));
        const auto &[unpacker_connected_chip, unpacker_connected_channel] =
            cluster_graph.get_chip_and_channel_of_remote_ethernet_core(
                unpacker_buffer.chip_location(), unpacker_buffer_channel);
        const auto &output_buffer_neighbour_soc_descriptor = router.get_soc_descriptor(unpacker_connected_chip);
        unique_id_t unpacker_buffer_neighbour_buffer_id = generate_eth_link_endpoint_buffer(
            router, output_buffer_neighbour_soc_descriptor, unpacker_buffer.info(), unpacker_connected_chip, unpacker_connected_channel);

        int pipe_scatter_granularity = router.get_pipe(old_pipe_id).consumer_tile_granularity;
        router.disconnect_pipe(old_pipe_id);
        router.remove_pipe(old_pipe_id);
        unique_id_t segment_1_pipe_id = router.create_unicast_pipe_connection_at_core(
            input_buffer_id, 
            input_buffer_neighbour_buffer_id, 
            router.get_buffer_location(input_buffer_neighbour_buffer_id), //input_buffer.core_location(), 
            pipe_scatter_granularity);
        unique_id_t segment_2_pipe_id = router.create_unicast_pipe_connection_at_core(
            input_buffer_neighbour_buffer_id,
            unpacker_buffer_neighbour_buffer_id,
            router.get_buffer_location(input_buffer_neighbour_buffer_id),
            pipe_scatter_granularity);
        unique_id_t segment_3_pipe_id = router.create_unicast_pipe_connection_at_core(
            unpacker_buffer_neighbour_buffer_id,
            unpacker_buffer_id,
            unpacker_buffer.core_location(), //router.get_buffer_location(unpacker_buffer_neighbour_buffer_id),
            pipe_scatter_granularity);

        log_trace(tt::LogRouter, "Ethernet datacopy op {}'s input buffer {}, pipe {}, and unpacker buffer {} are not directly connected, so added buffers {} and {} to the other end of their respective links.\nNew connectivity is input buffer {} -> pipe {} -> input buffer neighbour {} -> pipe {} -> unpacker buffer neighbour {} -> pipe {} -> unpacker buffer {}",
            op_name, 
            input_buffer_id, 
            old_pipe_id,
            unpacker_buffer_id,
            input_buffer_neighbour_buffer_id,
            unpacker_buffer_neighbour_buffer_id,
            input_buffer_id,
            segment_1_pipe_id,
            input_buffer_neighbour_buffer_id,
            segment_2_pipe_id,
            unpacker_buffer_neighbour_buffer_id,
            segment_3_pipe_id,
            unpacker_buffer_id
            );
    } else {
        // Thanks Github copilot for finally figuring something out :) (almost)
        auto buffers_occupy_both_ends_of_ethernet_link =
            [&router, &input_buffer, &unpacker_buffer, input_buffer_id, unpacker_buffer_id](const std::tuple<int, int> &channel_pair) {
                const auto &[sender_channel, receiver_channel] = channel_pair;
                bool sender_match =
                    router.buffer_is_on_ethernet_core(input_buffer_id, input_buffer.chip_location(), sender_channel);
                bool receiver_match = router.buffer_is_on_ethernet_core(
                    unpacker_buffer_id, unpacker_buffer.chip_location(), receiver_channel);
                return sender_match && receiver_match;
            };

        bool buffers_are_on_directly_connected_channels =
            std::find_if(
                producer_consumer_chip_connected_channels.begin(),
                producer_consumer_chip_connected_channels.end(),
                buffers_occupy_both_ends_of_ethernet_link) != producer_consumer_chip_connected_channels.end();
        TT_ASSERT(
            buffers_are_on_directly_connected_channels,
            "Ethernet datacopy input and unpacker buffers were added to directly connected chips but not "
            "directly connected channels for ethernet datacopy op {}",
            op_name);
    }
}

void create_implicit_buffers_for_ethernet_datacopy_ops(router::Router &router) {
    for (const auto &[op_name, operand_buffer_grid] : router.get_op_input_buf_map()) {
        tt_op_info const& op_info = router.get_op_info_map().at(op_name);
        if (!netlist_utils::is_valid_ethernet_op(op_info.type)) {
            continue;
        }

        for (const auto &[row, row_operand_buffers] : operand_buffer_grid) {
            for (const auto &[col, operand_buffers] : row_operand_buffers) {
                TT_ASSERT(operand_buffers.size() == 1);
                unique_id_t input_buffer_id = operand_buffers.at(0);
                create_implicit_buffers_for_ethernet_datacopy_op(router, op_name, input_buffer_id);
            }
        }
    }
}

/*
 * Given a chip-to-chip pipe, and explicit route information (chips and ethernet channels for every hop)
 * create the necessary buffers and pipes to implement the route for each hop. This means for every chip along
 * the route, a relay buffer is inserted on the receiver and sender ethernet channel cores. Additionally a pipe
 * connects those relay buffers and the relay buffers on each end of the ethernet link.
 */
void implement_chip_to_chip_route_with_selected_ethernet_channels(
    router::Router &router, 
    const ClusterGraph &cluster_graph, 
    unique_id_t original_chip_to_chip_pipe_id, 
    const tt::buffer_info &eth_buffer_info,
    const std::vector<std::pair<chip_id_t, chip_id_t>> &route,
    const std::vector<std::pair<ethernet_channel_t, ethernet_channel_t>> &ethernet_channels,
    std::vector<unique_id_t> &logically_redundant_buffers
) {
    TT_ASSERT(route.size() == ethernet_channels.size());
    // We take a copy because we need some of the original attributes in later helpers
    pipe_t original_pipe_copy = router.get_pipe(original_chip_to_chip_pipe_id);
    const int consumer_tile_granularity = original_pipe_copy.consumer_tile_granularity;

    router.disconnect_pipe(original_chip_to_chip_pipe_id);
    router.remove_pipe(original_chip_to_chip_pipe_id);

    // Create all the eth buffers and ethernet link pipes (won't create the pipes that go from receiver -> sender within a chip)
    std::vector<sender_receiver_t> sender_receiver_bufs_per_route_step = {};
    for (size_t i = 0; i < route.size(); i++) {
        const auto &[sender_chan, receiver_chan] = ethernet_channels.at(i);
        const auto &[sender_chip, receiver_chip] = route.at(i);

        const auto &[sender_buf_id, receiver_buf_id] = create_eth_link_pipe_and_endpoint_buffers(
            router, original_pipe_copy, sender_chip, sender_chan, receiver_chip, receiver_chan, eth_buffer_info, consumer_tile_granularity);

        sender_receiver_bufs_per_route_step.push_back(sender_receiver_t{.sender=sender_buf_id, .receiver=receiver_buf_id});
    }

    TT_ASSERT(sender_receiver_bufs_per_route_step.size() == route.size());
    create_pipes_between_receiver_and_sender_buffers_within_same_chip(router, sender_receiver_bufs_per_route_step, consumer_tile_granularity);

    link_original_sender_and_receiver_buffers_to_route(
        router, original_pipe_copy, sender_receiver_bufs_per_route_step, consumer_tile_granularity, logically_redundant_buffers);
}

std::stringstream get_route_string_stream(const std::vector<std::pair<chip_id_t, chip_id_t>> &route) {
    auto route_ss = std::stringstream();
    for (const auto &[sender_chip, receiver_chip] : route) {
        route_ss << "(" << sender_chip << "->" << receiver_chip << ")";
    }
    return route_ss;
}

void implement_chip_to_chip_pipe_route(
    router::Router &router, const ClusterGraph &cluster_graph, unique_id_t original_chip_to_chip_pipe_id, 
    const std::vector<std::pair<chip_id_t, chip_id_t>> &route, std::vector<unique_id_t> &logically_redundant_buffers
) {
    log_debug(tt::LogRouter, "Implementing chip-to-chip router for pipe {}. Route: {}", original_chip_to_chip_pipe_id, get_route_string_stream(route).str());

    // The current implementation assumes a 1D cluster topology and for that case, there is only one possible chip-to-chip route. For that reason,
    // when the route is generated, we don't need to test for resource availability beforhand. When upgrading to 2D, will will want to test for resource
    const pipe_t &original_pipe = router.get_pipe(original_chip_to_chip_pipe_id);
    const unique_id_t original_src_buf_id = original_pipe.input_buffer_ids.at(0);
    tt::buffer_info eth_buffer_info = create_relay_buffer_info(
        1, 
        router.get_scatter_gather_granularity(original_src_buf_id),
        router.get_buffer_total_epoch_tiles(original_src_buf_id), 
        router.get_buffer_data_format(original_src_buf_id),
        router.get_buffer_tile_dims(original_src_buf_id)
    );

    const auto &selected_eth_channels_per_route_step = choose_ethernet_channels_along_route(router, cluster_graph, original_chip_to_chip_pipe_id, eth_buffer_info, route);
    implement_chip_to_chip_route_with_selected_ethernet_channels(router, cluster_graph, original_chip_to_chip_pipe_id, eth_buffer_info, route, selected_eth_channels_per_route_step, logically_redundant_buffers);
}


bool pipe_already_routed_over_ethernet_to_connected_channel(const router::Router &router, const ClusterGraph &cluster_graph, unique_id_t pipe_id) {
    const pipe_t &pipe = router.get_pipe(pipe_id);

    bool is_unicast = pipe.input_buffer_ids.size() == 1 && !pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1;
    if (!is_unicast) {
        return false;
    }

    const tt_cxy_pair &input_buffer_core_location = router.get_buffer_location(pipe.input_buffer_ids.at(0));
    const tt_cxy_pair &output_buffer_core_location = router.get_buffer_location(pipe.output_buffer_ids().at(0));
    bool input_buffer_is_on_ethernet_core = router.get_soc_descriptor(input_buffer_core_location.chip).is_ethernet_core(input_buffer_core_location);
    bool output_buffer_is_on_ethernet_core = router.get_soc_descriptor(output_buffer_core_location.chip).is_ethernet_core(output_buffer_core_location);
    if (!input_buffer_is_on_ethernet_core || !output_buffer_is_on_ethernet_core) {
        return false;
    }
    
    const auto &cluster_descriptor = router.get_cluster_graph().cluster_description();
    const auto input_buffer_eth_channel = router.get_soc_descriptor(input_buffer_core_location.chip).get_channel_of_ethernet_core(input_buffer_core_location);
    const auto output_buffer_eth_channel = router.get_soc_descriptor(output_buffer_core_location.chip).get_channel_of_ethernet_core(output_buffer_core_location);
    TT_ASSERT(cluster_descriptor.ethernet_core_has_active_ethernet_link(input_buffer_core_location.chip, input_buffer_eth_channel));
    TT_ASSERT(cluster_descriptor.ethernet_core_has_active_ethernet_link(output_buffer_core_location.chip, output_buffer_eth_channel));
    return cluster_descriptor.channels_are_directly_connected(
        input_buffer_core_location.chip, input_buffer_eth_channel,output_buffer_core_location.chip, output_buffer_eth_channel);
}

/*
 * Choose the route and implement it, for every chip-to-chip unicast pipe. This function is called after chip-to-chip
 * pipe decomposition to ensure only unicast pipes cross chip boundaries.
 *
 * This function does not do exhaustive route creation/exploration and therefore *may* fail to route even if a possible
 * set of chip-to-chip routes is implementable for the design. This delta where a possible solution technically exists
 * but isn't found should be relatively rare.
 *
 * Instead of exhaustive search, we apply a greedy ordering heuristic where we prioritize shorter routes for routing
 * first.
 *
 * The function then chooses a route for each chip-to-chip pipe. This includes the chip path and the exact ethernet
 * channels to route through. To accomplish this, the function must be aware of resource usage or risk generating a
 * workload that is unimplementable in pipegen due to hardware resources being exceeeded.. It makes use of the core
 * resource trackers in router to accomplish this/
 */
void route_unicast_chip_to_chip_pipes(router::Router &router, const ClusterGraph &cluster_graph, std::vector<unique_id_t> &logically_redundant_buffers) {
    // Step 2 - router all the chip-to-chip unicasts over ethernet
    const auto chip_to_chip_pipe_ids = collect_chip_to_chip_pipe_ids_by_distance(router, cluster_graph);

    for (const unique_id_t id : chip_to_chip_pipe_ids) {
        // skip pipes that are already routed through connected ethernet channels. This would happen
        // for ethernet datacopy ops
        if (pipe_already_routed_over_ethernet_to_connected_channel(router, cluster_graph, id)) {
            continue;
        }

        const auto &route = generate_chip_to_chip_route_for_pipe(router, cluster_graph, id);
        if (route.size() == 0) {
            log_error("Couldn't find viable chip to chip path for pipe {}.", id);
            print_pipe_verbose(router, id, tt::Logger::Level::Debug);
            const auto &p = router.get_pipe(id);
            auto sender_chip = router.get_buffer(p.input_buffer_ids.at(0)).chip_location();
            log_error("Eth core resource sender chip: {}", sender_chip);
            dump_ethernet_core_resources(router, sender_chip);
            auto receiver_chip = router.get_buffer(p.output_buffer_ids().at(0)).chip_location();
            log_error("Eth core resource receiver chip: {}", receiver_chip);
            dump_ethernet_core_resources(router, receiver_chip);
            log_fatal("Failed to route.");
        }
        implement_chip_to_chip_pipe_route(router, cluster_graph, id, route, logically_redundant_buffers);
    }
}

tt_cxy_pair choose_location_for_merged_pipe(router::Router &router, unique_id_t old_input_pipe, unique_id_t old_output_pipe) {
    const auto &input_pipe = router.get_pipe(old_input_pipe);
    const auto &output_pipe = router.get_pipe(old_output_pipe);

    bool go_upstream = input_pipe.input_buffer_ids.size() == 1 && !router.get_buffer(input_pipe.input_buffer_ids.at(0)).is_queue();
    if (go_upstream) {
        return router.get_buffer(input_pipe.input_buffer_ids.at(0)).core_location();
    } else {
        TT_ASSERT(output_pipe.output_buffer_ids().size() == 1);
        TT_ASSERT(!router.get_buffer(output_pipe.output_buffer_ids().at(0)).is_queue());
        return router.get_buffer(output_pipe.output_buffer_ids().at(0)).core_location();
    }
}

void remove_redundant_relays(router::Router &router, std::vector<unique_id_t> &buffers_to_delete) {
    for (unique_id_t buffer_id : buffers_to_delete) {
        const auto &buffer = router.get_buffer(buffer_id);
        TT_ASSERT(!buffer.is_queue() && buffer.info().type() == RouterBufferType::Relay);
        
        unique_id_t input_pipe_id = router.get_buffer_input_pipes().at(buffer_id);
        const auto &output_pipes = router.get_buffer_output_pipes().at(buffer_id);
        TT_ASSERT(output_pipes.size() == 1);
        unique_id_t output_pipe_id = *(output_pipes.begin());

        auto input_buffer_list = router.get_pipe(input_pipe_id).input_buffer_ids;
        auto output_buffer_list = router.get_pipe(output_pipe_id).output_buffer_ids();
        bool multi_input = input_buffer_list.size() > 1;
        bool multi_output = output_buffer_list.size() > 1;

        const auto &first_receiver_buffer = router.get_buffer(output_buffer_list.at(0));
        bool read_from_dram = router.get_buffer(input_buffer_list.at(0)).is_queue();

        const auto &soc_desc = router.get_soc_descriptor(first_receiver_buffer.chip_location());
        bool consumer_on_eth_core = !multi_output && 
            soc_desc.ethernet_core_channel_map.find(first_receiver_buffer.core_location()) != soc_desc.ethernet_core_channel_map.end();
        bool read_from_dram_to_eth = consumer_on_eth_core && read_from_dram;
        int extra_input_buffering_in_bytes = 0;
        if (read_from_dram_to_eth) {
            int input_count = input_buffer_list.size();
            int extra_buffering_tiles = input_count == 1 ? std::max(0, first_receiver_buffer.info().allocated_size_in_tiles() - ETH_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES) :
                std::min<int>(input_count * ETH_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES, ETH_GATHER_STREAMED_READ_MAX_INPUT_BUFFER_TILES);
            extra_input_buffering_in_bytes = extra_buffering_tiles * (first_receiver_buffer.info().allocated_size_in_bytes() / first_receiver_buffer.info().allocated_size_in_tiles());
            const auto &receiver_core_attrs = router.get_cluster_resource_model().get_core_attributes(first_receiver_buffer.core_location());
            if (multi_input || receiver_core_attrs.available_l1_bytes() < extra_input_buffering_in_bytes) {
                continue;
            }
        }

        // We'll skip deleting buffers if they are fed by scatter buffers from DRAM because those will require larger relay buffers
        // which may exceed eth L1 sizing constraints easily
        bool inputs_are_queue_scatter_buffers = std::any_of(input_buffer_list.begin(), input_buffer_list.end(), [&router](auto id) { return router.is_queue_buffer(id) && router.is_buffer_scatter(id); });


        int new_pipe_scatter_gather_granularity = router.get_pipe(output_pipe_id).consumer_tile_granularity;
        auto new_pipe_location = choose_location_for_merged_pipe(router, input_pipe_id, output_pipe_id);

        router.disconnect_pipe(input_pipe_id);
        router.disconnect_pipe(output_pipe_id);
        router.remove_buffer(buffer_id);
        router.remove_pipe(input_pipe_id);
        router.remove_pipe(output_pipe_id);
        

        unique_id_t new_pipe_id = 0;
        if (multi_input) {
            if (multi_output) {
                log_fatal("Multi output is not supported"); // shouldn't be possible
                new_pipe_id = router.create_gather_multicast_pipe_connection(input_buffer_list, output_buffer_list, new_pipe_scatter_gather_granularity);
            } else {
                new_pipe_id = router.create_gather_pipe_connection(input_buffer_list, output_buffer_list.at(0), new_pipe_scatter_gather_granularity);
            }
        } else {
            if (multi_output) {
                new_pipe_id = router.create_multicast_pipe_connection(input_buffer_list.at(0), output_buffer_list, new_pipe_scatter_gather_granularity);
            } else {
                new_pipe_id = router.create_unicast_pipe_connection(input_buffer_list.at(0), output_buffer_list.at(0), new_pipe_scatter_gather_granularity);
            }
        }

        TT_ASSERT(new_pipe_id != 0);
        
        router.assign_pipe_to_core(new_pipe_id, new_pipe_location);

        if (extra_input_buffering_in_bytes > 0) {
            TT_ASSERT(read_from_dram_to_eth);
            // For now we don't move gathers onto eth cores because they require much more buffering. We could do it but we need to update a few other
            // places first to properly do the accounting for the extra L1 usage.
            // The extra usage in tiles is `std::min(input_buffer_list.size() * ETH_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES, ETH_GATHER_STREAMED_READ_MAX_INPUT_BUFFER_TILES);
            // We'd need to update router code for dataflow manipulation functions to account for this extra buffering before we can do the merging here
            auto &receiver_core_attrs = router.get_cluster_resource_model().get_core_attributes(first_receiver_buffer.core_location());
            receiver_core_attrs.allocate_bytes(extra_input_buffering_in_bytes);
        }
    }
}


void insert_ethernet_buffers_for_chip_to_chip_pipes(router::Router &router, const ClusterGraph &cluster_graph) {

    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "ALL PIPES");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");

    for (const auto &[id, p]: router.get_pipes()) {
        print_pipe_verbose(router, id, tt::Logger::Level::Trace);
    }
    log_trace(tt::LogRouter, "------------------------");


    const auto chip_to_chip_pipe_ids = collect_chip_to_chip_pipes(router);
    log_debug(tt::LogRouter, "------------------------");
    log_debug(tt::LogRouter, "CHIP TO CHIP PIPES: {}", chip_to_chip_pipe_ids.size());

    log_debug(tt::LogRouter, "------------------------");
    for (const auto id: chip_to_chip_pipe_ids) {
        print_pipe_verbose(router, id, tt::Logger::Level::Debug);
    }
    const auto &chip_to_chip_pipe_ids_dram_to_l1 = collect_chip_to_chip_pipes_dram_to_l1(router, chip_to_chip_pipe_ids);
    const auto &chip_to_chip_pipe_ids_l1_to_l1 = collect_chip_to_chip_pipes_l1_to_l1(router, chip_to_chip_pipe_ids);
    const auto &chip_to_chip_pipe_ids_l1_to_dram = collect_chip_to_chip_pipes_l1_to_dram(router, chip_to_chip_pipe_ids);
    const auto &chip_to_chip_pipe_ids_from_forked_buffers = collect_chip_to_chip_pipes_from_forked_buffers(router, chip_to_chip_pipe_ids);
    const auto &chip_to_chip_pipe_ids_from_forked_dram_buffers_producers = collect_chip_to_chip_pipes_from_forked_dram_buffers_producers(router, chip_to_chip_pipe_ids_from_forked_buffers);
    const auto &chip_to_chip_pipe_ids_from_forked_dram_buffers = collect_chip_to_chip_pipes_from_forked_dram_buffers(router, chip_to_chip_pipe_ids_from_forked_buffers);
    const auto &chip_to_chip_pipe_ids_from_forked_l1_buffers_producers = collect_chip_to_chip_pipes_from_forked_l1_buffers_producers(router, chip_to_chip_pipe_ids_from_forked_buffers);
    const auto &chip_to_chip_pipe_ids_from_forked_l1_buffers = collect_chip_to_chip_pipes_from_forked_l1_buffers(router, chip_to_chip_pipe_ids_from_forked_buffers);

    log_debug(tt::LogRouter, "------------------------");
    log_debug(tt::LogRouter, "CHIP TO CHIP PIPES: {}", chip_to_chip_pipe_ids.size());
    log_debug(tt::LogRouter, "\tDRAM -> L1: {}", chip_to_chip_pipe_ids_dram_to_l1.size());
    log_debug(tt::LogRouter, "\tL1 -> L1  : {}", chip_to_chip_pipe_ids_l1_to_l1.size());
    log_debug(tt::LogRouter, "\tL1 -> DRAM: {}", chip_to_chip_pipe_ids_l1_to_dram.size());
    log_debug(tt::LogRouter, "CHIP TO CHIP PIPES FROM FORKED BUFFERS: {}", chip_to_chip_pipe_ids_from_forked_buffers.size());
    log_debug(tt::LogRouter, "\tFROM DRAM: {}", chip_to_chip_pipe_ids_from_forked_dram_buffers.size());
    log_debug(tt::LogRouter, "\t\t#unique producers: {}", chip_to_chip_pipe_ids_from_forked_dram_buffers_producers.size());
    log_debug(tt::LogRouter, "\tFROM L1: {}", chip_to_chip_pipe_ids_from_forked_l1_buffers.size());
    log_debug(tt::LogRouter, "\t\t#unique producers: {}", chip_to_chip_pipe_ids_from_forked_l1_buffers_producers.size());
    auto ss = std::stringstream();
    emit_chip_to_chip_pipe_ops(ss, router, chip_to_chip_pipe_ids);
    log_debug(tt::LogRouter, "CHIP TO CHIP PRODUCER CONSUMER NAMES\n{}", ss.str());
    log_debug(tt::LogRouter, "------------------------");
    log_debug(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");
    log_trace(tt::LogRouter, "------------------------");

    // Step 1 - decompose all chip-to-chip pipes so that only unicast pipes are 
    create_implicit_buffers_for_ethernet_datacopy_ops(router);

    log_info(tt::LogRouter, "Decomposing non-relocatable chip-to-chip pipes");
    auto logically_redundant_buffers = decompose_interchip_pipes_to_unicast_interchip_pipes(router, cluster_graph, collect_non_relocatable_chip_to_chip_pipes);
    log_info(tt::LogRouter, "Decomposing relocatable chip-to-chip pipes");
    auto logically_redundant_buffers_from_second_pass = decompose_interchip_pipes_to_unicast_interchip_pipes(router, cluster_graph, collect_chip_to_chip_pipes);
    std::copy(
        logically_redundant_buffers_from_second_pass.begin(),
        logically_redundant_buffers_from_second_pass.end(),
        std::back_inserter(logically_redundant_buffers));

    route_unicast_chip_to_chip_pipes(router, cluster_graph, logically_redundant_buffers);

    // remove_redundant_relays(router, buffers_to_delete);
}



}; // namespace router
