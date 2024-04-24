// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router.hpp"
#include "buda_soc_descriptor.h"
#include "buffer_info.h"

#include "common/model/assert.hpp"
#include "core_resource_tracker.h"
#include "netlist/netlist_info_types.hpp"

#include "common/size_lib.hpp"
#include "device/tt_xy_pair.h"

#include "net2pipe_constants.h"
#include "net2pipe_common.h"
#include "net2pipe.h"

#include "router/router_passes.h"
#include "router/router_passes_common.h"

#include "netlist_utils.hpp"
#include "src/net2pipe/inc/core_resource_tracker.h"
#include "src/net2pipe/inc/router.hpp"
#include "src/net2pipe/inc/router_types.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_set>
#include <string_view>
#include <charconv>
#include <vector>


namespace router {


static std::vector<unique_id_t> collect_output_buffer_ids_for_pipe(const pipe_t &pipe) {
    std::vector<unique_id_t> out_buf_ids = {}; 

    if (pipe.has_multiple_timesteps()) {
        for (const auto &timestep_buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
            out_buf_ids.reserve(out_buf_ids.size() + timestep_buf_ids.size());
            std::copy(timestep_buf_ids.begin(), timestep_buf_ids.end(), std::back_inserter(out_buf_ids));
        }
    } else {
        out_buf_ids.reserve(out_buf_ids.size() + pipe.output_buffer_ids().size());
        std::copy(pipe.output_buffer_ids().begin(), pipe.output_buffer_ids().end(), std::back_inserter(out_buf_ids));
    }

    return out_buf_ids;
}

bool pipe_is_buffer_scatter_read_from_dram_into_relay(const Router &router, unique_id_t pipe_id) {
    const auto &pipe = router.get_pipe(pipe_id);
    unique_id_t consumer_buffer_id = pipe.output_buffer_ids().at(0);
    const auto &consumer_buffer = router.get_buffer(consumer_buffer_id);
    unique_id_t producer_buffer_id = pipe.input_buffer_ids.at(0);
    const auto &producer_buffer = router.get_buffer(producer_buffer_id);

    bool input_from_dram = producer_buffer.is_queue();
    bool consumer_is_relay = !consumer_buffer.is_queue() && consumer_buffer.info().type() == RouterBufferType::Relay;

    return input_from_dram && router.is_buffer_scatter(producer_buffer_id) && consumer_is_relay;
}

int compute_num_buffered_inputs(const Router &router, const pipe_t &pipe, bool buffer_scatter_read_from_dram_into_relay) {
    auto count_input_buffer_unique_core_locations = [](const Router &router, const pipe_t &pipe) {
        std::set<tt_cxy_pair> unique_core_locations;
        for (auto buffer_id : pipe.input_buffer_ids) {
            const auto &buffer = router.get_buffer(buffer_id);
            if (!buffer.is_queue()) {
                // ignore queue buffers since they have no location
                unique_core_locations.insert(buffer.core_location());
            }
        }
        return unique_core_locations.size();
    };

    if (buffer_scatter_read_from_dram_into_relay) {
        return 1;
    } else if (pipe.input_buffer_ids.size() == 1) {
        return 0;
    } else {
        bool single_core_input = count_input_buffer_unique_core_locations(router, pipe) == 1;
        if (single_core_input) {
            return 0;
        } else {
            return std::min<int>(pipe.input_buffer_ids.size(), 12);
        }
    }
};

bool requires_extra_input_streams(const Router &router, unique_id_t pipe_id) {
    const auto &pipe = router.get_pipe(pipe_id);
    bool has_scatter_input = false;
    bool has_forked_input_buffer = false;
    bool has_dram_input_buffer = false;
    bool is_scatter_prolog = false;
    bool scatter_reads_ascending = true;

    {
        auto const& in_buf_ids = router.get_pipe(pipe_id).input_buffer_ids;

        unique_id_t first_input_id = in_buf_ids.at(0);
        unique_id_t first_input_base_id = router.get_scatter_buffer_base_id(first_input_id);

        // If all the inputs are (ordered) scatter buffers that span the full buffer then it's also not a gather.
        // We also need to consider the periodic case where each chunk is read round-robin

        auto const& first_buffer = router.get_buffer(first_input_id);
        bool producer_is_queue = first_buffer.is_queue();

        auto producer_buffer_granularity = producer_is_queue ? 
            router.get_queue_output_buffer_granularity(first_buffer.queue_info().get_parent_queue_id()) : 
            first_buffer.info().scatter_gather_num_tiles();

        auto producer_buffer_num_tiles = producer_is_queue ? 
            tt::size::get_entry_size_in_tiles(
                router.get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.ublock_ct,
                router.get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.ublock_rt,
                router.get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.mblock_m,
                router.get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.mblock_n,
                router.get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.t):
            first_buffer.info().allocated_size_in_tiles();

        // Check for the case where we are doing a contiguous read of the buffer, but the pipe
        // lists the ordered, complete set of scatter buffers instead of just one buffer.
        //
        // Another version of this is we gather the input scatter buffers ordered, but where each
        // scatter chunk may be read repeatedly before advancing
        // e.g., 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2
        unique_id_t expected_id = first_input_id;
        unique_id_t last_id = first_input_id;
        unique_id_t expected_last_scatter_buffer_index = producer_buffer_num_tiles - producer_buffer_granularity;
        for (int i = 0; i < in_buf_ids.size(); i++) {
            auto current_id = in_buf_ids.at(i);
            bool matches_last = current_id == last_id;
            bool matches_expected = current_id == expected_id;
            if (!matches_expected and !matches_last) {
                scatter_reads_ascending = false;
                break;
            }
            
            if (matches_expected) {
                // If we matched the last ID, we 
                expected_id += producer_buffer_granularity;
            }
            if (expected_id >= producer_buffer_num_tiles + first_input_base_id) {
                expected_id = first_input_id;
            }
            last_id = current_id;
        }
    }

    
    std::unordered_set<unique_id_t> unique_base_id_buffers = {};
    for (unique_id_t input_buffer_id : pipe.input_buffer_ids) {
        unique_id_t base_id = router.is_buffer_scatter(input_buffer_id) ? router.get_scatter_buffer_base_id(input_buffer_id) : input_buffer_id;
        unique_base_id_buffers.insert(base_id);

        has_scatter_input = has_scatter_input || router.is_buffer_scatter(input_buffer_id);
        has_dram_input_buffer = has_dram_input_buffer || router.is_queue_buffer(base_id);
        is_scatter_prolog = is_scatter_prolog or (router.is_buffer_scatter(input_buffer_id) and router.is_queue_buffer(base_id) and router.get_buffer(base_id).queue_info().is_prolog());
    }

    // # gather input streams
    // if scatter pipe (inputs are scatter buffers)
    //   -> get_pipe_scatter_input_index_of_scatter_buf
    // if (!streaming ??) multicast with scatter inputs OR 
    //    no scattered inputs and not un-forked DRAM and !post_tm && # inputs > 1 && pipe_reduce_mem_usage
    // Requires extra gather streams boolean adapted from pipege code in `get_phase_info_maps` where we compute
    // .num_ginput_streams 
    bool single_output = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(0).size() == 1 : pipe.output_buffer_ids().size() == 1;
    bool is_pipe_scatter_single_core = has_dram_input_buffer || 
        is_scatter_prolog || 
        (
            (
                unique_base_id_buffers.size() == 1 /* || 
                (false && first_loc.has_value() && std::all(pipe.input_buffer_ids.begin(), pipe.input_buffer_ids.end(), [&router, first_loc] (unique_id_t id) })) */
            ) && 
            single_output
        );
    bool is_pipe_scatter_multicore = has_scatter_input && !is_pipe_scatter_single_core && !has_dram_input_buffer;
    
    !router.is_queue_buffer(pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(0).at(0) : pipe.output_buffer_ids().at(0)) &&
        (pipe.has_multiple_timesteps() ? 
            std::any_of(
                pipe.time_multiplexed_output_buffer_ids().begin(), pipe.time_multiplexed_output_buffer_ids().end(),[](const auto &x) { return x.size() > 1; }) :  
                pipe.output_buffer_ids().size() > 1
        );
    bool second_condition = !has_scatter_input && 
        (!has_dram_input_buffer /*|| !has_forked_input_buffer*/) &&   // need to do some extra work to make querying if a buffer is forked (since we'd have to look at each chunk of a scatter buffer too)
                                                                    // Can cache results of `collect_pipe_attributes` and update for every pipe manip function 
        (unique_base_id_buffers.size() > 1);
    bool requires_extra_gather_streams = !scatter_reads_ascending && (is_pipe_scatter_multicore || (second_condition || has_dram_input_buffer));
    return requires_extra_gather_streams;
}

// 1 stream per fork (output) if relay

int compute_pipe_segment_gather_extra_stream_use(
    const Router &router, pipe_segment_id_t const &pipe_segment_id, const tt_cxy_pair &core_location) {
    unique_id_t pipe_id = pipe_segment_id.pipe_id;
    const auto &pipe = router.get_pipe(pipe_id); 
    std::unordered_set<unique_id_t> unique_input_buffer_base_ids = {};
    bool has_scatter_input = false;
    bool has_forked_input_buffer = false;
    bool has_dram_input_buffer = false;
    for (unique_id_t input_buffer_id : pipe.input_buffer_ids) {
        unique_id_t base_id = router.is_buffer_scatter(input_buffer_id)
                                  ? router.get_scatter_buffer_base_id(input_buffer_id)
                                  : input_buffer_id;
        unique_input_buffer_base_ids.insert(base_id);
        const auto &base_buffer = router.get_buffer(base_id);
        has_scatter_input = has_scatter_input || router.is_buffer_scatter(input_buffer_id);
        has_dram_input_buffer = has_dram_input_buffer || router.is_queue_buffer(base_id);
    }

    bool add_extra_stream_per_dram_input = requires_extra_input_streams(router, pipe_id);
    int num_extra_streams = add_extra_stream_per_dram_input ? unique_input_buffer_base_ids.size() : 0;
    num_extra_streams = std::min<int>(num_extra_streams, MAX_STREAMS_USED_FOR_OPTIMIZED_GATHER);

    int number_of_other_gather_pipes_on_core = router.get_cluster_resource_model()
                                                   .get_core_attributes(core_location)
                                                   .get_number_of_resident_gather_multicast_pipes();
    bool limit_to_single_extra_gather_stream =
        number_of_other_gather_pipes_on_core >= router.get_max_gather_pipes_before_phase_based_gather();
    if (num_extra_streams > 1 && limit_to_single_extra_gather_stream) {
        num_extra_streams = 1;
    }
    return num_extra_streams;
}

int compute_pipe_segment_gather_extra_stream_use(
    const Router &router, pipe_segment_id_t const &pipe_segment_id) {
    pipe_t const &pipe = router.get_pipe(pipe_segment_id.pipe_id);
    return compute_pipe_segment_gather_extra_stream_use(
        router, pipe_segment_id, pipe.scatter_segment_core_location(pipe_segment_id.segment));
}


int compute_pipe_segment_extra_stream_use(const Router &router, pipe_segment_id_t const& pipe_segment_id, const tt_cxy_pair &core_location) {
    pipe_t const& pipe = router.get_pipe(pipe_segment_id.pipe_id);
    int mcast_streams = 0; // mcast doesn't use the extra stream budget.
    int gather_streams = compute_pipe_segment_gather_extra_stream_use(router, pipe_segment_id, core_location);
    int extra_streams = gather_streams + mcast_streams;
    if (gather_streams > 1 &&
        router.get_cluster_resource_model()
                .get_core_attributes(core_location)
                .get_number_of_resident_gather_multicast_pipes() >= router.get_max_gather_pipes_before_phase_based_gather()) {
        extra_streams = 1;
    }

    return extra_streams;
}


int compute_pipe_segment_extra_stream_use(Router const& router, pipe_segment_id_t const& pipe_segment_id) {
    pipe_t const& pipe = router.get_pipe(pipe_segment_id.pipe_id);
    return compute_pipe_segment_extra_stream_use(router, pipe_segment_id, pipe.core_location());
}

bool pipe_requires_input_buffering(const Router &router, unique_id_t pipe_id) {    
    const auto &pipe = router.get_pipe(pipe_id);

    if (pipe.has_multiple_timesteps()) {
        return false;
    }

    return pipe_is_buffer_scatter_read_from_dram_into_relay(router, pipe_id) || (pipe.input_buffer_ids.size() > 1);
}

int compute_pipe_segment_gather_extra_input_buffering_size(
    const Router &router, pipe_segment_id_t const &pipe_segment_id) {
    unique_id_t pipe_id = pipe_segment_id.pipe_id;
    const auto &pipe = router.get_pipe(pipe_id);

    if (pipe.has_multiple_timesteps()) {
        return 0;
    }

    unique_id_t consumer_buffer_id = pipe.output_buffer_ids().at(0);
    const auto &consumer_buffer = router.get_buffer(consumer_buffer_id);
    unique_id_t producer_buffer_id = pipe.input_buffer_ids.at(0);
    const auto &producer_buffer = router.get_buffer(producer_buffer_id);

    bool consumer_is_relay = !consumer_buffer.is_queue() && consumer_buffer.info().type() == RouterBufferType::Relay;
    bool consumer_is_tensix = !consumer_buffer.is_queue() && (!consumer_buffer.core_location_assigned() || router.get_soc_descriptor(consumer_buffer.chip_location()).is_worker_core(consumer_buffer.core_location()));
    bool input_from_dram = producer_buffer.is_queue();
    bool producer_is_prolog = input_from_dram && producer_buffer.queue_info().is_prolog();
    if (producer_is_prolog) {
        return 0;
    }

    // If pipe scatter single core or multicore: # scatter bufs * storage size bytes 

    // if pipe is reading from DRAM scatter buffer into relay, we don't need extra streams but we do need to add some input buffering by scaling the
    // relay buffer size until it exceeds `DRAM_INPUT_STREAM_MAX_PENDING_READ_BYTES`.
    bool buffer_scatter_read_from_dram_into_relay = pipe_is_buffer_scatter_read_from_dram_into_relay(router, pipe_id);//input_from_dram && router.is_buffer_scatter(producer_buffer_id) && consumer_is_relay;    

    int num_buffered_inputs = compute_num_buffered_inputs(router, pipe, buffer_scatter_read_from_dram_into_relay);    

    tt::DataFormat data_format = router.get_buffer_data_format(producer_buffer_id);
    int tile_size_in_bytes = tt::size::get_tile_size_in_bytes(data_format, true);
    int scale_factor = 1;
    if (buffer_scatter_read_from_dram_into_relay) {
        const int pipe_min_dram_buf_size = router.is_ethernet_pipe(pipe_id) ? PIPE_REDUCE_MEM_USAGE_THRESHOLD_ETHERNET : DRAM_INPUT_STREAM_MAX_PENDING_READ_BYTES;
        scale_factor = pipe_min_dram_buf_size / (consumer_buffer.info().scatter_gather_num_tiles() * tile_size_in_bytes);// consumer_buffer.info().size_in_bytes();
        scale_factor = std::max(1, scale_factor); // in case relay buffer is larger than pipe_min_dram_buf_size
    }

    int extra_tiles = num_buffered_inputs * 
        (buffer_scatter_read_from_dram_into_relay ? 
            consumer_buffer.info().scatter_gather_num_tiles() * scale_factor :// consumer_buffer.info().size_in_tiles() * scale_factor :
            (consumer_is_tensix ? WORKER_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES : ETH_GATHER_STREAMED_READ_INPUT_BUFFER_NUM_TILES)
        );
    int extra_bytes = tile_size_in_bytes * extra_tiles;

    bool is_multicast_pipe = pipe.output_buffer_ids().size() > 1;
    if (is_multicast_pipe) {
        extra_bytes += tt::size::get_tile_size_in_bytes(router.get_buffer_data_format(producer_buffer_id), true) * 4; // TODO: find the constant that maps to this 4 times multiple for mcast buffering
    }

    if (input_from_dram) {
        extra_bytes = extra_bytes * 2 > (100 * 1024/*PIPE_REDUCE_MEM_USAGE_THRESHOLD*/) ? extra_bytes: extra_bytes * 2;
    }

    return extra_bytes;
}


static std::tuple<std::vector<unique_id_t>,std::vector<unique_id_t>> collect_buffer_ids_for_pipe(const pipe_t &pipe) {
    return {pipe.input_buffer_ids, collect_output_buffer_ids_for_pipe(pipe)};
};

static void add_buffer_locations (const Router &router, const std::vector<unique_id_t> &buffer_ids, std::unordered_set<tt_cxy_pair> &cores) {
    for (auto id : buffer_ids) {
        const auto &buffer = router.get_buffer(id);
        if (buffer.core_location_assigned()) {
            if (buffer.is_queue() && buffer.queue_info().get_allocation_info().is_host_mapped) {
                continue;
            }
            cores.insert(buffer.core_location());
        }
    }
}


static std::unordered_set<tt_cxy_pair> collect_pipe_endpoint_cores(const Router &router, unique_id_t pipe_id) {
    auto endpoint_cores = std::unordered_set<tt_cxy_pair>{};
    const auto &pipe = router.get_pipe(pipe_id);

    add_buffer_locations(router, pipe.input_buffer_ids, endpoint_cores);
    if (pipe.has_multiple_timesteps()) {
        for (const auto &output_buffers : pipe.time_multiplexed_output_buffer_ids()) {
            add_buffer_locations(router, output_buffers, endpoint_cores);
        }
    } else {
        add_buffer_locations(router, pipe.output_buffer_ids(), endpoint_cores);
    }

    return endpoint_cores;
}

int get_number_of_active_dram_queues(const Router &router, unique_id_t pipe_id) {
    const auto &pipe = router.get_pipe(pipe_id);
    const auto &in_buf_ids = pipe.input_buffer_ids;

    std::unordered_set<unique_id_t> dram_queue_base_buffer_ids = {};
    const auto &buffer_output_pipes = router.get_buffer_output_pipes();
    for(auto id : in_buf_ids) { 
        unique_id_t base_id = router.is_buffer_scatter(id) ? router.get_scatter_buffer_base_id(id) : id;
        if(router.is_queue_buffer(id) && !router.get_buffer(id).queue_info().is_prolog()) {
            dram_queue_base_buffer_ids.insert(base_id);
        }
    }

    int active_dram_queues = dram_queue_base_buffer_ids.size();
    return active_dram_queues;
}

bool Router::is_pipe_scatter_single_core(unique_id_t pipe_id) const {
    
    const pipe_t &pipe = this->get_pipe(pipe_id);
    const auto &input_buffer_ids = pipe.input_buffer_ids;
    const auto &first_input_buf = this->get_buffer(input_buffer_ids.at(0));

    if (!is_buffer_scatter(input_buffer_ids.at(0))) {
        return false;
    }

    const std::optional<tt_cxy_pair> &first_input_buf_location = first_input_buf.core_location_assigned() ? std::optional<tt_cxy_pair>(first_input_buf.core_location()) : std::nullopt;
    bool all_same_producer_core = std::all_of(input_buffer_ids.begin(), input_buffer_ids.end(), [this, &first_input_buf_location] (auto id) -> bool {
        const auto &buffer = this->get_buffer(id);
        const std::optional<tt_cxy_pair> &buf_location = buffer.core_location_assigned() ? std::optional<tt_cxy_pair>(buffer.core_location()) : std::nullopt;
        return buf_location == first_input_buf_location;
    });

    if (!all_same_producer_core) {
        return false;
    }

    bool is_queue = first_input_buf.is_queue();
    bool all_same_queue_status = std::all_of(input_buffer_ids.begin(), input_buffer_ids.end(), [this, &is_queue] (auto id) -> bool {
        return this->get_buffer(id).is_queue() == is_queue;
    });

    if (!all_same_queue_status) {
        return false;
    }

    return true;
}

bool Router::pipe_implements_gather(pipe_segment_id_t const &pipe_segment_id) const { 
    auto const& in_buf_ids = this->get_pipe(pipe_segment_id.pipe_id).input_buffer_ids;
    if (in_buf_ids.size() <= 1) { 
        return false; 
    }

    unique_id_t first_input_id = in_buf_ids.at(0);
    unique_id_t first_input_base_id = this->get_scatter_buffer_base_id(first_input_id);
    bool all_inputs_same = std::count(in_buf_ids.begin(), in_buf_ids.end(), first_input_id) == in_buf_ids.size();
    if (all_inputs_same) { 
        return false; 
    }

    // If all the inputs are (ordered) scatter buffers that span the full buffer then it's also not a gather.
    // We also need to consider the periodic case where each chunk is read round-robin

    bool first_buffer_is_zero_offset = this->get_scatter_buffer_base_id(first_input_id) == first_input_id;
    if (!first_buffer_is_zero_offset) {
        return false;
    }

    auto const& first_buffer = this->get_buffer(first_input_id);
    bool producer_is_queue = this->buffer_map.at(first_input_id).is_queue();

    auto producer_buffer_granularity = producer_is_queue ? 
        this->get_queue_output_buffer_granularity(first_buffer.queue_info().get_parent_queue_id()) : 
        first_buffer.info().scatter_gather_num_tiles();

    auto producer_buffer_num_tiles = producer_is_queue ? 
        tt::size::get_entry_size_in_tiles(
            this->get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.ublock_ct,
            this->get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.ublock_rt,
            this->get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.mblock_m,
            this->get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.mblock_n,
            this->get_queue(first_buffer.queue_info().get_parent_queue_id()).dim.t):
        first_buffer.info().allocated_size_in_tiles();

    // Check for the case where we are doing a contiguous read of the buffer, but the pipe
    // lists the ordered, complete set of scatter buffers instead of just one buffer.
    //
    // Another version of this is we gather the input scatter buffers ordered, but where each
    // scatter chunk may be read repeatedly before advancing
    // e.g., 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2
    unique_id_t expected_id = first_input_id;
    unique_id_t last_id = first_input_id;
    unique_id_t expected_last_scatter_buffer_index = producer_buffer_num_tiles - producer_buffer_granularity;
    for (int i = 0; i < in_buf_ids.size(); i++) {
        auto current_id = in_buf_ids.at(i);
        bool matches_last = current_id == last_id;
        bool matches_expected = current_id == expected_id;
        if (!matches_expected and !matches_last) {
            return true;
        }
        
        if (matches_expected) {
            // If we matched the last ID, we 
            expected_id += producer_buffer_granularity;
        }
        if (expected_id >= producer_buffer_num_tiles + first_input_base_id) {
            expected_id = first_input_id;
        }
        last_id = current_id;
    }

    return false;
}

pipe_resource_attributes_t Router::collect_pipe_segment_attributes(pipe_segment_id_t const &pipe_segment_id) const {
    auto is_queue_buffer = [this](auto id) -> bool {
        return this->get_buffer(id).is_queue() && !this->get_buffer(id).queue_info().is_prolog();
    };
    auto get_buffer_chip = [this](auto id) -> chip_id_t { return this->get_buffer(id).chip_location(); };

    const auto &pipe = this->get_pipe(pipe_segment_id.pipe_id);
    const auto &in_buf_ids = pipe.input_buffer_ids;

    auto pipe_attrs = pipe_resource_attributes_t{};

    bool is_scatter_pipe_gathering_from_multiple_cores = pipe.is_scatter();
    if (pipe.is_scatter()) {
        auto const& first_input_buf = this->get_buffer(in_buf_ids.at(0));
        tt_cxy_pair const& first_input_core = first_input_buf.core_location_assigned() ? first_input_buf.core_location() : tt_cxy_pair(first_input_buf.chip_location(), 255, 255);
        auto buffer_location_is_different = [this, &first_input_core](unique_id_t id) -> bool {
            auto const& buffer = this->get_buffer(id);
            return (buffer.core_location_assigned() ? buffer.core_location() : tt_cxy_pair(buffer.chip_location(), 255, 255)) != first_input_core;
        };
        is_scatter_pipe_gathering_from_multiple_cores = std::any_of(in_buf_ids.begin(), in_buf_ids.end(), buffer_location_is_different);
    }

    pipe_attrs.has_input_from_dram = std::any_of(in_buf_ids.begin(), in_buf_ids.end(), is_queue_buffer);
    {
        const auto &out_buf_ids = pipe.output_segment_output_buffer_ids(pipe_segment_id.segment);
        pipe_attrs.has_output_to_dram.push_back(std::any_of(out_buf_ids.begin(), out_buf_ids.end(), is_queue_buffer));

        pipe_attrs.output_chips.push_back({});
        auto &output_chips = pipe_attrs.output_chips.back();
        std::for_each(out_buf_ids.begin(), out_buf_ids.end(), [this, &output_chips, &get_buffer_chip](auto id) { output_chips.insert(get_buffer_chip(id)); });
        pipe_attrs.has_multicast = pipe_attrs.has_multicast || (out_buf_ids.size() > 1);
    }
    pipe_attrs.has_multicast = pipe_attrs.has_multicast || is_scatter_pipe_gathering_from_multiple_cores;

    auto &input_chips = pipe_attrs.input_chips;
    std::for_each(in_buf_ids.begin(), in_buf_ids.end(), [this, &input_chips, &get_buffer_chip](auto id) { input_chips.insert(get_buffer_chip(id)); });

    for(auto id : in_buf_ids) { 
        bool add_to_forked_buffer_count = this->buffer_output_pipes.find(id) != this->buffer_output_pipes.end() && 
            this->buffer_output_pipes.at(id).size() > 1 &&
            (!is_buffer_scatter(id) || get_scatter_buffer_base_id(id) == id);
        if (add_to_forked_buffer_count) { 
            pipe_attrs.forked_input_buffers.insert(id); 
        }
    }

    pipe_attrs.active_dram_queues = get_number_of_active_dram_queues(*this, pipe_segment_id.pipe_id);
    pipe_attrs.has_gather = requires_extra_input_streams(*this, pipe_segment_id.pipe_id);

    return pipe_attrs;
}

pipe_resource_attributes_t Router::collect_pipe_attributes(unique_id_t pipe_id) const {
    
    auto is_queue_buffer = [this] (auto id) -> bool { return this->get_buffer(id).is_queue() && !this->get_buffer(id).queue_info().is_prolog(); };
    auto get_buffer_chip = [this] (auto id) -> chip_id_t { return this->get_buffer(id).chip_location(); };
    
    const auto &pipe = this->get_pipe(pipe_id);
    const auto &in_buf_ids = pipe.input_buffer_ids;

    auto pipe_attrs = pipe_resource_attributes_t{};
    
    pipe_attrs.has_input_from_dram = std::any_of(in_buf_ids.begin(), in_buf_ids.end(), is_queue_buffer);
    if (pipe.has_multiple_timesteps()) {
        for (const auto &out_buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
            pipe_attrs.has_output_to_dram.push_back(std::any_of(out_buf_ids.begin(), out_buf_ids.end(), is_queue_buffer));

            pipe_attrs.output_chips.push_back({});
            auto &output_chips = pipe_attrs.output_chips.back();
            std::for_each(out_buf_ids.begin(), out_buf_ids.end(), [this, &output_chips, &get_buffer_chip](auto id) { output_chips.insert(get_buffer_chip(id)); });
            pipe_attrs.has_multicast = pipe_attrs.has_multicast || (out_buf_ids.size() > 1);
        }
        pipe_attrs.has_multicast = pipe_attrs.has_multicast || pipe.input_buffer_ids.size() > 0;
    } else {
        const auto &out_buf_ids = pipe.output_buffer_ids();
        pipe_attrs.has_output_to_dram.push_back(std::any_of(out_buf_ids.begin(), out_buf_ids.end(), is_queue_buffer));

        pipe_attrs.output_chips.push_back({});
        auto &output_chips = pipe_attrs.output_chips.back();
        std::for_each(out_buf_ids.begin(), out_buf_ids.end(), [this, &output_chips, &get_buffer_chip](auto id) { output_chips.insert(get_buffer_chip(id)); });

        pipe_attrs.has_multicast = pipe.output_buffer_ids().size() > 1;
    }

    auto &input_chips = pipe_attrs.input_chips;
    std::for_each(in_buf_ids.begin(), in_buf_ids.end(), [this, &input_chips, &get_buffer_chip](auto id) { input_chips.insert(get_buffer_chip(id)); });

    for(auto id : in_buf_ids) { 
        bool add_to_forked_buffer_count = this->buffer_output_pipes.find(id) != this->buffer_output_pipes.end() && 
            this->buffer_output_pipes.at(id).size() > 1 &&
            (!is_buffer_scatter(id) || get_scatter_buffer_base_id(id) == id);
        if (add_to_forked_buffer_count) { 
            pipe_attrs.forked_input_buffers.insert(id); 
        }
    }

    pipe_attrs.active_dram_queues = get_number_of_active_dram_queues(*this, pipe_id);
    pipe_attrs.has_gather = requires_extra_input_streams(*this, pipe_id);

    return pipe_attrs;
}

bool Router::is_ethernet_pipe(unique_id_t pipe_id) const {
    const auto &pipe = get_pipe(pipe_id);
    if (pipe.input_buffer_ids.size() != 1 || pipe.output_buffer_ids().size() != 1) {
        return false;
    }
    const auto &input_buffer = get_buffer(pipe.input_buffer_ids.at(0));
    const auto &output_buffer = get_buffer(pipe.output_buffer_ids().at(0));
    if (input_buffer.is_queue() || output_buffer.is_queue()) {
        return false;
    }
    if (!input_buffer.core_location_assigned() || !output_buffer.core_location_assigned()) {
        return false;
    }
    const auto &soc_desc = get_soc_descriptor(input_buffer.chip_location());
    return soc_desc.is_ethernet_core(input_buffer.core_location()) &&
        soc_desc.is_ethernet_core(output_buffer.core_location());
}


void Router::add_pipe_resource_usage_to_core(pipe_segment_id_t const& pipe_segment_id, std::optional<tt_cxy_pair> const& optional_core_location) {
    unique_id_t pipe_id = pipe_segment_id.pipe_id;
    const auto &pipe = get_pipe(pipe_segment_id.pipe_id);

    std::optional<tt_cxy_pair> const &core_location =
        !optional_core_location.has_value()
            ? (pipe.core_location_assigned() ? pipe.scatter_segment_core_location(pipe_segment_id.segment)
                                             : std::optional<tt_cxy_pair>(std::nullopt))
            : optional_core_location.value();

    if (core_location.has_value()) {
        const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id);
        bool has_any_output_to_dram = std::any_of(pipe_attrs.has_output_to_dram.begin(), pipe_attrs.has_output_to_dram.end(), [](bool i) { return i; });

        tt_cxy_pair const& pipe_location = core_location.value();
        auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(pipe_location);

        if (pipe_attrs.has_input_from_dram) {
            core_resource_tracker.adjust_input_from_dram(1);
        }
        if (has_any_output_to_dram) {
            core_resource_tracker.adjust_outputs_to_dram(1);
        }

        TT_ASSERT(std::all_of(
            pipe_attrs.forked_input_buffers.begin(), pipe_attrs.forked_input_buffers.end(), [this](auto id) {
                return this->buffer_output_pipes.at(id).size() > 1;
            }), "ex");

        int extra_pipe_input_streams = compute_pipe_segment_extra_stream_use(*this, pipe_segment_id, pipe_location);

        if (pipe_attrs.has_gather && pipe_attrs.has_multicast) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). register_gather_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_resource_tracker.register_resident_gather_multicast_pipe(
                create_pipe_segment_hash(*this, pipe_segment_id),
                pipe.input_buffer_ids.size());
        } else if (pipe_attrs.has_gather) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). register_gather_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_resource_tracker.register_resident_gather_pipe(
                create_pipe_segment_hash(*this, pipe_segment_id),
                pipe.input_buffer_ids.size());
        } else if (pipe_attrs.has_multicast) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). register_mcast_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_resource_tracker.register_resident_multicast_pipe(create_pipe_segment_hash(*this, pipe_segment_id));
        }

        int gather_pipe_extra_input_buffering_bytes = compute_pipe_segment_gather_extra_input_buffering_size(*this, pipe_segment_id);
        int old_amount = core_resource_tracker.get_num_allocated_bytes();
        core_resource_tracker.allocate_bytes(gather_pipe_extra_input_buffering_bytes);
        int new_amount = core_resource_tracker.get_num_allocated_bytes();
        int delta = new_amount - old_amount;
        if (pipe_attrs.has_input_from_dram && delta == 0) {
            core_resource_tracker.adjust_extra_streams(1);
        }
        log_trace(
            tt::LogRouter,
            "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). {} B + {} B = {} B in L1 ({} B "
            "remaining), Extra input streams = {}",
            pipe_id,
            pipe_location.chip,
            pipe_location.y,
            pipe_location.x,
            old_amount,
            delta,
            new_amount,
            core_resource_tracker.available_l1_bytes(),
            extra_pipe_input_streams);

        // compute required active dram queues
        if (pipe_attrs.active_dram_queues > 0) {
            this->cluster_resource_model.get_core_attributes(pipe_location).adjust_active_dram_queues(pipe_attrs.active_dram_queues);
        }
    }

    if (is_chip_to_chip_pipe(get_pipe(pipe_id), *this)) {
        auto endpoint_cores = collect_pipe_endpoint_cores(*this, pipe_id);

        for (const auto &loc : endpoint_cores) {
            this->cluster_resource_model.get_core_attributes(loc).adjust_ethernet_streams(1);
        }
    }

}


