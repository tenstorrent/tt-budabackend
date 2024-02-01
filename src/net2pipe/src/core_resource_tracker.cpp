// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "src/net2pipe/inc/net2pipe_common.h"
#include "src/net2pipe/inc/router.hpp"
#include "common/size_lib.hpp"
#include "router_types.h"

#include "common/tt_core_resource_tracker.hpp"

#include "net2pipe_constants.h"

#include "noc_parameters.h"
#include "src/net2pipe/inc/core_resource_tracker.h"

#include <functional>
#include <unordered_map>
#include <algorithm>
#include <vector>

static constexpr int INPUT_FROM_DRAM_STREAM_LIMIT = MAX_DRAM_IO_INPUT_STREAMS;
static constexpr int OUTPUT_TO_DRAM_STREAM_LIMIT = MAX_DRAM_IO_OUTPUT_STREAMS;

namespace router {


HwCoreAttributes::HwCoreAttributes(
    int l1_size,
    int max_input_from_dram_slots,
    int max_output_to_dram_slots,
    int max_active_dram_queues_limit,
    bool supports_eth_links,
    int max_num_gather_pipes_before_phase_based_gather) :
    tt::CoreResources(
        {.l1_memory_size = l1_size,
         .max_input_from_dram_streams = max_input_from_dram_slots,
         .max_output_to_dram_streams = max_output_to_dram_slots,
         .max_mcast_streams_per_core = MAX_MCAST_STREAMS_PER_CORE,
         .max_num_non_mcast_eth_streams = MAX_NUM_NON_MCAST_ETH_STREAMS,
         .extra_tile_size_header_buffer_size = MAX_TILES_MSG_INFO_BUF_PER_PHASE * TILE_HEADER_SIZE_BYTES,
         .max_extra_streams = MAX_EXTRA_STREAMS,
         .max_active_dram_queues = max_active_dram_queues_limit,
         .max_num_eth_streams_total =
             (supports_eth_links ? (env_var("TT_ENABLE_FW_ETH_STREAMS", 0) == 1 ? MAX_NUM_ETH_STREAMS_TOTAL
                                                                                : MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL)
                                 : 0),
         .used_l1_memory = 0,
         .used_input_from_dram_slots = 0,
         .used_output_to_dram_slots = 0,
         .used_io_buffer_count = 0,
         .used_multicast_non_eth_stream_slots = 0,
         .used_ethernet_stream_slots = 0,
         .used_extra_streams = 0,
         .used_active_dram_streams = 0,
         .used_tile_sizes = {}}),
    max_num_gather_pipes_before_phase_based_gather(max_num_gather_pipes_before_phase_based_gather),
    resident_gather_multicast_pipes(0),
    resource_constraint_asserts_enabled(true),
    tile_sizes_used({}) {}

void HwCoreAttributes::remove_input_buffer(unique_id_t buffer_id) {
    this->input_buffers.erase(std::find(this->input_buffers.begin(), this->input_buffers.end(), buffer_id));
    this->used_io_buffer_count -= 1;
}
void HwCoreAttributes::remove_output_buffer(unique_id_t buffer_id) {
    this->output_buffers.erase(std::find(this->output_buffers.begin(), this->output_buffers.end(), buffer_id));
    this->used_io_buffer_count -= 1;
}
void HwCoreAttributes::remove_relay_buffer(unique_id_t buffer_id) {
    this->relay_buffers.erase(std::find(this->relay_buffers.begin(), this->relay_buffers.end(), buffer_id));
    this->used_io_buffer_count -= 1;
    // used_extra_streams--;
}

void HwCoreAttributes::add_to_buffer_count() {
    this->used_io_buffer_count += 1;
}

bool HwCoreAttributes::has_available_relay_buffer_slot() const {
    return relay_buffers.size() < (HwCoreAttributes::RELAY_BUFFER_STREAM_LAST - HwCoreAttributes::RELAY_BUFFER_STREAM_FIRST);
}

void HwCoreAttributes::remove_buffer_attributes(const BufferAttributes &attrs) {
    int output_to_dram_stream_diff = - attrs.get_num_outputs_to_dram();
    int multicast_diff = -attrs.get_num_multicasts();
    int ethernet_streams_diff = -attrs.get_num_ethernet_io_streams();
    int input_from_dram_diff = -static_cast<int>(attrs.has_input_from_dram());
    
    this->adjust_input_from_dram(input_from_dram_diff);
    this->adjust_outputs_to_dram(output_to_dram_stream_diff);
    this->adjust_ethernet_streams(ethernet_streams_diff);
    validate_stream_counts_not_negative();
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->get_used_ethernet_stream_count() <= this->get_buffer_count(), "Counted extra ethernet streams used than actual buffers placed on chip. This is impossible.");
}
void HwCoreAttributes::apply_buffer_attrs(const BufferAttributes &attrs) {

    int output_to_dram_stream_diff = attrs.get_num_outputs_to_dram();
    int multicast_diff = attrs.get_num_multicasts();
    int ethernet_streams_diff = attrs.get_num_ethernet_io_streams();
    int input_from_dram_diff = static_cast<int>(attrs.has_input_from_dram());
    
    this->adjust_input_from_dram(input_from_dram_diff);
    this->adjust_outputs_to_dram(output_to_dram_stream_diff);
    this->adjust_ethernet_streams(ethernet_streams_diff);
    validate_stream_counts_not_negative();
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->get_used_ethernet_stream_count() <= this->get_buffer_count(), "Counted extra ethernet streams used than actual buffers placed on chip. This is impossible.");
}


int HwCoreAttributes::next_available_relay_buffer_stream() const {
    return this->relay_buffers.size() + HwCoreAttributes::RELAY_BUFFER_STREAM_FIRST;
}


int HwCoreAttributes::count_extra_streams_from_gather_and_multicast() const {
    auto does_not_have_gather = [this](pipe_segment_hash_t const &pipe_segment_id) {
        auto is_match = [&pipe_segment_id](const std::pair<pipe_segment_hash_t, int>  &p) { return pipe_segment_id == p.first; };
        return std::find_if(
                    resident_gather_pipe_input_counts.begin(),
                    resident_gather_pipe_input_counts.end(),
                    is_match) ==
                resident_gather_pipe_input_counts.end();
    };

    int num_multicast_only_pipes =
        std::count_if(resident_multicast_pipes.begin(), resident_multicast_pipes.end(), does_not_have_gather);
    int num_gather_multicasts = resident_multicast_pipes.size() - num_multicast_only_pipes;
    int pipe_count = num_multicast_only_pipes;
    int num_optimized_gather_slots = std::max<int>(0, this->max_num_gather_pipes_before_phase_based_gather - num_multicast_only_pipes);
    int num_optimized_gather_multicasts = std::min<int>(num_gather_multicasts, num_optimized_gather_slots);

    int total = 0;

    pipe_count += num_optimized_gather_multicasts;
    std::unordered_set<unique_id_t> optimized_gather_pipes = {};

    for (auto const &[id, input_count] : resident_gather_pipe_input_counts) {
        bool is_gather_multicast =
            std::find(resident_multicast_pipes.begin(), resident_multicast_pipes.end(), id) !=
            resident_multicast_pipes.end();
        if (is_gather_multicast && num_optimized_gather_multicasts > 0) {
            int num_extra_streams_from_gather_part = std::min(
                MAX_STREAMS_USED_FOR_OPTIMIZED_GATHER,
                input_count);
            total += num_extra_streams_from_gather_part;
            num_optimized_gather_multicasts -= 1;
            optimized_gather_pipes.insert(id.pipe_id);
        } else {
            bool use_optimized_gather = pipe_count < this->max_num_gather_pipes_before_phase_based_gather ||
                                        (optimized_gather_pipes.find(id.pipe_id) != optimized_gather_pipes.end());
            total += use_optimized_gather ? std::min(MAX_STREAMS_USED_FOR_OPTIMIZED_GATHER, input_count) : 1;
            if (use_optimized_gather) {
                optimized_gather_pipes.insert(id.pipe_id);
            }
        }

        pipe_count += !is_gather_multicast;
    }

    return total;
}