void Router::remove_pipe_resource_usage_from_core(pipe_segment_id_t const& pipe_segment_id) {
    unique_id_t pipe_id = pipe_segment_id.pipe_id;
    int segment = pipe_segment_id.segment;
    const auto &pipe = get_pipe(pipe_id);

    if (pipe.locations.size() > segment) {
        auto pipe_locations_set = std::set<tt_cxy_pair>(pipe.locations.begin(), pipe.locations.end());
        const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id);

        tt_cxy_pair const& pipe_location = pipe.scatter_segment_core_location(segment);
        
        auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(pipe_location);
        
        if (pipe_attrs.has_input_from_dram) {
            core_resource_tracker.adjust_input_from_dram(-1);
        }
        bool has_any_output_to_dram = std::any_of(pipe_attrs.has_output_to_dram.begin(), pipe_attrs.has_output_to_dram.end(), [](bool i) { return i; });
        if (has_any_output_to_dram) {
            core_resource_tracker.adjust_outputs_to_dram(-1);
        }
        TT_ASSERT(std::all_of(
            pipe_attrs.forked_input_buffers.begin(), pipe_attrs.forked_input_buffers.end(), [this](auto id) {
                return this->buffer_output_pipes.at(id).size() > 1;
            }));

        int extra_streams_to_remove = compute_pipe_segment_extra_stream_use(*this, pipe_segment_id, pipe.locations.at(pipe_segment_id.segment));
        auto &core_attrs = this->cluster_resource_model.get_core_attributes(pipe_location);
        
        if (pipe_attrs.has_gather && pipe_attrs.has_multicast) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). deregister_resident_gather_multicast_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_attrs.deregister_resident_gather_multicast_pipe(create_pipe_segment_hash(*this, pipe_segment_id));
        } else if (pipe_attrs.has_gather) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). deregister_resident_gather_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_attrs.deregister_resident_gather_pipe(create_pipe_segment_hash(*this, pipe_segment_id));
        } else if (pipe_attrs.has_multicast) {
            log_trace(tt::LogRouter, "Router::add_pipe_resource_usage_to_core pipe {} to core (c={},y={},x={}). deregister_resident_multicast_pipe", pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x);
            core_attrs.deregister_resident_multicast_pipe(create_pipe_segment_hash(*this, pipe_segment_id));
        }
        
        if (pipe_attrs.has_input_from_dram && !pipe_attrs.has_gather) {
            core_resource_tracker.adjust_extra_streams(-1);
        }

        int extra_input_buffering_bytes = compute_pipe_segment_gather_extra_input_buffering_size(*this, pipe_segment_id);
        if (extra_input_buffering_bytes > 0) {
            core_attrs.allocate_bytes(-extra_input_buffering_bytes);
        }
        log_trace(tt::LogRouter, "Router::remove_pipe_resource_usage_from_core (c={},y={},x={}): {} B returned to L1. Remaining L1 capacity: {} B. {} extra streams from core (c={},y={},x={}) for multiple input side of gather pipe", 
            pipe_location.chip, pipe_location.y, pipe_location.x, extra_input_buffering_bytes, core_attrs.available_l1_bytes(),
            extra_streams_to_remove, pipe_locations_set.begin()->chip, pipe_locations_set.begin()->y, pipe_locations_set.begin()->x);

        if (pipe_attrs.active_dram_queues > 0 && pipe_locations_set.size() > 0) {
            TT_ASSERT(pipe_locations_set.size() == 1);
            const auto &loc = *(pipe_locations_set.begin());
            this->cluster_resource_model.get_core_attributes(loc).adjust_active_dram_queues(-pipe_attrs.active_dram_queues);
        }
    }


    if (is_chip_to_chip_pipe(get_pipe(pipe_id), *this)) {
        auto endpoint_cores = collect_pipe_endpoint_cores(*this, pipe_id);

        for (const auto &loc : endpoint_cores) {
            this->cluster_resource_model.get_core_attributes(loc).adjust_ethernet_streams(-1);
        }
    }

}