void HwCoreAttributes::register_resident_gather_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id, int number_of_gather_inputs) {
    if (number_of_gather_inputs == 0) {
        return;
    }
    pipe_segment_instance_counts[pipe_segment_id]++;
    if (pipe_segment_instance_counts[pipe_segment_id] > 1) {
        return;
    }
    
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();

    resident_multicast_pipes.push_back(pipe_segment_id);
    resident_gather_pipe_input_counts.push_back({pipe_segment_id, number_of_gather_inputs});
    increment_resident_gather_multicast_pipe_count();

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    this->adjust_multicast_streams(1);
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}
void HwCoreAttributes::deregister_resident_gather_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id) {
    pipe_segment_instance_counts[pipe_segment_id]--;
    if (pipe_segment_instance_counts[pipe_segment_id] > 0) {
        return;
    }
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    // we only increment this if we are not already at the cap.
    // If we are at the cap it indicates the stream slot for mcast may already be accounted
    // for by an optimized gather. In that case we wouldn't use an additional mcast and instead
    // just reassign one from one of the optimized gather pipes
    bool decrement_mcast_stream_count = resident_multicast_pipes.size() + resident_gather_pipe_input_counts.size() == MAX_MCAST_STREAMS_PER_CORE;

    auto multicast_pipe_it = std::find(resident_multicast_pipes.begin(), resident_multicast_pipes.end(), pipe_segment_id);
    TT_ASSERT(multicast_pipe_it != resident_multicast_pipes.end());
    resident_multicast_pipes.erase(multicast_pipe_it);

    auto gather_pipe_it = std::find_if(
        resident_gather_pipe_input_counts.begin(),
        resident_gather_pipe_input_counts.end(),
        [&pipe_segment_id](std::pair<pipe_segment_hash_t, int> const &p) { return p.first == pipe_segment_id; });
    TT_ASSERT(gather_pipe_it != resident_gather_pipe_input_counts.end());
    resident_gather_pipe_input_counts.erase(gather_pipe_it);
    decrement_resident_gather_multicast_pipe_count();

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    this->adjust_multicast_streams(-1);
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}

void HwCoreAttributes::register_resident_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id) {
    pipe_segment_instance_counts[pipe_segment_id]++;
    if (pipe_segment_instance_counts[pipe_segment_id] > 1) {
        return;
    }
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();

    resident_multicast_pipes.push_back(pipe_segment_id);
    increment_resident_gather_multicast_pipe_count();
    this->adjust_multicast_streams(1);

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}

void HwCoreAttributes::deregister_resident_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id) {
    pipe_segment_instance_counts[pipe_segment_id]--;
    if (pipe_segment_instance_counts[pipe_segment_id] > 0) {
        return;
    }
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    // we only increment this if we are not already at the cap.
    // If we are at the cap it indicates the stream slot for mcast may already be accounted
    // for by an optimized gather. In that case we wouldn't use an additional mcast and instead
    // just reassign one from one of the optimized gather pipes
    bool decrement_mcast_stream_count = resident_multicast_pipes.size() + resident_gather_pipe_input_counts.size() == MAX_MCAST_STREAMS_PER_CORE;

    auto it = std::find(resident_multicast_pipes.begin(), resident_multicast_pipes.end(), pipe_segment_id);
    TT_ASSERT(it != resident_multicast_pipes.end());
    resident_multicast_pipes.erase(it);
    decrement_resident_gather_multicast_pipe_count();

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    if (decrement_mcast_stream_count) {
        this->adjust_multicast_streams(-1);
    }
    
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}

// Also increments extra streams
void HwCoreAttributes::register_resident_gather_pipe(pipe_segment_hash_t const& pipe_segment_id, int number_of_gather_inputs) {
    if (number_of_gather_inputs == 0) {
        return;
    }
    pipe_segment_instance_counts[pipe_segment_id]++;
    if (pipe_segment_instance_counts[pipe_segment_id] > 1) {
        return;
    }
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();

    resident_gather_pipe_input_counts.push_back({pipe_segment_id, number_of_gather_inputs});
    increment_resident_gather_multicast_pipe_count();

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}

// Also decrements extra streams
void HwCoreAttributes::deregister_resident_gather_pipe(pipe_segment_hash_t const& pipe_segment_id) {
    pipe_segment_instance_counts[pipe_segment_id]--;
    if (pipe_segment_instance_counts[pipe_segment_id] > 0) {
        return;
    }
    int old_extra_stream_count = count_extra_streams_from_gather_and_multicast();

    auto it = std::find_if(
        resident_gather_pipe_input_counts.begin(),
        resident_gather_pipe_input_counts.end(),
        [&pipe_segment_id](std::pair<pipe_segment_hash_t, int> const &p) { return p.first == pipe_segment_id; });
    TT_ASSERT(it != resident_gather_pipe_input_counts.end());
    resident_gather_pipe_input_counts.erase(it);
    decrement_resident_gather_multicast_pipe_count();

    int new_extra_stream_count = count_extra_streams_from_gather_and_multicast();
    this->adjust_extra_streams(new_extra_stream_count - old_extra_stream_count);
}

std::optional<std::vector<BufferAllocationFailureReason>> HwCoreAttributes::reasons_for_buffer_allocation_failure(
    int buffer_size_in_bytes, 
    int tile_size_in_bytes,
    const BufferAttributes &attributes,
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) const {
    std::vector<BufferAllocationFailureReason> allocation_failure_reasons = {};

    buffer_size_in_bytes += extra_bytes_required_from_buffer_tile_size(tile_size_in_bytes);

    if (!can_allocate_bytes(buffer_size_in_bytes)) {
        allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_L1);
    }

    if (this->relay_buffers.size() == HwCoreAttributes::RELAY_BUFFER_LIMIT) {
        allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_Relay_Streams);
    }

    if (attributes.has_multicast()) {
        if (!this->has_available_multicast_stream_slots(attributes.get_num_multicasts())) {
            allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_Multicast_Streams);
        }
    }

    if (attributes.has_input_from_dram()) {
        if (!this->has_available_input_from_dram_slot()) {
            allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_DRAM_Input_Streams);
        }
    }

    if (attributes.has_outputs_to_dram()) {
        if (!this->has_available_output_to_dram_slots(attributes.get_num_outputs_to_dram())) {
            allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_DRAM_Output_Streams);
        }
    }

    if (attributes.has_ethernet_io_streams()) {
        if (!this->has_available_ethernet_stream_slots(attributes.get_num_ethernet_io_streams())) {
            allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_Ethernet_Streams);
        }
    }

    if (attributes.get_forking_factor() > 1) {
        if (!this->has_extra_streams_available(attributes.get_forking_factor() - 1)) {
            allocation_failure_reasons.push_back(BufferAllocationFailureReason::Insufficient_Extra_Streams);
        }
    }

    if (ignorable_allocation_failure_reasons.size() > 0) {
        allocation_failure_reasons.erase(
            std::remove_if(
                allocation_failure_reasons.begin(), 
                allocation_failure_reasons.end(),
                [&ignorable_allocation_failure_reasons](BufferAllocationFailureReason reason) { return ignorable_allocation_failure_reasons.find(reason) != ignorable_allocation_failure_reasons.end(); }
            ),
            allocation_failure_reasons.end()
        );
    }
    if (allocation_failure_reasons.size() > 0) {
        return allocation_failure_reasons;
    } else {
        return std::nullopt;
    }
}

std::optional<std::vector<BufferAllocationFailureReason>> HwCoreAttributes::reasons_for_buffer_allocation_failure(
    const tt::buffer_info &buffer_info, 
    const BufferAttributes &attributes,
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) const {
    return reasons_for_buffer_allocation_failure(buffer_info.allocated_size_in_bytes(), buffer_info.tile_size_in_bytes(), attributes, ignorable_allocation_failure_reasons);
}

bool HwCoreAttributes::can_add_buffer(
    const tt::buffer_info &buffer_info, 
    const BufferAttributes &attributes, 
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) const {
    return !(reasons_for_buffer_allocation_failure(buffer_info, attributes, ignorable_allocation_failure_reasons).has_value());
}

bool HwCoreAttributes::can_add_buffer(
    int size_in_tiles, 
    DataFormat data_format, 
    bool include_tile_header,
    const BufferAttributes &attributes,
    const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons
) const {
    int size_in_bytes = tt::size::get_size_of_n_tiles_in_bytes(data_format, size_in_tiles, true);
    return !(reasons_for_buffer_allocation_failure(size_in_bytes, tt::size::get_tile_size_in_bytes(data_format, include_tile_header), attributes, ignorable_allocation_failure_reasons).has_value());
}