unique_id_t Router::get_scatter_buffer_base_id(unique_id_t scatter_buffer_id) const { 
    unique_id_t base_id = scatter_buffer_id - (scatter_buffer_id % this->UNIQUE_ID_ALIGN);
    TT_ASSERT(this->buffer_map.find(base_id) != this->buffer_map.end());
    return base_id;
}


tt_xy_pair Router::get_buffer_tile_dims(unique_id_t buffer_id) const {
    const auto &buffer = this->get_buffer(buffer_id);
    if (buffer.is_queue()) {
        const auto &queue_info = this->get_queue(buffer.queue_info().get_parent_queue_id());
        return tt_xy_pair(n2p::get_tile_width(queue_info.tile_dim), n2p::get_tile_height(queue_info.tile_dim));
    } else {
        return buffer.info().tile_dims();
    }
}


int Router::get_buffer_total_epoch_tiles(const router_buffer_info_t &buf_info) const {
    if (buf_info.is_queue()) {
        const auto &queue_info = this->get_queue(buf_info.queue_info().get_parent_queue_id());
        return queue_info.input_count * get_entry_size_in_tiles(queue_info);
    } else {
        return buf_info.info().total_epoch_tiles();
    }
}
int Router::get_buffer_total_epoch_tiles(unique_id_t id) const {
    const auto &buf = this->buffer_map.at(id);
    return this->get_buffer_total_epoch_tiles(buf);
}

DataFormat Router::get_buffer_data_format(unique_id_t id) const {
    const auto &buf = this->buffer_map.at(id);
    if (buf.is_queue()) {
        const auto &queue_info = this->get_queue(buf.queue_info().get_parent_queue_id());
        return queue_info.data_format;
    } else {
        return buf.info().data_format();
    }
}

int Router::get_scatter_gather_granularity(unique_id_t buf_id) const {
    const auto &buf = this->buffer_map.at(buf_id);
    if (buf.is_queue()) {
        const auto &queue_info = this->get_queue(buf.queue_info().get_parent_queue_id());
        return this->op_queue_output_buf_granularity.at(queue_info.name);
    } else {
        return buf.info().scatter_gather_num_tiles();
    }
}


int Router::get_op_kernel_input_tile_clear_granularity(const std::string& op_name, int operand_index) const {
  return this->op_input_kernel_tile_clear_granularity.at(op_name).at(operand_index);
}

  
std::unordered_map<unique_id_t, std::pair<tt_cxy_pair, int>> build_core_operand_map(const Router &router) {
    auto core_operand_map = std::unordered_map<unique_id_t, std::pair<tt_cxy_pair, int>>{};
    for (const auto &[op_name, grid_buffers] : router.get_op_input_buf_map()) {
        for (const auto &[row, row_buffers] : grid_buffers) {
            for (const auto &[col, operand_buffers] : row_buffers) {
                for (const auto &[operand_index, buffer_id] : operand_buffers) {
                    core_operand_map[buffer_id] = {router.get_buffer(buffer_id).core_location(), operand_index};
                }
            }
        }
    }

    return core_operand_map;
}

std::unordered_map<unique_id_t, std::pair<std::string, int>> build_buffer_consumer_operand_map(const Router &router) {
    auto result = std::unordered_map<unique_id_t, std::pair<std::string, int>>{};
    for (const auto &[op_name, grid_buffers] : router.get_op_input_buf_map()) {
        for (const auto &[row, row_buffers] : grid_buffers) {
            for (const auto &[col, operand_buffers] : row_buffers) {
                for (const auto &[operand_index, buffer_id] : operand_buffers) {
                    result[buffer_id] = {op_name, operand_index};
                }
            }
        }
    }

    return result;
}

void retag_all_dram_input_pipes_with_consumer_op_metadata(Router &router)
{
    auto buffer_id_to_consumer_operand_map = build_buffer_consumer_operand_map(router);
    // iterate through all pipes
    const auto& buffer_to_output_pipes_map = router.get_buffer_output_pipes();

    for (auto& [pipe_id, pipe] : router.get_pipes_mutable()) {
        // const auto &pipe_attrs = router.collect_pipe_attributes(pipe_id);
        const auto &pipe_attrs = router.collect_pipe_segment_attributes(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0});

        if(pipe_attrs.has_input_from_dram) {
            auto &dram_pipe = pipe;
            // follow pipe to core input buffer
            auto pipe_output_buf_id = dram_pipe.output_buffer_ids().at(0);
            while(buffer_to_output_pipes_map.find(pipe_output_buf_id) != buffer_to_output_pipes_map.end()) { // and buffer_to_output_pipes_map.at(pipe_output_buf_id).size() > 0) {
                auto output_pipes = buffer_to_output_pipes_map.at(pipe_output_buf_id);
                assert(output_pipes.size() == 1);
                auto output_pipe_id = (*output_pipes.begin());
                pipe_output_buf_id = router.get_pipe(output_pipe_id).output_buffer_ids().at(0);
            }
            //try {
            if(buffer_id_to_consumer_operand_map.find(pipe_output_buf_id) != buffer_id_to_consumer_operand_map.end()) {
                auto& [op_name, op_index] = buffer_id_to_consumer_operand_map.at(pipe_output_buf_id);
                dram_pipe.set_consumer_name(op_name);
                dram_pipe.set_consumer_input_index(op_index);
            }
            else{
                log_warning(tt::LogRouter, "DRAM Input pipes metadata: cant find buf id: {}", pipe_output_buf_id);
            }
        }
    }
    /* / for all ops
    for (auto &[op_name, op_info]: graph_info.op_map) {
        // process normal queues and pre-tm prolog queues that dont have forks
        for (auto& input_name : op_info.input_names) {
            if (name_is_queue(input_name)) {
                //assert(dram_group_mapping[input_name][op_info.name].size() == 0);
                dram_group_mapping[input_name][op_info.name] = {};    
            }
        }
    }
    */
}

static bool do_inverse_r_buf_placement(const tt_op_info &op_info, const std::string &queue_name) {
    bool inverse_r_buf_placement = false;
    if (n2p::get_op_class(op_info) == OpClass::MatMul || n2p::get_op_class(op_info) == OpClass::Depthwise) {
        int input_index = -1;
        for (int i = 0; i < (int)(op_info.input_names.size()); i++) {
            if (op_info.input_names[i] == queue_name) {
                input_index = i;
                break;
            }
        }
        TT_ASSERT(input_index != -1);
        inverse_r_buf_placement = op_info.grid_transpose ? (input_index == 0) : (input_index == 1);
    }
    return inverse_r_buf_placement;
};


tt_xy_pair map_prolog_queue_grid_entry_to_worker_coordinate(int queue_grid_r, int queue_grid_c, const std::string &queue_name, const tt_op_info &consumer_op) {

    int consumer_logical_core_r = queue_grid_r % consumer_op.grid_size_logical_r();
    int consumer_logical_core_c = queue_grid_c % consumer_op.grid_size_logical_c();
    if (do_inverse_r_buf_placement(consumer_op, queue_name)) {
        consumer_logical_core_r = consumer_op.grid_size_logical_r() - 1 - consumer_logical_core_r;
    }

    int consumer_worker_core_r, consumer_worker_core_c;
    consumer_op.get_core_yx_coord(
        consumer_logical_core_r,
        consumer_logical_core_c,
        consumer_worker_core_r,
        consumer_worker_core_c);

    return tt_xy_pair(consumer_worker_core_c, consumer_worker_core_r);
}

void Router::get_prolog_buffer_allocation_sizes(std::unordered_map<tt_cxy_pair, int> &l1_usage_per_core) {

    std::unordered_map<std::string, std::unordered_set<std::string>> queue_consumer_map = {};
    for (const auto &[op_name, operand_index_names] : op_input_name_map) {
        for (const auto &[operand_index, n] : operand_index_names) {
            bool operand_is_queue = this->queue_settings_map.find(n) != this->queue_settings_map.end();
            if (operand_is_queue) {
                queue_consumer_map[n].insert(op_name);
            }
        }
    }

    for (const auto &[queue_name, fork_buf_unique_id_map] : prolog_queue_name_fork_buf_unique_id_map) {
        for (const auto &[queue_grid_r, col_operand_index_buffer] : fork_buf_unique_id_map) {
            for (const auto &[queue_grid_c, operand_index_buffer] : col_operand_index_buffer) {
                for (const auto [fork_index, buffer_id] : operand_index_buffer) {
                    bool in_op_queue_output_map = this->op_queue_output_map.find(queue_name) != this->op_queue_output_map.end() && this->op_queue_output_map.at(queue_name).size() > fork_index;
                    if (queue_settings_map.find(queue_name) == queue_settings_map.end() || !queue_settings_map.at(queue_name).prolog) {
                        // entry for other epoch
                        continue;
                    }
                    const tt_queue_info &queue_info = get_queue(get_buffer(buffer_id).queue_info().get_parent_queue_id());
                    TT_ASSERT(queue_info.type == tt::IO_TYPE::RandomAccess || in_op_queue_output_map || queue_consumer_map.at(queue_name).size() == 1);
                    if (!(in_op_queue_output_map || (queue_consumer_map.find(queue_name) != queue_consumer_map.end() && queue_consumer_map.at(queue_name).size() == 1)) && queue_info.type == tt::IO_TYPE::RandomAccess) {
                        // Sometimes we have output gradient_acc queues that require prolog = true, although they aren't real inputs
                        continue;
                    }
                    const auto &op_name = in_op_queue_output_map ? 
                        this->op_queue_output_map.at(queue_name).at(fork_index) :
                        *(queue_consumer_map.at(queue_name).begin());

                    if (prolog_post_tm_operand.find(op_name) != prolog_post_tm_operand.end() && prolog_post_tm_operand.at(op_name).find(queue_name) != prolog_post_tm_operand.at(op_name).end() && prolog_post_tm_operand.at(op_name).at(queue_name)) {
                        log_trace(tt::LogRouter, "Prolog buffer for queue {} -> op {} skipped", queue_name, op_name);
                        continue;
                    }

                    if (is_buffer_scatter(buffer_id) && get_scatter_buffer_base_id(buffer_id) != buffer_id) {
                        continue;
                    }

                    const auto &consumer_op = op_info_map.at(op_name);
                    chip_id_t chip = get_buffer(op_output_buf_map.at(op_name).at(0).at(0)).chip_location();

                    const tt_xy_pair &worker_core =
                        map_prolog_queue_grid_entry_to_worker_coordinate(queue_grid_r, queue_grid_c, queue_name, consumer_op);

                    const auto &core_location = tt_cxy_pair(chip, this->get_soc_descriptor(chip).get_routing_core(worker_core));
                    int num_allocated_bytes = get_entry_size_in_bytes(queue_info, true);
                    log_trace(tt::LogRouter, "Prolog buffer {} - allocating {} B on core (c={},y={},x={})", buffer_id, num_allocated_bytes, core_location.chip, core_location.y, core_location.x);
                    l1_usage_per_core[core_location] += num_allocated_bytes;
                }
            }
        }
    }
        
}

// builds a map from buffer ID to bool where the bool value is true if the buffer is a forked buffer
// first collects the mapping from buffers to their scatter buffer children from Router::get_scatter_buffers_of_all_base_ids
// then iterates over the buffers in the router and checks if the buffer is a scatter buffer, if it is, it is a forked buffer if
// the size of the set of unique output pipes of the scatter buffer and all its children combined is greater than 1; each scatter buffer child is also forked
// otherwise if the buffer isn't a scatter buffer, it is a forked buffer if the size of the set of unique output pipes of the buffer is greater than 1
std::unordered_map<unique_id_t, bool> build_forked_buffer_map(Router &router) {
    auto forked_buffer_map = std::unordered_map<unique_id_t, bool>{};
    const auto &scatter_buffer_children = router.get_scatter_buffers_of_all_base_ids();
    for (const auto &[buffer_id, buffer] : router.get_buffer_map()) {
        if (router.is_buffer_scatter(buffer_id)) {
            if (router.get_scatter_buffer_base_id(buffer_id) != buffer_id) {
                // skip scatter buffer children
                continue;
            }

            auto scatter_buffer_children_set = std::unordered_set<unique_id_t>{};
            scatter_buffer_children_set.insert(buffer_id);
            scatter_buffer_children_set.insert(scatter_buffer_children.at(buffer_id).begin(), scatter_buffer_children.at(buffer_id).end());
            auto output_pipes = std::unordered_set<unique_id_t>{};
            for (const auto &scatter_buffer_child : scatter_buffer_children_set) {
                if (router.get_buffer_output_pipes().find(scatter_buffer_child) != router.get_buffer_output_pipes().end()) {
                    output_pipes.insert(router.get_buffer_output_pipes().at(scatter_buffer_child).begin(), router.get_buffer_output_pipes().at(scatter_buffer_child).end());
                }
            }
            bool is_forked = output_pipes.size() > 1;
            for (const auto &scatter_buffer_child : scatter_buffer_children_set) {
                forked_buffer_map[scatter_buffer_child] = is_forked;
            }
        } else {
            forked_buffer_map[buffer_id] = router.get_buffer_output_pipes().find(buffer_id) != router.get_buffer_output_pipes().end() && router.get_buffer_output_pipes().at(buffer_id).size() > 1;
        }
    }

    return forked_buffer_map;
}

std::unordered_map<unique_id_t, std::vector<unique_id_t>> Router::get_scatter_buffers_of_all_base_ids() const {
    std::unordered_map<unique_id_t, std::vector<unique_id_t>> scatter_buffers_of_base_ids = {};
    for (const auto &[id, buffer] : buffer_map) {
        if (is_buffer_scatter(id)) {
            unique_id_t base_id = get_scatter_buffer_base_id(id);
            scatter_buffers_of_base_ids[base_id].push_back(id);
        }
    }

    return scatter_buffers_of_base_ids;
}