void HwCoreAttributes::add_buffer(unique_id_t id, tt::buffer_info const& buffer_info, const BufferAttributes &attributes) {
    // TODO(snijjar): add buffer type check
    // TODO(snijjar): attribute to check for dram I/O stream limit count
    const auto size_in_bytes = buffer_info.allocated_size_in_bytes();
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->can_allocate_bytes(size_in_bytes), 
        "Tried adding buffer ", id, " of size ", size_in_bytes, 
        " bytes to core but it has insufficient L1 memory. Space should have been checked before adding buffer.");
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->has_available_relay_buffer_slot(), 
        "Tried adding buffer ", id, " to core but it has insufficient relay buffer slots. Space should have been checked before adding buffer.");
 
    this->allocate_bytes(size_in_bytes);
    // Here for now instead of inside allocate_bytes because we sometimes get legal netlists that exceed the limit for router with the
    // inputr/output buffers from the netlist, before router/net2pipe even add additional buffering. So we want to ignore to assert
    // for those buffers allocated during router construction and only make sure we don't further exceed the limit from newly added
    // relay buffers until this sizing discrepency between net2pipe/router and pipegen is resolved
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->used_l1_memory <= this->l1_memory_size, 
        "Tried adding buffer ", id, " of size ", size_in_bytes, 
        " bytes to core but it has exceeded L1 memory limit. Space should have been checked before adding buffer.");
    this->add_to_buffer_count();
    TT_ASSERT(!this->resource_constraint_asserts_enabled || this->relay_buffers.size() < HwCoreAttributes::RELAY_BUFFER_LIMIT, 
        "Tried adding buffer ", id, " to core but it has exceeded relay buffer limit. Space should have been checked before adding buffer.");

    if (attributes.has_multicast()) {
        const auto num_multicasts = attributes.get_num_multicasts();
        TT_ASSERT(!this->resource_constraint_asserts_enabled || this->has_available_multicast_stream_slots(num_multicasts), 
            "Tried adding buffer ", id, " to core but it has insufficient multicast stream slots. Space should have been checked before adding buffer."); 
        this->add_multicast_streams(num_multicasts);
    }
    if (attributes.has_input_from_dram()) {
        TT_ASSERT(!this->resource_constraint_asserts_enabled || this->has_available_input_from_dram_slot(), 
            "Tried adding buffer ", id, " to core but it has insufficient input from dram slots. Space should have been checked before adding buffer."); 
        this->add_input_from_dram();
    }
    if (attributes.has_outputs_to_dram()) {
        const auto num_outputs_to_dram = attributes.get_num_outputs_to_dram();
        TT_ASSERT(!this->resource_constraint_asserts_enabled || this->has_available_output_to_dram_slots(num_outputs_to_dram), 
            "Tried adding buffer ", id, " to core but it has insufficient output to dram slots. Space should have been checked before adding buffer."); 
        this->add_outputs_to_dram(num_outputs_to_dram);
    }
    if (attributes.has_ethernet_io_streams()) {
        const auto num_ethernet_streams = attributes.get_num_ethernet_io_streams();
        TT_ASSERT(!this->resource_constraint_asserts_enabled || this->max_num_eth_streams_total == 0 || this->has_available_ethernet_stream_slots(num_ethernet_streams), 
            "Tried adding buffer ", id, " to core but it has insufficient ethernet stream slots. Space should have been checked before adding buffer.");
        this->add_ethernet_streams(num_ethernet_streams);
    }
    // if (buffer_info.type() == RouterBufferType::Relay) {
    //     this->used_extra_streams++;
    // }

    switch (buffer_info.type()) {
        case RouterBufferType::Relay: this->relay_buffers.push_back(id); break;
        case RouterBufferType::Input: this->input_buffers.push_back(id); break;
        case RouterBufferType::Output: this->output_buffers.push_back(id); break;
        case RouterBufferType::Intermediate:
        case RouterBufferType::DRAM:
        case RouterBufferType::PrologInter:
            // don't need to explicitly track these yet
            break;
        default: TT_ASSERT(false, "Unknown buffer type");
    };

    auto tile_size_in_bytes = buffer_info.tile_size_in_bytes();
    bool need_new_tile_size_buffer = this->used_tile_sizes.find(tile_size_in_bytes) == this->used_tile_sizes.end();
    if (need_new_tile_size_buffer) {
        int extra_bytes_needed = this->extra_bytes_required_from_buffer_tile_size(tile_size_in_bytes);
        TT_ASSERT(this->used_tile_sizes.size() == 0 || extra_bytes_needed > 0);
        this->allocate_bytes(extra_bytes_needed);
        this->used_tile_sizes.insert(tile_size_in_bytes);
    }
}


// using HwCoreAttributesMap = std::unordered_map<tt_cxy_pair, HwCoreAttributes>;

// HwCoreAttributesMap generate_initial_core_resource_usage_per_temporal_epoch();

}; // namespace router


std::ostream &std::operator<<(std::ostream &s, router::HwCoreResourceUsageSnapshot const& snapshot) {
  s << "{";
  for (auto const& [type, amount] : snapshot.resources_used) {
    switch (type) {
      case tt::ResourceUsageType::L1_MEMORY: s << "L1_MEMORY"; break;
      case tt::ResourceUsageType::INPUT_FROM_DRAM: s << "INPUT_FROM_DRAM"; break;
      case tt::ResourceUsageType::OUTPUT_TO_DRAM: s << "OUTPUT_TO_DRAM"; break;
      case tt::ResourceUsageType::MULTICAST_STREAMS: s << "MULTICAST_STREAMS"; break;
      case tt::ResourceUsageType::ETHERNET_STREAMS: s << "ETHERNET_STREAMS"; break;
      case tt::ResourceUsageType::EXTRA_STREAMS: s << "EXTRA_STREAMS"; break;
      case tt::ResourceUsageType::ACTIVE_DRAM_QUEUES: s << "ACTIVE_DRAM_QUEUES"; break;
    }
    s << ": " << amount << ", ";
  }
  s << "}";

  return s;
}