void Router::constructor_initialize_prolog_extra_stream_counts(/*const std::unordered_map<unique_id_t, std::vector<unique_id_t>> &scatter_buffers_of_base_ids*/) {

    // The queue_consumer_map is specifically to handle the special case where we may have a scenario where
    // we have for example gradient accumulation buffers that are only written to but never read from in the netlist.
    // In practice, this typically happens with training workloads that have the optimizer running on CPU 
    std::unordered_map<std::string, std::unordered_set<std::string>> queue_consumer_map = {};
    for (const auto &[op_name, operand_index_names] : op_input_name_map) {
        for (const auto &[operand_index, n] : operand_index_names) {
            bool operand_is_queue = this->queue_settings_map.find(n) != this->queue_settings_map.end();
            if (operand_is_queue) {
                queue_consumer_map[n].insert(op_name);
            }
        }
    }

    const auto &scatter_buffers_of_base_ids = get_scatter_buffers_of_all_base_ids();

    for (const auto &[queue_name, fork_buf_unique_id_map] : prolog_queue_name_fork_buf_unique_id_map) {
        for (const auto &[queue_grid_r, col_operand_index_buffer] : fork_buf_unique_id_map) {
            for (const auto &[queue_grid_c, operand_index_buffer] : col_operand_index_buffer) {
                for (const auto [fork_index, buffer_id] : operand_index_buffer) {
                    bool in_op_queue_output_map = this->op_queue_output_map.find(queue_name) != this->op_queue_output_map.end() && this->op_queue_output_map.at(queue_name).size() > fork_index;
                    if (queue_settings_map.find(queue_name) == queue_settings_map.end() || !queue_settings_map.at(queue_name).prolog) {
                        // entry for other epoch
                        continue;
                    }
                    const tt_queue_info &queue_info = get_queue(get_buffer(buffer_id).queue_info().get_parent_queue_id());
                    TT_ASSERT(queue_info.type == tt::IO_TYPE::RandomAccess || in_op_queue_output_map || queue_consumer_map.at(queue_name).size() == 1);
                    if (!(in_op_queue_output_map || (queue_consumer_map.find(queue_name) != queue_consumer_map.end() && queue_consumer_map.at(queue_name).size() == 1)) && queue_info.type == tt::IO_TYPE::RandomAccess) {
                        // Sometimes we have output gradient_acc queues that require prolog = true, although they aren't real inputs
                        continue;
                    }
                    const auto &op_name = in_op_queue_output_map ? 
                        this->op_queue_output_map.at(queue_name).at(fork_index) :
                        *(queue_consumer_map.at(queue_name).begin());
                    if (prolog_post_tm_operand.find(op_name) != prolog_post_tm_operand.end() && prolog_post_tm_operand.at(op_name).find(queue_name) != prolog_post_tm_operand.at(op_name).end() && prolog_post_tm_operand.at(op_name).at(queue_name)) {
                        log_trace(tt::LogRouter, "Prolog buffer for queue {} -> op {} skipped", queue_name, op_name);
                        continue;
                    }
                    const auto &consumer_op = op_info_map.at(op_name);
                    chip_id_t chip = get_buffer(op_output_buf_map.at(op_name).at(0).at(0)).chip_location();

                    // We want to wrap around the untransposed consumer op grid and then after that we apply the
                    // transpose implicitly via `get_core_yx_coord` so that we can also effectively transpose the
                    // locations of the queue entries.
                    const tt_xy_pair &worker_core =
                        map_prolog_queue_grid_entry_to_worker_coordinate(queue_grid_r, queue_grid_c, queue_name, consumer_op);
                    int consumer_routing_core_r = this->get_soc_descriptor(chip).worker_log_to_routing_y.at(worker_core.y);
                    int consumer_routing_core_c = this->get_soc_descriptor(chip).worker_log_to_routing_x.at(worker_core.x);

                    const auto &core_location = tt_cxy_pair(chip, consumer_routing_core_c, consumer_routing_core_r);
                    auto &core_attrs = this->cluster_resource_model.get_core_attributes(core_location);
                    log_trace(
                        tt::LogRouter,
                        "Add 1 extra stream for prolog buffer {} for queue {} for the buffer itself, on core "
                        "(c={},y={},x={})",
                        buffer_id,
                        queue_name,
                        core_location.chip,
                        core_location.y,
                        core_location.x);
                    core_attrs.adjust_extra_streams(1);

                    auto output_pipes = std::unordered_set<unique_id_t>{};
                    if (is_buffer_scatter(buffer_id)) {
                        for (unique_id_t id : scatter_buffers_of_base_ids.at(buffer_id)) {
                            bool has_output_pipes = buffer_output_pipes.find(id) != buffer_output_pipes.end();
                            if (has_output_pipes) {
                                for (unique_id_t output_pipe : buffer_output_pipes.at(id)) {
                                    output_pipes.insert(output_pipe);
                                }
                            }
                        }
                    } else {
                        bool has_output_pipes = buffer_output_pipes.find(buffer_id) != buffer_output_pipes.end();
                        if (has_output_pipes) {
                            output_pipes = buffer_output_pipes.at(buffer_id);
                        }
                    }

                    if (output_pipes.size() > 1) {
                        // +1 extra stream per output from the prolog buffer that isn't already subsummed by streams used for output gather,
                        // multicast, or gather multicast pipes
                        auto pipe_not_gather_or_mcast_pipe = [this](unique_id_t pipe_id) {
                            // const auto &pipe_attrs = this->collect_pipe_attributes(pipe_id);
                            const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0});
                            return !(pipe_attrs.has_gather || pipe_attrs.has_multicast);
                        };
                        int num_extra_streams = std::count_if(output_pipes.begin(), output_pipes.end(), pipe_not_gather_or_mcast_pipe);

                        TT_ASSERT(num_extra_streams >= 0);
                        log_trace(tt::LogRouter, "Prolog buffer (base id __1) {} uses {} extra (forking) streams on core (c={},y={},x={})", buffer_id, num_extra_streams, core_location.chip,core_location.y,core_location.x);
                        core_attrs.adjust_extra_streams(num_extra_streams);
                    }
                }
            }
        }
    }
}

void Router::constructor_initialize_forked_kernel_output_buffer_extra_streams(const std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &buffer_scatter_children) {
    auto add_to_output_pipes = [this] (unique_id_t buffer_id, std::unordered_set<unique_id_t> &output_pipes) {
        bool buffer_has_output_pipes = this->buffer_output_pipes.find(buffer_id) != this->buffer_output_pipes.end();
        if (buffer_has_output_pipes) {
            for (auto pipe_id : this->buffer_output_pipes.at(buffer_id)) {
                output_pipes.insert(pipe_id);
            }
        }
    };


    for (const auto &[id, buffer] : buffer_map) {
        std::unordered_set<unique_id_t> output_pipes = {};
        
        if (!buffer.is_queue() && buffer.info().type() == RouterBufferType::PrologInter) {
            log_debug(tt::LogRouter, "Add 1 extra stream at (c={},y={},x={}) for prolog intermediate buffer {}", buffer.core_location().chip, buffer.core_location().y, buffer.core_location().x, id);
            this->cluster_resource_model.get_core_attributes(buffer.core_location()).adjust_extra_streams(1);
        }

        bool saw_self = false;

        if (buffer.is_queue()) {
            add_to_output_pipes(id, output_pipes);
        } else {
            if (is_buffer_scatter(id) && get_scatter_buffer_base_id(id) != id) {
                continue;
            }
            for (auto sub_id : buffer_scatter_children.at(id)) {
                add_to_output_pipes(sub_id, output_pipes);
                saw_self = saw_self || (id == sub_id);
            }
            TT_ASSERT(saw_self);
        }
        
        auto fork_factor = output_pipes.size();
        if(fork_factor > 1 || (!buffer.is_queue() && buffer.info().type() == RouterBufferType::Relay)) {
            // Count fork streams for (output) L1 buffers
            if (!buffer.is_queue()) {
                // for queues, one of three scenarios takes place:
                //   - if the buffer is scatter, then each consumer will read it individually (no actual fork)
                //   - otherwise, the pipe core will fork - the fork is implemented at the pipe location core
                //   - the queue is prologued - forked prolog buffers are handled later
                bool is_scatter_base_or_non_scatter_buffer = !is_buffer_scatter(id) || get_scatter_buffer_base_id(id) == id;
                if (is_scatter_base_or_non_scatter_buffer) {
                    auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(buffer.core_location());
                    log_debug(tt::LogRouter, "Buffer {} has fork factor {}", id, fork_factor);
                    int fork_factor_extra_streams = fork_factor - (buffer.info().type() == RouterBufferType::Relay ? 0 : 1);
                    log_debug(tt::LogRouter, "Add {} extra stream at (c={},y={},x={}) for forked buffer {}", fork_factor_extra_streams, buffer.core_location().chip, buffer.core_location().y, buffer.core_location().x, id);
                    core_resource_tracker.adjust_extra_streams(fork_factor_extra_streams);
                }
            }
        }
    }
}

/* 
 * Router tracks extra tile header buffers required on a per core basis. However, PipeGen, non-idealistically
 * tracks extra tile header buffers required globally across all tensix cores in the tempora epoch. This function
 * updates Router to mirror that functionality, without affecting the core logic that count's extra tile header
 * buffers. This way, when the extra tile header buffer counting in PipeGen (or v2) is cleaned up, we can simply
 * disable the call to this function.
 */
void Router::constructor_allocate_extra_tile_headers_for_tensix_cores() {
    std::set<int> tile_sizes;

    for (const auto &[name, op] : op_info_map) {
        for (DataFormat df : op.input_data_formats) {
            tile_sizes.insert(tt::size::get_tile_size_in_bytes(df, true));
        }

        tile_sizes.insert(tt::size::get_tile_size_in_bytes(op.intermed_data_format, true));
        tile_sizes.insert(tt::size::get_tile_size_in_bytes(op.output_data_format, !op.untilize_output));
    }

    int num_extra_tile_headers = tile_sizes.size() - 1;
    if (num_extra_tile_headers > 0) {
        for (const chip_id_t &device_id : chip_ids) {
            for (const auto &[core_routing_coords, core_descriptor] : this->get_soc_descriptor(device_id).cores) {
                TT_ASSERT(core_routing_coords == core_descriptor.coord);
                bool is_worker_core = (core_descriptor.type == CoreType::WORKER);
                if (!is_worker_core) {
                    continue;
                }

                HwCoreAttributes &core = cluster_resource_model.get_core_attributes(tt_cxy_pair(device_id, core_routing_coords.x, core_routing_coords.y));
                int old_extra_tile_header_count = core.used_tile_sizes.size() - 1;
                int new_extra_tile_header_count = num_extra_tile_headers - old_extra_tile_header_count;
                if (new_extra_tile_header_count > 0) {
                    core.used_tile_sizes.insert(tile_sizes.begin(), tile_sizes.end());
                    core.allocate_bytes(new_extra_tile_header_count * address_map.l1.TILE_HEADER_BUF_SIZE);
                }
            }
        }
    }
}

std::tuple<
    std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>,
    std::unordered_map<tt_cxy_pair, std::unordered_set<int>>>
Router::constructor_initialize_pipe_resource_usage() {
    auto log_mismatch = [this](unique_id_t pipe_id, int delta1, int delta2) {
        pipe_t const& pipe = this->get_pipe(pipe_id);
        std::stringstream inputs_ss;
        inputs_ss << "{";
        for (unique_id_t buffer_id : pipe.input_buffer_ids) {
            inputs_ss << "{" << buffer_id << ", is_prolog: " << this->get_buffer(buffer_id).is_prolog() << "}";
        }
        inputs_ss << "}";
        if(pipe.has_multiple_timesteps()) {
            log_debug(
                tt::LogRouter,
                "\tPipe {} (Multi-location) has mismatched extra stream counts. Expected {} but got {}. Pipe inputs: {}",
                pipe_id,
                delta1,
                delta2,
                inputs_ss.str());
        }
        
        else {
            log_debug(
                tt::LogRouter,
                "\tPipe {} (c={},y={},x={}) has mismatched extra stream counts. Expected {} but got {}. Pipe inputs: {}",
                pipe_id,
                pipe.core_location().chip, pipe.core_location().y, pipe.core_location().x,
                delta1,
                delta2,
                inputs_ss.str());
        }
    };

    auto core_dram_read_sources = std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>>{}; // only for logging/debug purposes
    auto dram_reader_core_operands = std::unordered_map<tt_cxy_pair, std::unordered_set<int>>{};

    for (const auto &[pipe_id, pipe]: this->get_pipes()) {
        log_debug(tt::LogRouter, "Pipe: {}", pipe_id);
        const auto &output_buffers = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids() : 
            time_multiplexed_outputs_t{pipe.output_buffer_ids()};

        int num_segments = output_buffers.size();
        for (int s = 0; s < num_segments; s++) {
            pipe_segment_id_t const& pipe_segment_id{.pipe_id=pipe_id, .segment=s};
            // const auto &pipe_attrs = this->collect_pipe_attributes(pipe_segment_id.pipe_id);
            const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id);
            tt_cxy_pair const& pipe_location = pipe.scatter_segment_core_location(s);
            log_debug(tt::LogRouter, "Adding pipe {}", pipe_id);

            auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(pipe_location);
            auto const& snapshot_before = HwCoreResourceUsageSnapshot::create(core_resource_tracker, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS, ResourceUsageType::INPUT_FROM_DRAM, ResourceUsageType::OUTPUT_TO_DRAM});

            int num_extra_streams = compute_pipe_segment_extra_stream_use(*this, pipe_segment_id, pipe_location);
            { // Logging
                log_debug(tt::LogRouter,
                    "\tAdd {} extra stream for segment {} at (c={},y={},x={}) for gather. Total={}+{}",
                    num_extra_streams,
                    s,
                    pipe_location.chip,
                    pipe_location.y,
                    pipe_location.x,
                    core_resource_tracker.get_used_extra_streams(),
                    num_extra_streams);
                if (num_extra_streams > 1) {
                    TT_ASSERT(
                        core_resource_tracker.get_number_of_resident_gather_multicast_pipes() <= this->get_max_gather_pipes_before_phase_based_gather(),
                        "Gather pipe counting on core incorrect");
                }
            }

            { // Extra gather bytes
                int gather_input_buffering_extra_bytes = compute_pipe_segment_gather_extra_input_buffering_size(*this, pipe_segment_id);
                log_debug(tt::LogRouter, "\tAllocating {} extra input buffer bytes for gather pipe {} inputs at core (c={},y={},x={}). {} B allocated on core", 
                    gather_input_buffering_extra_bytes, pipe_id, pipe_location.chip, pipe_location.y, pipe_location.x, core_resource_tracker.get_num_allocated_bytes());
                core_resource_tracker.allocate_bytes(gather_input_buffering_extra_bytes);
            }

            bool extra_gather_streams_added = false;
            { // Extra streams counted
                
                int old_extra_streams = core_resource_tracker.get_used_extra_streams();
                if (pipe_attrs.has_gather && pipe_attrs.has_multicast) {
                    log_trace(tt::LogRouter, "\tgather-multicast pipe");
                    core_resource_tracker.register_resident_gather_multicast_pipe(
                        create_pipe_segment_hash(*this, pipe_segment_id), pipe.input_buffer_ids.size());
                } else if (pipe_attrs.has_gather) {
                    log_trace(tt::LogRouter, "\tgather pipe");
                    core_resource_tracker.register_resident_gather_pipe(
                        create_pipe_segment_hash(*this, pipe_segment_id), pipe.input_buffer_ids.size());
                } else if (pipe_attrs.has_multicast) {
                    log_trace(tt::LogRouter, "\tmulticast pipe");
                    core_resource_tracker.register_resident_multicast_pipe(create_pipe_segment_hash(*this, pipe_segment_id));
                }
                int new_extra_streams = core_resource_tracker.get_used_extra_streams();
                log_trace(tt::LogRouter, "\t\t{} extra streams consumed. Total={}", new_extra_streams - old_extra_streams, new_extra_streams);
                // The compute extra gather streams will omit the base extra DRAM read stream used for gather from DRAM
                // -> if 3 input gather from DRAM, it will return 2 because it assumes the the gather will only add up to 2 extra
                //    streams over all since 1 extra stream is already used for the DRAM read... this behaviour should be cleaned
                //    up and we should only add the extra DRAM read stream when we don't do the gather strwean optimization.

                extra_gather_streams_added = (new_extra_streams - old_extra_streams) > 0;
                if (num_extra_streams != (new_extra_streams - old_extra_streams)) {
                    log_mismatch(pipe_id, num_extra_streams, new_extra_streams - old_extra_streams);
                }
            }

            { // input from dram stream counting
                if (pipe_attrs.has_input_from_dram) {
                    unique_id_t first_consumer_buf_id = output_buffers.at(s).at(0);
                    const auto &first_consumer_buf = buffer_map.at(first_consumer_buf_id);

                    bool input_from_dram = pipe_attrs.has_input_from_dram;
                    if (first_consumer_buf.info().type() == RouterBufferType::PrologInter) {
                        const auto &[core, operand_index] = this->buffer_core_operand_map.at(first_consumer_buf_id);
                        // For fused ops, we only want to count this attribute once
                        
                        // prolog intermediate buffers actually loaded into L1 during the prolog phase (which happens before the main epoch execution)
                        input_from_dram = input_from_dram &&
                            dram_reader_core_operands[pipe_location].find(operand_index) == dram_reader_core_operands[pipe_location].end();
                        if (input_from_dram) {
                            log_trace(tt::LogRouter, "\tAdd 1 extra stream at (c={},y={},x={}) for prolog intermediate buffer {}", pipe_location.chip,pipe_location.y,pipe_location.x, first_consumer_buf_id);
                            core_dram_read_sources[pipe_location].push_back(pipe_id);
                            dram_reader_core_operands[pipe_location].insert(operand_index);
                        }


                    } else if (first_consumer_buf.info().type() == RouterBufferType::Relay) {
                        // really just for unit (g)tests
                        if (input_from_dram) {
                            log_trace(tt::LogRouter, "\tAdd 1 extra stream at (c={},y={},x={}) for DRAM reading relay buffer {}", pipe_location.chip,pipe_location.y,pipe_location.x, first_consumer_buf_id);
                            core_resource_tracker.adjust_extra_streams(1);
                        }
                    }
                    if (input_from_dram) {
                        // If input size > 1, the extra dram read stream is incorporated into the gather stream
                        if (!extra_gather_streams_added) {
                            core_resource_tracker.adjust_extra_streams(1);
                        }
                        core_resource_tracker.adjust_input_from_dram(1);
                    }
                }
            }

            { // output to dram stream counting
                bool any_timestep_outputs_to_dram =
                    std::find(pipe_attrs.has_output_to_dram.begin(), pipe_attrs.has_output_to_dram.end(), true) !=
                    pipe_attrs.has_output_to_dram.end();
                if (any_timestep_outputs_to_dram) {
                    core_resource_tracker.adjust_outputs_to_dram(1);

                    const auto &buf_location = get_buffer_location(pipe.input_buffer_ids.at(0));
                    log_debug(tt::LogRouter,
                        "\tAdd 1 extra stream at (c={},y={},x={}) for output to dram",
                        buf_location.chip,
                        buf_location.y,
                        buf_location.x);
                }
            }

            auto const &snapshot_after = HwCoreResourceUsageSnapshot::create(
                core_resource_tracker,
                {ResourceUsageType::L1_MEMORY,
                 ResourceUsageType::EXTRA_STREAMS,
                 ResourceUsageType::INPUT_FROM_DRAM,
                 ResourceUsageType::OUTPUT_TO_DRAM});
            log_debug(tt::LogRouter,
                "Added pipe {} segment {} to core (c={},y={},x={}). Resources used by pipe segment: {}. Total "
                "resources used on core: {}",
                pipe_id,
                s,
                pipe_location.chip,
                pipe_location.y,
                pipe_location.x,
                snapshot_after - snapshot_before,
                snapshot_after);
            auto pipe_locations_set = std::set<tt_cxy_pair>(pipe.locations.begin(), pipe.locations.end());
            if (pipe_attrs.active_dram_queues > 0 && pipe_locations_set.size() > 0) {
                TT_ASSERT(pipe_locations_set.size() == 1);
                const auto &loc = *(pipe_locations_set.begin());
                this->cluster_resource_model.get_core_attributes(loc).adjust_active_dram_queues(pipe_attrs.active_dram_queues);
            }
        }

        if (this->get_soc_descriptor(this->chip_ids.at(0)).ethernet_cores.size() > 0) {
            if (is_chip_to_chip_pipe(get_pipe(pipe_id), *this)) {
                auto endpoint_cores = collect_pipe_endpoint_cores(*this, pipe_id);

                for (const auto &loc : endpoint_cores) {
                    this->cluster_resource_model.get_core_attributes(loc).adjust_ethernet_streams(1);
                }
            }
        }
    }

    return {core_dram_read_sources, dram_reader_core_operands};
}

Router::Router(
    std::unordered_map<chip_id_t, buda_SocDescriptor> const& _soc_descriptors,
    const std::vector<chip_id_t> &_chip_ids,
    const ClusterGraph &_cluster_graph,

    int unique_id_align,
    int unique_id_block,
    n2p::UniqueIdGenerator &_unique_id_generator,

    const std::unordered_map<unique_id_t, tt_queue_info> &_queue_map,
    std::map<unique_id_t, router_buffer_info_t> &_buffer_map,
    std::map<unique_id_t, pipe_t> &_pipes,
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &_buffer_output_pipes,
    std::unordered_map<unique_id_t, unique_id_t> &_buffer_input_pipes,
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &_op_input_buf_map,
    std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>> &_op_intermediate_buf_map,
    std::map<std::string, std::map<int, std::map<int, unique_id_t>>> &_op_output_buf_map,
    const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> &_fused_op_schedule_map,

    const std::map<std::string, bool> &_op_queue_output_scatter,
    const std::map<std::string, int> &_op_queue_output_buf_granularity,
    const std::map<std::string, std::map<int, int>> &_op_input_kernel_tile_clear_granularity,

    std::map<std::string, tt_op_info> &_op_info_map,


    const std::map<std::string, QueueSettings> &_queue_settings_map,
    const std::map<std::string, std::map<int, std::string>> &_op_input_name_map,
    const std::unordered_map<std::string, std::vector<n2p::prolog_buffer>> &_prolog_buffers_per_op,
    const std::map<std::string, std::map<std::string, bool>> &_prolog_post_tm_operand,

    const std::map<std::string, std::map<int, std::map<int, std::map<int, unique_id_t>>>>
        &_prolog_queue_name_fork_buf_unique_id_map,
    const std::map<std::string, std::vector<std::string>> &_op_queue_output_map,
    std::vector<std::uint64_t>& _hronological_list_ids) :
    soc_descriptors(_soc_descriptors),
    chip_ids(_chip_ids),
    cluster_graph(_cluster_graph),

    dram_channels(Router::initialize_dram_core_map(_soc_descriptors)),
    address_map(ArchAddressMap::of(_soc_descriptors.begin()->second.arch)),
    cluster_resource_model(Router::initialize_hw_cores(_soc_descriptors, address_map, _chip_ids)),
    UNIQUE_ID_ALIGN(unique_id_align),
    ID_BLOCK(unique_id_block),
    unique_id_generator(_unique_id_generator),

    queue_map(_queue_map),
    buffer_map(_buffer_map),
    pipes(_pipes),
    buffer_output_pipes(_buffer_output_pipes),
    buffer_input_pipes(_buffer_input_pipes),
    op_input_buf_map(_op_input_buf_map),
    op_intermediate_buf_map(_op_intermediate_buf_map),
    op_output_buf_map(_op_output_buf_map),
    fused_op_schedule_map(_fused_op_schedule_map),

    op_queue_output_scatter(_op_queue_output_scatter),
    op_queue_output_buf_granularity(_op_queue_output_buf_granularity),
    op_input_kernel_tile_clear_granularity(_op_input_kernel_tile_clear_granularity),
    op_info_map(_op_info_map),

    queue_settings_map(_queue_settings_map),
    op_input_name_map(_op_input_name_map),
    prolog_buffers_per_op(_prolog_buffers_per_op),
    prolog_post_tm_operand(_prolog_post_tm_operand),

    prolog_queue_name_fork_buf_unique_id_map(_prolog_queue_name_fork_buf_unique_id_map),
    op_queue_output_map(_op_queue_output_map),
    forked_buffer_lookup(build_forked_buffer_map(*this)),
    hronological_unique_id_list(_hronological_list_ids)
{
    this->constructor_disable_core_resource_tracking_asserts();
    log_debug(tt::LogRouter, "Router Constructor");
    this->buffer_core_operand_map = build_core_operand_map(*this);

    auto per_core_l1_buffer_use = compute_l1_use_from_buffers(*this);
    get_prolog_buffer_allocation_sizes(per_core_l1_buffer_use);

    for (const auto &[core, used_bytes] : per_core_l1_buffer_use) {
        auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(core);
        core_resource_tracker.allocate_bytes(used_bytes);
    }

    std::unordered_set<unique_id_t> scatter_buf_owning_bufs_counted = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_scatter_children = {};
    for (const auto &[id, buf]: this->buffer_map) {
        if (!buf.is_queue()) {

            // Dram buffer core attributes are updated in assign_dram_subchan pass, when the core is selected
            unique_id_t buffer_id = buf.info().is_scatter() ? this->get_scatter_buffer_base_id(id) : id;
            buffer_scatter_children[buffer_id].insert(id);
            if (scatter_buf_owning_bufs_counted.find(buffer_id) != scatter_buf_owning_bufs_counted.end()) {
                continue;
            }

            if (buf.info().is_scatter()) {
                scatter_buf_owning_bufs_counted.insert(buffer_id);
            }

            auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(buf.core_location());
            core_resource_tracker.add_to_buffer_count();

            switch (buf.info().type()) {
                case RouterBufferType::Input:
                    this->cluster_resource_model.get_core_attributes(buf.core_location()).add_input_buffer(buffer_id);
                    break;
                case RouterBufferType::Intermediate:
                    this->cluster_resource_model.get_core_attributes(buf.core_location())
                        .add_intermediate_buffer(buffer_id);
                    break;
                case RouterBufferType::Output:
                    this->cluster_resource_model.get_core_attributes(buf.core_location()).add_output_buffer(buffer_id);
                    break;
                case RouterBufferType::Relay:
                    this->cluster_resource_model.get_core_attributes(buf.core_location()).add_relay_buffer(buffer_id);
                    break;
                default: break;
            };
        }
    }


    auto [core_dram_read_sources, dram_reader_core_operands] = constructor_initialize_pipe_resource_usage();

    constructor_initialize_forked_kernel_output_buffer_extra_streams(buffer_scatter_children);

    constructor_initialize_prolog_extra_stream_counts();

    constructor_allocate_extra_tile_headers_for_tensix_cores();

    // ----------------------------------- LOGGING ----------------------------------- //

    log_debug(tt::LogRouter, "======================================================================");
    log_debug(tt::LogRouter, "EXTRA STREAMS USED");
    for (auto chip : chip_ids) {
        log_debug(tt::LogRouter, "CHIP {}", chip);
        auto ss = std::stringstream();
        ss << "COLUMNS:";
        for (int c = 0; c < this->get_soc_descriptor(chip).grid_size.x; c++) {
           ss << "   " << c << "  |";
        }
        log_debug(tt::LogRouter, "{}", ss.str());
        
        for (int r = 0; r < this->get_soc_descriptor(chip).grid_size.y; r++) {
            auto ss2 = std::stringstream();
            ss2 << "ROW (" << r << "): ";
            for (int c = 0; c < this->get_soc_descriptor(chip).grid_size.x; c++) {
                bool core_defined = this->get_soc_descriptor(chip).cores.find(tt_xy_pair(c,r)) != this->get_soc_descriptor(chip).cores.end();
                int used_extra_streams = core_defined ? cluster_resource_model.get_core_attributes(tt_cxy_pair(chip,c,r)).get_used_extra_streams() : 0;
                ss2 << "   " << used_extra_streams << "  |";
            }
            log_debug(tt::LogRouter, "{}", ss2.str());
        }
    }

    //     if (is_chip_to_chip) {
    //         for (const auto &loc : endpoint_cores) {
    //             this->cluster_resource_model.get_core_attributes(loc).apply_buffer_attrs(BufferAttributes::requires_ethernet_io());
    //         }
    //     }
    // }

    // initialize reverse lookup structures 
    for (const auto &[op_name, grid_buffers] : this->op_input_buf_map) {
      for (const auto &[row, row_buffers] : grid_buffers) {
        for (const auto &[col, operand_buffers] : row_buffers) {
          for (const auto &[operand_index, buffer_id] : operand_buffers) {
            op_input_buffer_index_map[buffer_id] = operand_index;
            op_input_buffer_op_name_map[buffer_id] = op_name;
          }
          unique_id_t output_buffer_id =  op_output_buf_map.at(op_name).at(row).at(col);
          op_output_buffer_op_name_map[output_buffer_id] = op_name;
        }
      }
    }
    log_debug(tt::LogRouter, "======================================================================");
    log_debug(tt::LogRouter, "core_dram_read_sources");
    for (auto chip : chip_ids) {
        log_debug(tt::LogRouter, "CHIP {}", chip);
        for (int r = 0; r < this->get_soc_descriptor(chip).grid_size.y; r++) {
            auto ss = std::stringstream();
            ss << "ROW (" << r << "): ";
            for (int c = 0; c < this->get_soc_descriptor(chip).grid_size.x; c++) {
                ss << "COL=" << c << "{";
                for (auto id : core_dram_read_sources[tt_cxy_pair(chip,c,r)]) {
                    ss << id << ",";
                }
                ss << "}, ";
            }
            log_debug(tt::LogRouter, "{}", ss.str());
        }
    }

    if (env_var("ROUTER_SKIP_RESOURCE_VALIDATION_CHECK", 0) == 0) {
        this->constructor_enable_core_resource_tracking_asserts();
        this->constructor_report_exceeded_core_resources();
    }
}

void Router::constructor_disable_core_resource_tracking_asserts() {
    for (auto chip_id : chip_ids) {
        for (const auto &[core_routing_coords, core_descriptor] : this->get_soc_descriptor(chip_id).cores) {
            const tt_cxy_pair core_location(chip_id, core_routing_coords.x, core_routing_coords.y);
            this->cluster_resource_model.get_core_attributes(core_location).disable_resource_constraint_checking();
        }
    }
}

void Router::constructor_enable_core_resource_tracking_asserts() {
    for (auto chip_id : chip_ids) {
        for (const auto &[core_routing_coords, core_descriptor] : this->get_soc_descriptor(chip_id).cores) {
            const tt_cxy_pair core_location(chip_id, core_routing_coords.x, core_routing_coords.y);
            this->cluster_resource_model.get_core_attributes(core_location).enable_resource_constraint_checking();
        }
    }
}

void Router::constructor_report_exceeded_core_resources() const {
    std::unordered_map<tt_cxy_pair, std::vector<resource_usage_entry_t>> exceeded_resource_messages = {};
    std::unordered_set<ResourceUsageType> ignore_list {
        ResourceUsageType::ETHERNET_STREAMS,
        ResourceUsageType::L1_MEMORY,
        ResourceUsageType::ACTIVE_DRAM_QUEUES};

    for (auto chip_id : chip_ids) {
        for (const auto &[core_routing_coords, core_descriptor] : this->get_soc_descriptor(chip_id).cores) {
            const tt_cxy_pair core_location(chip_id, core_routing_coords.x, core_routing_coords.y);
            const auto &core_resource_tracker = this->cluster_resource_model.get_core_attributes(core_location);
            if (core_resource_tracker.any_resource_limits_exceeded_with_ignore_list(ignore_list)) {
                exceeded_resource_messages[core_location] = core_resource_tracker.get_exceeded_resources_with_ignore_list(ignore_list);
            }
        }
    }

    std::stringstream error_report;
    if (exceeded_resource_messages.size() > 0) {
        std::unordered_map<tt_cxy_pair, std::string> core_op_names = {};
        for (const auto &[buf_id, op_name] : this->op_output_buffer_op_name_map) {
            const tt_cxy_pair &core_location = this->get_buffer_location_tensix_grid(buf_id);
            core_op_names[core_location] = op_name;
        }

        for (const auto &[core_location, messages] : exceeded_resource_messages) {
            error_report << "======================================================================" << std::endl;
            error_report << "Resource limits exceeded for the following cores" << std::endl;
            buda_SocDescriptor const& soc_descriptor = this->get_soc_descriptor(core_location.chip);
            error_report << "Core (c=" << core_location.chip << ",y=" << core_location.y << ",x=" << core_location.x << ") [routing] ";
            if (soc_descriptor.is_worker_core(tt_xy_pair(core_location.x, core_location.y))) {
                const auto &worker_location = tt_cxy_pair(core_location.chip, soc_descriptor.get_worker_core(tt_xy_pair(core_location.x, core_location.y)));
                error_report << " (c=" << worker_location.chip << ",y=" << worker_location.y << ",x=" << worker_location.x << ") [worker]";
                if (core_op_names.find(worker_location) != core_op_names.end()) {
                    error_report << " [op_name=" << core_op_names.at(worker_location) << "]";
                }
            }
            error_report << " exceeded resource constraints: " << std::endl;
            for (const auto &message : messages) {
                error_report << "  " << message.resource_name << " used: " << message.used << " limit: " << message.limit << std::endl;
            }
        }
    }

    TT_ASSERT(exceeded_resource_messages.size() == 0, error_report.str());
}


///////////////////////////////////////////////////////////////////////////////
// ROUTER DATAFLOW MANIPULATION FUNCTIONS                                    //
///////////////////////////////////////////////////////////////////////////////
tt_cxy_pair Router::get_buffer_location(unique_id_t buffer_id) const {
    return this->buffer_map.at(buffer_id).core_location();
}

  
tt_cxy_pair Router::get_buffer_location_tensix_grid(unique_id_t buffer_id) const {
  const router_buffer_info_t &buffer_info = this->get_buffer(buffer_id);
  tt_cxy_pair buffer_loc_routing = buffer_info.core_location();
  tt_cxy_pair result;
  if(buffer_info.is_prolog()) {
    result.chip = buffer_loc_routing.chip;
    result.x = buffer_info.queue_info().get_prolog_core_location().x;
    result.y = buffer_info.queue_info().get_prolog_core_location().y;
    return result;
  }
  result.chip = buffer_loc_routing.chip;
  result.x = this->get_soc_descriptor(result.chip).routing_x_to_worker_x.at(buffer_loc_routing.x);
  result.y = this->get_soc_descriptor(result.chip).routing_y_to_worker_y.at(buffer_loc_routing.y);
  return result;
} 

tt_cxy_pair Router::get_pipe_location_tensix_grid(unique_id_t pipe_id, int output_index = 0) const {
  const pipe_t &pipe = this->get_pipes().at(pipe_id);
  tt_cxy_pair result;
  tt_cxy_pair pipe_loc_routing = pipe.has_multiple_timesteps() ? pipe.core_locations().at(output_index) : pipe.core_location();
  result.chip = pipe_loc_routing.chip;
  result.x = this->get_soc_descriptor(result.chip).routing_x_to_worker_x.at(pipe_loc_routing.x);
  result.y = this->get_soc_descriptor(result.chip).routing_y_to_worker_y.at(pipe_loc_routing.y);
  return result;
}

bool Router::buffer_is_on_ethernet_core(unique_id_t buf_id, chip_id_t target_chip, ethernet_channel_t target_channel) const {
    const auto &buffer_location = this->get_buffer_location(buf_id);
    const auto &soc_descriptor = this->get_soc_descriptor(buffer_location.chip);
    return soc_descriptor.is_ethernet_core(buffer_location) &&
           tt_cxy_pair(target_chip, soc_descriptor.ethernet_cores.at(target_channel)) == buffer_location;
}

bool Router::is_buffer_scatter(unique_id_t buffer_id) const {
    const auto &b = this->buffer_map.at(buffer_id);
    if (b.is_queue()) {
    unique_id_t parent_id = b.queue_info().get_parent_queue_id();
    bool is_scatter = parent_id == router::NO_ASSOCIATED_QUEUE_OBJECT_ID
                          ? false
                          : this->op_queue_output_scatter.at(this->get_queue(parent_id).name);
    return is_scatter;
    }
    else {
        return b.info().is_scatter();
    }
}

bool Router::is_queue_buffer(unique_id_t input_buffer_id) const {
    return this->buffer_map.at(input_buffer_id).is_queue();
}

RouterBufferType Router::get_buffer_type(unique_id_t buffer_id) const {
    return this->buffer_map.at(buffer_id).info().type();
}

bool Router::is_buffer_tilized(unique_id_t buffer_id) const {
    const auto &b = this->buffer_map.at(buffer_id);
    return b.is_queue() ? 
        this->get_queue(b.queue_info().get_parent_queue_id()).layout == IO_LAYOUT::Tilized :        
        !b.info().is_untilized();
}

void Router::update_dram_queue_core_location(unique_id_t buffer_id, tt_cxy_pair queue_location) {
    TT_ASSERT(is_queue_buffer(buffer_id), "Only queues can be updated with subchan core location info");
    this->buffer_map.at(buffer_id).update_core_location(queue_location);
    HwCoreAttributes &core_attrs = this->cluster_resource_model.get_core_attributes(queue_location);
    core_attrs.add_to_buffer_count();
    log_trace(
        tt::LogRouter,
        "update_dram_queue_core_location: Adding buffer {} to buffer count at core: (c={},y={},x={}). Total = {}",
        buffer_id,
        queue_location.chip,
        queue_location.y,
        queue_location.x,
        core_attrs.get_buffer_count()
    );
}

void Router::update_dram_buffer_writers(unique_id_t buffer_id, bool has_writers, int write_port) {
    this->buffer_map.at(buffer_id).update_writers(has_writers, write_port);
}

void Router::update_dram_buffer_readers(unique_id_t buffer_id, bool has_readers, int read_port) {
    this->buffer_map.at(buffer_id).update_readers(has_readers, read_port);
}

std::optional<int> Router::get_available_relay_buffer_stream_at_core(const tt_cxy_pair &relay_buffer_core) const {
    const auto &hw_core = this->cluster_resource_model.get_core_attributes(relay_buffer_core);
    if (hw_core.has_available_relay_buffer_slot()) {
        return hw_core.next_available_relay_buffer_stream();
    }

    return std::nullopt;
}


void Router::change_buffer_size(unique_id_t buffer_id, int new_size_in_bytes) {
    auto &buffer = this->buffer_map.at(buffer_id);
    TT_ASSERT(not buffer.is_queue(), "change_buffer_size cannot be called for queue buffers");

    const auto old_size_in_bytes = buffer.info().allocated_size_in_bytes();
    auto &core_attrs = this->cluster_resource_model.get_core_attributes(buffer.core_location());

    auto bytes_allocated_delta = new_size_in_bytes - old_size_in_bytes;
    TT_ASSERT(bytes_allocated_delta > 0, "Can't shrink buffer sizes");
    core_attrs.allocate_bytes(bytes_allocated_delta);

    buffer.info()._allocated_size_tiles = new_size_in_bytes / tt::size::get_tile_size_in_bytes(buffer.info().data_format(), !buffer.info().is_untilized());
}

bool Router::is_l1_buffer(unique_id_t buffer_id) const {
    auto &buffer = this->buffer_map.at(buffer_id);
    return
        !is_dram_core(tt_xy_pair(buffer.core_location().x, buffer.core_location().y)) &&  // not dram
        !(buffer.is_queue() && buffer.queue_info().get_allocation_info().is_host_mapped); // not host queue
}

unique_id_t Router::add_new_buffer(const tt::buffer_info &buffer_info, chip_id_t chip) {
    unique_id_t buffer_id = get_new_buffer_unique_id();
    this->buffer_map.insert({buffer_id, {buffer_info, chip}});
    this->buffer_output_pipes[buffer_id] = {};
    return buffer_id;
}

unique_id_t Router::add_new_buffer_to_core(const tt_cxy_pair &core_location, const tt::buffer_info &buffer_info) {

    auto &hw_core = this->cluster_resource_model.get_core_attributes(core_location);
    const int buffer_stream = buffer_info.stream_id();
    bool buffer_stream_initialized = buffer_stream >= 0;
    if (buffer_stream_initialized) {
        bool stream_available = hw_core.next_available_relay_buffer_stream() == buffer_stream;
        TT_ASSERT(stream_available);
    }
    TT_ASSERT(this->dram_channels.find(tt_xy_pair(core_location.x, core_location.y)) == this->dram_channels.end());

    const auto buffer_id = add_new_buffer(buffer_info, core_location.chip);
    log_trace(tt::LogRouter, "Adding buffer {} to core (c={},y={},x={})", buffer_id, core_location.chip, core_location.y, core_location.x);

    this->buffer_map.at(buffer_id).update_core_location(core_location);
    // Compute attributes from input/output pipes
    BufferAttributes buffer_attributes = identify_buffer_attributes(buffer_id);

    hw_core.add_buffer(buffer_id, buffer_info, buffer_attributes); 
    log_trace(
        tt::LogRouter,
        "add_new_buffer_to_core: Adding buffer {} to buffer count at core: (c={},y={},x={}). Total = {}. {} B allocated. {} B remaining in core L1",
        buffer_id,
        core_location.chip,
        core_location.y,
        core_location.x,
        hw_core.get_buffer_count(),
        buffer_info.allocated_size_in_bytes(),
        hw_core.available_l1_bytes()
    );
    return buffer_id;
}

/*Currently doesn't support chip-to-chip pipes*/
bool Router::can_assign_connected_pipe_to_core(pipe_segment_id_t const& pipe_segment_id, const tt_cxy_pair &core_location) const {
    const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id);
    bool has_any_output_to_dram = std::any_of(pipe_attrs.has_output_to_dram.begin(), pipe_attrs.has_output_to_dram.end(), [](bool i) { return i; });
    const auto &core_attrs = cluster_resource_model.get_core_attributes(core_location);
    
    bool can_assign_to_core = true;
    can_assign_to_core = can_assign_to_core && (has_any_output_to_dram ? core_attrs.has_available_output_to_dram_slots() : true);
    can_assign_to_core = can_assign_to_core && (pipe_attrs.has_input_from_dram ? core_attrs.has_available_input_from_dram_slot() : true);
    can_assign_to_core = can_assign_to_core && (pipe_attrs.has_gather ? core_attrs.has_extra_streams_available(compute_pipe_segment_gather_extra_stream_use(*this, pipe_segment_id, core_location)) : true);
    can_assign_to_core = can_assign_to_core && (pipe_attrs.has_gather ? core_attrs.can_allocate_bytes(compute_pipe_segment_gather_extra_input_buffering_size(*this, pipe_segment_id)) : true);
    can_assign_to_core = can_assign_to_core && core_attrs.has_available_active_dram_queue_slots(pipe_attrs.active_dram_queues);
    can_assign_to_core = can_assign_to_core && (pipe_attrs.has_multicast ? core_attrs.has_available_multicast_stream_slots(1) : true);
    can_assign_to_core = can_assign_to_core && core_attrs.has_extra_streams_available(compute_pipe_segment_extra_stream_use(*this, pipe_segment_id, core_location));

    return can_assign_to_core;
}

bool Router::can_assign_connected_buffer_to_core(unique_id_t buffer_id, const tt_cxy_pair &core_location) const {
    const auto &core_attrs = cluster_resource_model.get_core_attributes(core_location);
    
    bool can_assign_to_core = true;
    can_assign_to_core = can_assign_to_core && core_attrs.has_extra_streams_available(1);
    can_assign_to_core = can_assign_to_core && core_attrs.can_allocate_bytes(get_buffer(buffer_id).info().allocated_size_in_bytes());

    return can_assign_to_core;

}

bool Router::can_assign_connected_pipe_and_buffer_to_core(pipe_segment_id_t const& pipe_segment_id, unique_id_t buffer_id, const tt_cxy_pair &core_location) const {
    const auto &pipe_attrs = this->collect_pipe_segment_attributes(pipe_segment_id);
    bool has_any_output_to_dram = std::any_of(
        pipe_attrs.has_output_to_dram.begin(), pipe_attrs.has_output_to_dram.end(), [](bool i) { return i; });
    const auto &core_attrs = cluster_resource_model.get_core_attributes(core_location);

    bool can_assign_to_core = true;
    can_assign_to_core =
        can_assign_to_core && (has_any_output_to_dram ? core_attrs.has_available_output_to_dram_slots() : true);
    can_assign_to_core =
        can_assign_to_core && (pipe_attrs.has_input_from_dram ? core_attrs.has_available_input_from_dram_slot() : true);
    can_assign_to_core =
        can_assign_to_core &&
        (pipe_attrs.has_gather ? core_attrs.has_extra_streams_available(compute_pipe_segment_gather_extra_stream_use(
                                     *this, pipe_segment_id, core_location))
                               : true);
    can_assign_to_core =
        can_assign_to_core && core_attrs.can_allocate_bytes(
                                     compute_pipe_segment_gather_extra_input_buffering_size(*this, pipe_segment_id) +
                                     get_buffer(buffer_id).info().allocated_size_in_bytes());
    can_assign_to_core =
        can_assign_to_core && core_attrs.has_available_active_dram_queue_slots(pipe_attrs.active_dram_queues);
    can_assign_to_core =
        can_assign_to_core && (pipe_attrs.has_multicast ? core_attrs.has_available_multicast_stream_slots(1) : true);
    can_assign_to_core =
        can_assign_to_core &&
        core_attrs.has_extra_streams_available(
            compute_pipe_segment_extra_stream_use(*this, pipe_segment_id, core_location) + 1 /*for buffer*/);

    return can_assign_to_core;
}

/* Take a copy of the op_name in case the original argument passed in is one
 * of the string references from the op maps in router
 */
void Router::erase_op(const std::string op_name) {
    TT_ASSERT(netlist_utils::is_valid_ethernet_op(this->op_info_map.at(op_name).type), "Can only erase ethernet datacopy ops");
    // iterate over all op output buffers
    for (const auto &[_, row_operand_buffers] : this->op_input_buf_map.at(op_name)) {
        for (const auto &[_, operand_buffers] : row_operand_buffers) {
            for (const auto &[_, operand_buffer_id] : operand_buffers) {
                this->op_input_buffer_op_name_map.erase(operand_buffer_id);
                this->op_input_buffer_index_map.erase(operand_buffer_id);
            }
        }
    }
    this->op_input_buf_map.erase(op_name);

    // iterate over all op output buffers
    for (const auto &[_, row_output_buffers] : this->op_output_buf_map.at(op_name)) {
        for (const auto &[_, output_buffer] : row_output_buffers) {
            this->op_output_buffer_op_name_map.erase(output_buffer);
        }
    }
    this->op_output_buf_map.erase(op_name);

    this->op_intermediate_buf_map.erase(op_name);
    this->op_info_map.erase(op_name);
}

void Router::replace_pipe_output(unique_id_t pipe_id, unique_id_t old_buffer_id, unique_id_t new_buffer_id) {
    pipe_t &pipe = this->pipes.at(pipe_id);
    
    for (int s = 0; s < pipe.number_of_output_segments(); s++) {
        pipe_output_list_t &segment_outputs = pipe.output_segment_output_buffer_ids(s);
        bool found = std::find(segment_outputs.begin(), segment_outputs.end(), old_buffer_id) != segment_outputs.end();
        if (found) {
            this->remove_pipe_resource_usage_from_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=s});
            for (
                auto iter = std::find(segment_outputs.begin(), segment_outputs.end(), old_buffer_id); 
                iter != segment_outputs.end(); 
                iter = std::find(iter, segment_outputs.end(), old_buffer_id)
            ) {
                *iter = new_buffer_id;
            }

            // const auto &all_output_buf_ids = collect_output_buffer_ids_for_pipe(pipe);
            this->buffer_input_pipes.erase(old_buffer_id);
            this->buffer_input_pipes.insert({new_buffer_id, pipe_id});
            add_pipe_resource_usage_to_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=s}, get_pipe(pipe_id).core_location());
        }
    }
}

void Router::replace_pipe_input(unique_id_t pipe_id, unique_id_t old_buffer_id, unique_id_t new_buffer_id) {
    pipe_t &pipe = this->pipes.at(pipe_id);
    // auto old_inputs = pipe.input_buffer_ids;
    bool scatter_pipe = pipe.has_multiple_timesteps();
    if (!scatter_pipe) {
        // The pipe resources shouldn't be changing because we are in either one of two scenarios:
        // Reading from DRAM in which case we can't implement this type of pipe (so ignored here)
        // Replacing an op output buffer (pipe input) with something else. That's only possible for 
        // ethernet datacopy optimization (if we remove it) in which case the resource usage from the pipe itself 
        // wouldn't change.
        this->remove_pipe_resource_usage_from_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0});  // this should be done before we update the input buffer IDs
    }
    auto &inputs = pipe.input_buffer_ids;
    for (
        auto iter = std::find(inputs.begin(), inputs.end(), old_buffer_id); 
        iter != inputs.end(); 
        iter = std::find(iter, inputs.end(), old_buffer_id)
    ) {
        *iter = new_buffer_id;
    }
    
    this->buffer_output_pipes.at(old_buffer_id).erase(pipe_id);
    this->buffer_output_pipes[new_buffer_id].insert(pipe_id);
    if (!scatter_pipe) {
        // See comment above `remove_pipe_resource_usage_from_core` call
        add_pipe_resource_usage_to_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}, get_pipe(pipe_id).core_location());
    }
}

void Router::replace_scatter_pipe_outputs_at_segment(pipe_segment_id_t const& pipe_segment_id, const pipe_output_list_t &new_outputs) {
    // TODO(snijjar): Update to handle extra streams - move off of `collect buffer attributes` only flow
    auto &pipe = this->pipes.at(pipe_segment_id.pipe_id);
    TT_ASSERT(pipe.is_scatter());
    int num_timesteps = pipe.time_multiplexed_output_buffer_ids().size();
    TT_ASSERT(pipe_segment_id.segment < num_timesteps);
    TT_ASSERT(new_outputs.size() == 1,
        "Currently can only support replacing multi-timestep pipe outputs with 1 output buf per timestep. To enable \
        this, proper core resource tracking needs implementing"
    );

    remove_pipe_resource_usage_from_core(pipe_segment_id);

    // update the buffer input/output connection maps
    const auto old_outs = pipe.time_multiplexed_output_buffer_ids()[pipe_segment_id.segment];
    bool outputs_changed = not std::equal(std::begin(old_outs), std::end(old_outs), std::begin(new_outputs), std::end(new_outputs));
    if (outputs_changed) {
        for (unique_id_t id : old_outs) {
            if (buffer_input_pipes.at(id) == pipe_segment_id.pipe_id)  {
                // TODO (snijjar) : need to fix this to replace all scatter targets that go to this buffer with this new output
                //  and avoid erasing it repeatedly
                this->buffer_input_pipes.erase(id);
            }
        }
        for (unique_id_t id : new_outputs) {
            if (buffer_input_pipes.find(id) != buffer_input_pipes.end()) {
                TT_ASSERT(buffer_input_pipes[id] == pipe_segment_id.pipe_id, "New scatter pipe output buffer {} already has input pipe {}, but is being assigned to pipe {}.", id, buffer_input_pipes[id], pipe_segment_id.pipe_id);
            } else {
                this->buffer_input_pipes.insert({id, pipe_segment_id.pipe_id}); 
            }
        }
        pipe.time_multiplexed_output_buffer_ids()[pipe_segment_id.segment] = new_outputs;
    }

    if (this->buffer_map.at(new_outputs.at(0)).core_location_assigned()) {
        pipe.locations.at(pipe_segment_id.segment) = this->get_buffer_location(new_outputs.at(0));
    }
    add_pipe_resource_usage_to_core(pipe_segment_id, pipe.scatter_segment_core_location(pipe_segment_id.segment));
}

void  Router::replace_scatter_pipe_outputs(unique_id_t pipe_id, const time_multiplexed_outputs_t &new_outputs) {
    // TODO(snijjar): Update to handle extra streams - move off of `collect buffer attributes` only flow
    auto &pipe = this->pipes.at(pipe_id);
    TT_ASSERT(pipe.has_multiple_timesteps());
    auto num_segments = pipe.time_multiplexed_output_buffer_ids().size();
    TT_ASSERT(num_segments == new_outputs.size());
    TT_ASSERT(
        std::all_of(std::begin(new_outputs), std::end(new_outputs), [](const auto &v) { return v.size() == 1; }), 
        "Currently can only support replacing multi-timestep pipe outputs with 1 output buf per timestep. To enable \
        this, proper core resource tracking needs implementing"
    );

    // update the buffer input/output connection maps
    for (int s = 0; s < static_cast<int>(num_segments); s++) {
        // Also updates the core resource attributes
        replace_scatter_pipe_outputs_at_segment(pipe_segment_id_t{.pipe_id=pipe_id,.segment=s}, new_outputs.at(s));
    }
}

unique_id_t Router::get_new_pipe_unique_id() {
    auto id = this->unique_id_generator.get_next_unique_id(this->UNIQUE_ID_ALIGN);
    hronological_unique_id_list.emplace_back(id);
    return id;
}

unique_id_t Router::get_new_buffer_unique_id() {
    return this->get_new_pipe_unique_id();
}



unique_id_t Router::add_new_pipe(const pipe_t &pipe) {
    TT_ASSERT(pipe.locations.size() == 0);

    const auto &[in_buf_ids, out_buf_ids] = collect_buffer_ids_for_pipe(pipe);

    // Add the pipe and update the buffer input/output connection maps
    unique_id_t pipe_id = get_new_pipe_unique_id();
    this->pipes.insert({pipe_id, pipe});
    for (auto id : in_buf_ids) { 
        this->buffer_output_pipes[id].insert(pipe_id); 
    }
    for (auto id : out_buf_ids) { 
        TT_ASSERT(this->buffer_input_pipes.find(id) == this->buffer_input_pipes.end());
        this->buffer_input_pipes.insert({id, pipe_id}); 
    }

    // Add the forking factor

    for (int s = 0; s < pipe.number_of_output_segments(); s++) {
        add_pipe_resource_usage_to_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=s});
    }

    return pipe_id;
}


unique_id_t Router::create_gather_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, unique_id_t consumer_buffer_id, int consumer_tile_granularity) {
    pipe_t gather = pipe_t(
        producer_buffer_ids,
        pipe_output_list_t{consumer_buffer_id}
    );
    gather.consumer_tile_granularity = consumer_tile_granularity;
    TT_ASSERT(this->buffer_input_pipes.find(consumer_buffer_id) == this->buffer_input_pipes.end());
    unique_id_t pipe_id = this->add_new_pipe(gather);

    return pipe_id;
}
unique_id_t Router::create_gather_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, unique_id_t consumer_buffer_id, const tt_cxy_pair &location, int consumer_tile_granularity) {
    unique_id_t pipe_id = create_gather_pipe_connection(producer_buffer_ids, consumer_buffer_id, consumer_tile_granularity);
    assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}

unique_id_t Router::create_multicast_pipe_connection(unique_id_t producer_buffer_id, const std::vector<unique_id_t> &consumer_buffer_ids, int consumer_tile_granularity) {
    pipe_t multicast = pipe_t(
        {producer_buffer_id},
        consumer_buffer_ids
    );
    multicast.consumer_tile_granularity = consumer_tile_granularity;
    unique_id_t pipe_id = this->add_new_pipe(multicast);

    return pipe_id;
}
unique_id_t Router::create_multicast_pipe_connection_at_core(unique_id_t producer_buffer_id, const std::vector<unique_id_t> &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_tile_granularity) {
    unique_id_t pipe_id = create_multicast_pipe_connection(producer_buffer_id, consumer_buffer_ids, consumer_tile_granularity);
    assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}


unique_id_t Router::create_scatter_pipe_connection_at_core(unique_id_t producer_buffer_id, const std::vector<unique_id_t> &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_tile_granularity) {
    pipe_t multicast = pipe_t(
        {producer_buffer_id},
        consumer_buffer_ids
    );
    multicast.consumer_tile_granularity = consumer_tile_granularity;
    unique_id_t pipe_id = this->add_new_pipe(multicast);
    this->assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}

unique_id_t Router::create_gather_multicast_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, const std::vector<unique_id_t> &consumer_buffer_ids, int consumer_tile_granularity) {
    pipe_t gather_multicast = pipe_t(
        producer_buffer_ids,
        consumer_buffer_ids
    );
    gather_multicast.consumer_tile_granularity = consumer_tile_granularity;
    unique_id_t pipe_id = this->add_new_pipe(gather_multicast);

    return pipe_id;
}
unique_id_t Router::create_gather_multicast_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, const std::vector<unique_id_t> &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_tile_granularity) {
    unique_id_t pipe_id = create_gather_multicast_pipe_connection(producer_buffer_ids, consumer_buffer_ids, consumer_tile_granularity);
    this->assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}

unique_id_t Router::create_gather_scatter_pipe_connection(const std::vector<unique_id_t> &producer_buffer_ids, const std::vector<unique_id_t> &consumer_buffer_ids, int consumer_tile_granularity) {
    pipe_t gather_multicast = pipe_t(
        producer_buffer_ids,
        consumer_buffer_ids
    );
    gather_multicast.consumer_tile_granularity = consumer_tile_granularity;
    unique_id_t pipe_id = this->add_new_pipe(gather_multicast);
    return pipe_id;
}


unique_id_t Router::create_scatter_pipe_connection_at_cores(
    const std::vector<unique_id_t> &producer_buffer_ids, 
    const time_multiplexed_outputs_t &scatter_segments_consumer_buffer_ids, 
    const std::vector<tt_cxy_pair> &locations
) {
    unique_id_t pipe_id = this->add_new_pipe(pipe_t(
        producer_buffer_ids,
        scatter_segments_consumer_buffer_ids
    ));
    pipe_t &tm_pipe = this->pipes.at(pipe_id);

    int num_scatter_segments = scatter_segments_consumer_buffer_ids.size();
    TT_ASSERT(num_scatter_segments == locations.size(), "Number of scatter segments must match number of locations");
    for (int i = 0; i < num_scatter_segments; i++) {
        assign_pipe_scatter_segment_to_core(pipe_id, i, locations[i]);
    }
    tm_pipe.locations = locations;

    return pipe_id;
}

unique_id_t Router::create_gather_scatter_pipe_connection_at_core(const std::vector<unique_id_t> &producer_buffer_ids, const std::vector<unique_id_t> &consumer_buffer_ids, const tt_cxy_pair &location, int consumer_tile_granularity) {
    unique_id_t pipe_id = create_gather_scatter_pipe_connection(producer_buffer_ids, consumer_buffer_ids, consumer_tile_granularity);
    this->assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}


unique_id_t Router::create_unicast_pipe_connection(unique_id_t producer_buffer_id, unique_id_t consumer_buffer_id, int consumer_tile_granularity) {
    pipe_t unicast = pipe_t(
        {producer_buffer_id},
        pipe_output_list_t{consumer_buffer_id}
    );
    unicast.consumer_tile_granularity = consumer_tile_granularity;
    unique_id_t pipe_id = this->add_new_pipe(unicast);
    return pipe_id;
}
unique_id_t Router::create_unicast_pipe_connection_at_core(unique_id_t producer_buffer_id, unique_id_t consumer_buffer_id, const tt_cxy_pair &location, int consumer_tile_granularity) {
    unique_id_t pipe_id = create_unicast_pipe_connection(producer_buffer_id, consumer_buffer_id, consumer_tile_granularity);
    this->assign_pipe_to_core(pipe_id, location);
    return pipe_id;
}


void Router::remove_buffer(unique_id_t id) {
    const auto &buffer = get_buffer(id);

    if (buffer.core_location_assigned()) {
        auto &core_attrs = cluster_resource_model.get_core_attributes(buffer.core_location());
        switch (get_buffer_type(id)) {
            case tt::RouterBufferType::Input: core_attrs.remove_input_buffer(id); break;
            case tt::RouterBufferType::Output: core_attrs.remove_output_buffer(id); break;
            case tt::RouterBufferType::Relay: core_attrs.remove_relay_buffer(id); break;
            default:
                TT_ASSERT(
                    false,
                    "Only RouterBufferType::Relay buffers can be removed, or RouterBufferType::Input/Output buffers "
                    "for ethernet datacopy ops");
        };

        int fork_factor = (buffer_output_pipes.find(id) != buffer_output_pipes.end()) ? buffer_output_pipes.at(id).size() : 1;
        if (!buffer.is_queue() && (fork_factor > 1 || buffer.info().type() == RouterBufferType::Relay)) {
            int extra_streams_to_remove = buffer.info().type() == RouterBufferType::Relay ? fork_factor : fork_factor - 1;
            core_attrs.adjust_extra_streams(-extra_streams_to_remove);
        }
    }
    TT_ASSERT(!buffer.is_queue() && (get_buffer_type(id) == RouterBufferType::Relay || get_buffer_type(id) == RouterBufferType::Input) || get_buffer_type(id) == RouterBufferType::Output);
    TT_ASSERT(buffer_input_pipes.find(id) == buffer_input_pipes.end());
    if (buffer_output_pipes.find(id) != buffer_output_pipes.end()) {
        TT_ASSERT(buffer_output_pipes.at(id).size() == 0);
        buffer_output_pipes.erase(id);
    }
    buffer_map.erase(id);
}

void Router::assign_buffer_to_core(unique_id_t buffer_id, const tt_cxy_pair &core_location) {
    
    auto &buffer = this->buffer_map.at(buffer_id);
    auto &core_attributes_tracker = this->cluster_resource_model.get_core_attributes(core_location);
    auto const& snapshot_before = HwCoreResourceUsageSnapshot::create(core_attributes_tracker, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
    
    int fork_factor = (buffer_output_pipes.find(buffer_id) != buffer_output_pipes.end()) ? buffer_output_pipes.at(buffer_id).size() : 1;
    log_trace(tt::LogRouter,"-----");
    if (buffer.core_location_assigned()) {
        int num_extra_streams_removed = 1;
        // cluster_resource_model.get_core_attributes(buffer.core_location()).adjust_extra_streams(-1); // haandled by remove relay buffer
        tt_cxy_pair const old_buffer_location = buffer.core_location();
        auto &old_core_location_attrs = this->cluster_resource_model.get_core_attributes(old_buffer_location);
        auto const&old_snapshot = HwCoreResourceUsageSnapshot::create(old_core_location_attrs, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
        if (!buffer.is_queue() && (fork_factor > 1 || buffer.info().type() == RouterBufferType::Relay)) {
            int extra_streams_to_remove = std::max<int>(0, buffer.info().type() == RouterBufferType::Relay ? fork_factor : fork_factor - 1);
            old_core_location_attrs.adjust_extra_streams(-extra_streams_to_remove);
            num_extra_streams_removed += extra_streams_to_remove;
        }
        TT_ASSERT(buffer.info().type() == RouterBufferType::Relay);
        old_core_location_attrs.remove_relay_buffer(buffer_id);
        auto const&new_snapshot = HwCoreResourceUsageSnapshot::create(old_core_location_attrs, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
        
        log_trace(
            tt::LogRouter,
            "assign_buffer_to_core: removing buffer {} from core: (c={},y={},x={}). Resources delta: {}. Total resources: {}",
            buffer_id,
            old_buffer_location.chip,
            old_buffer_location.y,
            old_buffer_location.x,
            new_snapshot - old_snapshot,
            new_snapshot
        );
    }
    if (not buffer.is_queue()) {
        int old_extra_stream_count = core_attributes_tracker.get_used_extra_streams();
        auto next_available_stream_id = core_attributes_tracker.next_available_relay_buffer_stream();
        buffer.info()._stream_id = next_available_stream_id;
        core_attributes_tracker.add_buffer(buffer_id, buffer.info(), {});
        int new_extra_stream_count = core_attributes_tracker.get_used_extra_streams();
        int extra_streams_used = new_extra_stream_count - old_extra_stream_count;
        log_trace(
            tt::LogRouter,
            "assign_buffer_to_core: adding buffer {} to buffer count at core: (c={},y={},x={}). Total = {}. Extra streams used = {}",
            buffer_id,
            core_location.chip,
            core_location.y,
            core_location.x,
            core_attributes_tracker.get_buffer_count(),
            extra_streams_used
        );
    } else {
        TT_ASSERT(false, "UNTESTED PATH");
    }

    // Technically this will leave gaps if we ever move around relay buffers. To avoid this we need to track the relay buffers within each
    // `HwCoreAttribtues` object. For now we accept this while refactoring since we don't implement this anywhere but very soon after this
    // we will need to implement that.

    // Ideally this could be handled directly by pipe add/remove resource helpers but maybe it's not possible to keep the counts consistent 
    // that way... Update - it's handled in the remove relay buffer and add_buffer methods
    // cluster_resource_model.get_core_attributes(core_location).adjust_extra_streams(1); 
    // log_trace(tt::LogRouter,"Added buffer {} to core (c={},y={},x={}). {} B + {} B = {} B used in L1", buffer_id, core_location.chip, core_location.y, core_location.x, old_l1_usage, delta, new_l1_usage);
    buffer.update_core_location(core_location);
    if (!buffer.is_queue() && (fork_factor > 1 || buffer.info().type() == RouterBufferType::Relay)) {
        int extra_streams_to_add = buffer.info().type() == RouterBufferType::Relay ? fork_factor : fork_factor - 1;
        auto &core_attrs = this->cluster_resource_model.get_core_attributes(core_location);
        log_trace(tt::LogRouter,"Router::assign_buffer_to_core buffer {} has fork factor {}", buffer_id, extra_streams_to_add);
        core_attrs.adjust_extra_streams(extra_streams_to_add);
    }
    auto const& snapshot_after = HwCoreResourceUsageSnapshot::create(core_attributes_tracker, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
    auto const& snapshot_delta = snapshot_after - snapshot_before;
    log_trace(
        tt::LogRouter,
        "assign_buffer_to_core: adding buffer {} to core: (c={},y={},x={}). Total = {}. Resources used: {}. Total used on core: {}",
        buffer_id,
        core_location.chip,
        core_location.y,
        core_location.x,
        core_attributes_tracker.get_buffer_count(),
        snapshot_delta,
        snapshot_after
    );
}


void Router::assign_pipe_to_core(unique_id_t pipe_id, const tt_cxy_pair &core_location) {
    auto &pipe = this->pipes.at(pipe_id);
    TT_ASSERT(pipe.locations.size() == 0 || pipe.locations.size() == 1);
    auto const &core_attributes_tracker = this->cluster_resource_model.get_core_attributes(core_location);
    auto const &snapshot_before = HwCoreResourceUsageSnapshot::create(
        core_attributes_tracker, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
    this->assign_pipe_scatter_segment_to_core(pipe_id, 0, core_location);
    auto const &snapshot_after = HwCoreResourceUsageSnapshot::create(
        core_attributes_tracker, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
    log_debug(
        tt::LogRouter,
        "Assigned pipe {} to core (c={},y={},x={}). Resource use delta: {}. Resource use total: {}",
        pipe_id,
        core_location.chip,
        core_location.y,
        core_location.x,
        snapshot_after - snapshot_before,
        snapshot_after);
}

void Router::assign_pipe_scatter_segment_to_core(unique_id_t pipe_id, int segment, const tt_cxy_pair &core_location) {
    auto &pipe = this->pipes.at(pipe_id);
    
    remove_pipe_resource_usage_from_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=segment});
    if (pipe.locations.size() == 0) {
        TT_ASSERT(segment == 0);
        pipe.locations.push_back(core_location);
    } else {
        TT_ASSERT(static_cast<int>(pipe.locations.size()) > segment || pipe.locations.size() == segment);
        if (pipe.locations.size() == segment) {
            pipe.locations.push_back(core_location);
        } else {
            pipe.locations.at(segment) = core_location;
        }
    }

    add_pipe_resource_usage_to_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=segment}, core_location);
}


void Router::disconnect_pipe(unique_id_t pipe_id) {
    log_trace(tt::LogRouter, "Disconnecting pipe {}", pipe_id);
    auto &pipe = this->pipes.at(pipe_id);
    if (pipe.core_location_assigned()) {
        for (int s = 0; s < pipe.number_of_output_segments(); s++) {
            tt_cxy_pair const& location = pipe.scatter_segment_core_location(s);
            auto const& core_resources = this->cluster_resource_model.get_core_attributes(location);
            auto const& snapshot_before = HwCoreResourceUsageSnapshot::create(core_resources, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});

            this->remove_pipe_resource_usage_from_core(pipe_segment_id_t{.pipe_id=pipe_id, .segment=s});

            auto const& snapshot_after = HwCoreResourceUsageSnapshot::create(core_resources, {ResourceUsageType::L1_MEMORY, ResourceUsageType::EXTRA_STREAMS});
            log_trace(tt::LogRouter,
                "\tsegment: {}s on core (c={},y={},x={}). Resources regained: {}. Resources total on core: {}",
                s,
                location.chip,
                location.y,
                location.x,
                snapshot_before - snapshot_after,
                snapshot_after);
        }
    }

    std::unordered_set<unique_id_t> seen_output_buffer_ids = {};
    const auto &[in_buf_ids, out_buf_ids] = collect_buffer_ids_for_pipe(pipe);
    // Disconnect the pipe
    for (auto id : in_buf_ids) {
        this->buffer_output_pipes.at(id).erase(pipe_id); 
    }
    int c = 0;
    for (auto id : out_buf_ids) { 
        if (seen_output_buffer_ids.find(id) != seen_output_buffer_ids.end()) {
            TT_ASSERT(this->buffer_input_pipes.find(id) == this->buffer_input_pipes.end(), "Should have been cleared earlier in this function");
        } else {
            TT_ASSERT(this->buffer_input_pipes.at(id) == pipe_id);
            this->buffer_input_pipes.erase(id); 
        }

        seen_output_buffer_ids.insert(id);
        c++;
    }

    pipe.input_buffer_ids.clear();
    pipe.output_buffer_ids().clear();
}

void Router::remove_buffer_attributes(const tt_cxy_pair &core, const BufferAttributes &attrs) {
    auto &core_attrs = cluster_resource_model.get_core_attributes(core);
    core_attrs.remove_buffer_attributes(attrs);
}

void Router::apply_buffer_attributes(const tt_cxy_pair &core, const BufferAttributes &attrs) {
    auto &core_attrs = cluster_resource_model.get_core_attributes(core);
    core_attrs.apply_buffer_attrs(attrs);
}

void Router::apply_buffer_attribute_diffs(
    const std::unordered_map<tt_cxy_pair, BufferAttributes> &old_attrs, 
    const std::unordered_map<tt_cxy_pair, BufferAttributes> &new_attrs
) {
    for (const auto &[location, attrs] : old_attrs) {
        remove_buffer_attributes(location, attrs);
    }
    for (const auto &[location, attrs] : new_attrs) {
        apply_buffer_attributes(location, attrs);
    }
}

// Assumes the pipe is already disconnected
void Router::remove_pipe(unique_id_t pipe_id) {
    const auto pipe = this->pipes.at(pipe_id);
    TT_ASSERT(pipe.input_buffer_ids.size() == 0);
    TT_ASSERT((pipe.has_multiple_timesteps() && pipe.time_multiplexed_output_buffer_ids().size() == 0) || (pipe.output_buffer_ids().size() == 0));   
    this->pipes.erase(pipe_id);
}


/* We compute this dynamically because the attributes are topology dependent, and the router may end up changing the topology
 * throughout compilation. If this ever becomes a bottleneck, there are many ways to possibly optimize.
 */
BufferAttributes Router::identify_buffer_attributes(unique_id_t buffer_id) const {
    // TODO (snijjar): Most of these buffer attributes can be deprecated since they are really attributes of the outgoing pipes
    //                 and are already calculated for the outgoing pipes
    const auto &buffer = this->buffer_map.at(buffer_id);
    chip_id_t chip_location = buffer.chip_location();
    auto attributes = BufferAttributes();

    bool input_from_dram = false;
    bool input_from_eth = false;
    bool buffer_has_input_pipe = this->buffer_input_pipes.find(buffer_id) != this->buffer_input_pipes.end();
    if (buffer_has_input_pipe) {
        auto input_pipe_id = this->buffer_input_pipes.at(buffer_id);
        const pipe_t input_pipe = this->get_pipes().at(input_pipe_id);

        input_from_dram = std::any_of(input_pipe.input_buffer_ids.begin(), input_pipe.input_buffer_ids.end(), [&] (unique_id_t id) {
            return this->buffer_map.at(id).is_queue();
        });

        input_from_eth = this->buffer_map.at(input_pipe.input_buffer_ids.at(0)).chip_location() != chip_location;
    }
    if (input_from_dram) {
        attributes.set_input_from_dram();
    }

    int multicasts = 0;
    int outputs_to_dram = 0;
    int output_to_ethernet = false;
    bool buffer_has_output_pipes = this->buffer_output_pipes.find(buffer_id) != this->buffer_output_pipes.end();
    if (buffer_has_output_pipes) {
        for (auto out_pipe_id : this->buffer_output_pipes.at(buffer_id)) {
            const pipe_t &pipe = this->pipes.at(out_pipe_id);

            if (pipe.has_multiple_timesteps()) {
                for (const auto &timestep_output_buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
                    for (unique_id_t out_buf : timestep_output_buf_ids) {
                        TT_ASSERT(this->buffer_map.at(out_buf).is_queue() == false, "TM pipe writing to DRAM is unsupported");
                    }
                }
            } else {
                const auto &out_bufs = pipe.output_buffer_ids();
                outputs_to_dram += std::find_if(out_bufs.begin(), out_bufs.end(), [&](unique_id_t buf_id) { return this->is_queue_buffer(buf_id); }) != out_bufs.end() ? 1 : 0;

                if (out_bufs.size() > 1) {
                    multicasts++;
                } 
                    output_to_ethernet = this->buffer_map.at(out_bufs.at(0)).chip_location() != chip_location;
            }
        }
    }
    attributes.set_outputs_to_dram(outputs_to_dram);
    attributes.set_multicast(multicasts);
    attributes.set_ethernet_io_streams(static_cast<int>(input_from_eth) + static_cast<int>(output_to_ethernet));

    int forking_factor = 1;
    if (buffer_output_pipes.find(buffer_id) != buffer_output_pipes.end()) {
        forking_factor += std::max<int>(0, buffer_output_pipes.at(buffer_id).size() - 1);
    }
    if (!buffer.is_queue() && buffer.info().type() == RouterBufferType::Relay) {
        forking_factor += 1; // relay buffer requires extra stream
    }
    attributes.set_forking_factor(forking_factor);

    return attributes;
}

/* Intended to be called at the start of router invocation.
 * - Creeats a lookup table that maps a dram core's noc-grid location to the dram channel/subchannel associated with that core
 */
std::unordered_map<tt_xy_pair, dram_channel_t> Router::initialize_dram_core_map(const std::unordered_map<chip_id_t, buda_SocDescriptor> &soc_descriptors) {
    auto dram_channels = std::unordered_map<tt_xy_pair, dram_channel_t>{};
    const int num_channels = soc_descriptors.begin()->second.dram_cores.size();
    for (int channel = 0; channel < num_channels; channel++) {
        const auto &subchannel_cores = soc_descriptors.begin()->second.dram_cores[channel];
        const int num_subchannels = subchannel_cores.size();
        for (int subchannel = 0; subchannel < num_subchannels; subchannel++) {
            const tt_xy_pair &loc = subchannel_cores[subchannel];
            dram_channels.insert({loc, dram_channel_t{.channel=channel,.subchannel=subchannel}});
        }
    }

    return dram_channels;
}

std::unordered_map<tt_xy_pair, int> get_pipegen_core_memory_overrides(const buda_SocDescriptor &soc_descriptor) {
    auto get_value = [](auto b, auto e) {
        return std::stoi(std::string(b, e)); // not the most efficient...
    };

    std::unordered_map<tt_xy_pair, int> pipegen_per_core_mem_override = {};

    auto pipegen_l1_override_env_var = std::getenv("PIPEGEN_GLOBAL_L1_OVERRIDE");
    if (pipegen_l1_override_env_var != nullptr) {
        int override_size = stoi(pipegen_l1_override_env_var);
        log_debug(tt::LogRouter, "Overriding L1 budget for pipegen to {} B globally for all worker cores", override_size);
        for (const auto &[core_physical_coords, core_descriptor] : soc_descriptor.cores) {
            if (soc_descriptor.is_worker_core(core_physical_coords)) {
                pipegen_per_core_mem_override[core_physical_coords] = override_size;
            }
        }
    }

    auto pipegen_mem_override_env_var = std::getenv("PIPEGEN_PER_CORE_MEM_OVERRIDE");
    // Schema is <token>[:<token>[:...]]
    // token = core_y,core_x,pipegen_usable_l1 (in bytes)
    const auto &token_delimiter = ":";
    const auto &field_delimiter = ",";
    if (pipegen_mem_override_env_var != nullptr) {
        auto full_string = std::string(pipegen_mem_override_env_var);
        auto token_start = full_string.begin();
        while (token_start != full_string.end()) {
            auto first_comma = std::find(token_start, full_string.end(), ','); TT_ASSERT(first_comma != full_string.end());
            int core_y = get_value(token_start, first_comma);
            
            token_start = std::next(first_comma, 1);
            auto second_comma = std::find(token_start, full_string.end(), ','); TT_ASSERT(second_comma != full_string.end());
            int core_x = get_value(token_start, second_comma);
            
            token_start = std::next(second_comma, 1);
            auto token_end = std::find(token_start, full_string.end(), ':');
            int bytes = get_value(token_start, token_end);

            log_debug(tt::LogRouter, "Core (y={},x={}) L1 space for pipegen overridden to {} B", core_y, core_x, bytes);
            pipegen_per_core_mem_override[tt_xy_pair(core_x, core_y)] = bytes;

            token_start = token_end == full_string.end() ? token_end : std::next(token_end, 1);
            log_debug(tt::LogRouter, "\ty={}, x={}, override={}", core_y, core_x, bytes);
        }
    }

    return pipegen_per_core_mem_override;
}

/* Initializes the core resource management objects for every core in the cluster. Intended to be called at the start of router invocation.
 * 
 * In the current revision, this function is not aware of temporal vs spatial epochs and assumes only a single epoch for core resource 
 * management.
 */
std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> Router::initialize_hw_cores(const std::unordered_map<chip_id_t, buda_SocDescriptor> &soc_descriptors, const AddressMap &address_map, const std::vector<chip_id_t> &chip_ids) {
    std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> core_attributes = {};

    auto get_pipegen_stream_mem_per_core = [](const std::unordered_map<tt_xy_pair, int> &overrides, const tt_cxy_pair &location) {
        const auto &core_xy = tt_xy_pair(location.x, location.y);
        if (overrides.find(core_xy) != overrides.end()) {
            log_debug(tt::LogRouter, "Using override for core (y={},x={}) pipegen stream memory", core_xy.y, core_xy.x);
            return std::min(overrides.at(core_xy), PIPEGEN_STREAM_MEM_PIPE_PER_CORE/* + NET2PIPE_STREAM_MEM_PIPE_PER_CORE*/);
        } else {
            return 0;
        }
    };

    auto get_blob_extra_size = []() -> int {
        int extra_blob_size = env_var("TT_BACKEND_OVERLAY_MAX_EXTRA_BLOB_SIZE", 0);
        return (extra_blob_size > 0) ? extra_blob_size : 0;
    };

    for (const chip_id_t &device_id : chip_ids) {
        auto const& soc_descriptor = soc_descriptors.at(device_id);
        for (const auto &[core_routing_coords, core_descriptor] : soc_descriptor.cores) {
            TT_ASSERT(core_routing_coords == core_descriptor.coord);
            bool is_ethernet_core = (core_descriptor.type == CoreType::ETH);
            bool is_worker_core = (core_descriptor.type == CoreType::WORKER);

            const tt_cxy_pair &core_location = tt_cxy_pair(device_id, core_routing_coords);
            int l1_size = is_ethernet_core ? address_map.ethernet_l1.MAX_SIZE
                          : is_worker_core ? address_map.l1.MAX_SIZE
                                           : 0;
                                           
            core_attributes.insert(std::move(std::make_pair(
                core_location,
                std::make_unique<HwCoreAttributes>(
                    l1_size,
                    MAX_DRAM_IO_INPUT_STREAMS,
                    MAX_DRAM_IO_OUTPUT_STREAMS,
                    MAX_TOTAL_ACTIVE_DRAM_QUEUES,
                    is_ethernet_core,
                    address_map.arch_constants.MAX_MCAST_STREAMS_PER_CORE))));

            const auto &pipegen_per_core_mem_override = get_pipegen_core_memory_overrides(soc_descriptor);
            int initial_allocation_size =
                is_ethernet_core ? (address_map.ethernet_l1.DATA_BUFFER_SPACE_BASE +
                                    get_pipegen_stream_mem_per_core(pipegen_per_core_mem_override, core_location))
                                 : (address_map.l1.DATA_BUFFER_SPACE_BASE +
                                    get_pipegen_stream_mem_per_core(pipegen_per_core_mem_override, core_location) +
                                    get_blob_extra_size());
            core_attributes.at(core_location)->allocate_bytes(initial_allocation_size);
            log_trace(
                tt::LogRouter,
                "Core (c={},y={},x={}) total L1 size is {} B, initial allocation size is {} B => {} B available for "
                "use",
                device_id,
                core_routing_coords.y,
                core_routing_coords.x,
                l1_size,
                initial_allocation_size,
                core_attributes.at(core_location)->available_l1_bytes());
        }
    }

    return core_attributes;
}


void Router::run(const netlist_parser &parsed_netlist) {
    run_router_passes(*this, this->get_cluster_graph(), parsed_netlist);
    retag_all_dram_input_pipes_with_consumer_op_metadata(*this);
}

int Router::get_noc_distance(int noc_id, int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip_id) const {
    return (noc_id == 1) ? get_noc1_distance(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing, chip_id)
                         : get_noc0_distance(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing, chip_id);
}

int Router::get_noc0_distance(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const {

  int noc0_x_distance = dest_x_noc0_routing - src_x_noc0_routing;
  if (noc0_x_distance < 0) {
    noc0_x_distance += this->get_soc_descriptor(chip).grid_size.x;
  }
  int noc0_y_distance = dest_y_noc0_routing - src_y_noc0_routing;
  if (noc0_y_distance < 0) {
    noc0_y_distance += this->get_soc_descriptor(chip).grid_size.y;
  }
  return noc0_x_distance + noc0_y_distance;
}


int Router::get_noc1_distance(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const {

  int noc0_x_distance = dest_x_noc0_routing - src_x_noc0_routing;
  if (noc0_x_distance < 0) {
    noc0_x_distance += this->get_soc_descriptor(chip).grid_size.x;
  }
  int noc0_y_distance = dest_y_noc0_routing - src_y_noc0_routing;
  if (noc0_y_distance < 0) {
    noc0_y_distance += this->get_soc_descriptor(chip).grid_size.y;
  }
  int noc1_x_distance = (noc0_x_distance == 0) ? 0 : (this->get_soc_descriptor(chip).grid_size.x - noc0_x_distance);
  int noc1_y_distance = (noc0_y_distance == 0) ? 0 : (this->get_soc_descriptor(chip).grid_size.y - noc0_y_distance);
  return noc1_x_distance + noc1_y_distance;
}
  
  
int Router::get_shortest_path_noc_id(int src_y_noc0_routing, int src_x_noc0_routing, int dest_y_noc0_routing, int dest_x_noc0_routing, chip_id_t chip) const {
  int noc0_hops = get_noc0_distance(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing, chip);
  int noc1_hops = get_noc1_distance(src_y_noc0_routing, src_x_noc0_routing, dest_y_noc0_routing, dest_x_noc0_routing, chip);
  return (noc0_hops > noc1_hops) ? 1 : 0;
}

bool op_forks_to_multiple_consumers(router::Router &router, const std::string &op_name) {
  for (const auto &[grid_r, row_output_buffers] : router.get_op_output_buf_map().at(op_name)) {
    for (const auto &[grid_c, output_buffer] : row_output_buffers) {
            if (router.is_buffer_forked(output_buffer)) {
                return true;
            }
            if (router.get_buffer_output_pipes().at(output_buffer).size() > 1) {
                return true;
            }
    }
  }

  return false;
}

bool is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(router::Router &router, const pipe_t &pipe) {
  bool single_producer = pipe.input_buffer_ids.size() == 1;
  if (not single_producer) {
    return false;
  }

  const auto &producer_buffer = router.get_buffer(pipe.input_buffer_ids.at(0));
  if (pipe.has_multiple_timesteps()) {
    const auto &scatter_output_segments = pipe.time_multiplexed_output_buffer_ids();
    const auto &first_scatter_output_segment = scatter_output_segments.at(0);
    return std::all_of(
        std::begin(scatter_output_segments),
        std::end(scatter_output_segments),
        [&first_scatter_output_segment](const auto &segment) { return segment == first_scatter_output_segment; });
  } else {
    unique_id_t input_buffer_id = pipe.input_buffer_ids.at(0);
    bool has_producer_op = router.get_op_output_buffer_op_name_map().find(input_buffer_id) !=
                           router.get_op_output_buffer_op_name_map().end();
    if (has_producer_op) {
            const auto &producer_op = router.get_op_output_buffer_op_name_map().at(input_buffer_id);
            for (const auto &[r, output_buffer_row] : router.get_op_output_buf_map().at(producer_op)) {
                for (const auto &[c, output_buffer_id] : output_buffer_row) {
                    if (router.is_buffer_forked(output_buffer_id)) {
                        return false;
                    }
                }
            }
    }
    return true;
  }
}

pipe_segment_hash_t create_pipe_segment_hash(Router const &router, pipe_segment_id_t const &pipe_segment_id) {
  return pipe_segment_hash_t{
      .pipe_id = pipe_segment_id.pipe_id,
      .outputs = router.get_pipe(pipe_segment_id.pipe_id).output_segment_output_buffer_ids(pipe_segment_id.segment)};
}

};  // namespace router
