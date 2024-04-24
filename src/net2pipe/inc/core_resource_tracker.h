// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "router_types.h"
#include "buffer_info.h"

#include "net2pipe_constants.h"
#include "common/tt_core_resource_tracker.hpp"
#include "tt_backend_api_types.hpp"

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>


namespace router {
class Router;

/* Book-keeps various resources on a given core on the noc. 
 * Current resources include:
 * - L1 space
 * - Stream counts
 * - DRAM input buffer streams
 * - DRAM output buffer streams
 * 
 * Future resources may include:
 * - inbound and outbound noc bandwidth usage
 * - multicast streams
 */
class HwCoreAttributes : public tt::CoreResources {
    friend Router;
  private:
    // Maximum number of ethernet streams that are available before we need to start spilling over into the mcast streams
    // For ethernet cores, there are 8 total streams that can transmit over ethernet. 4 are ethernet streams, but the other
    // 4 overlap with the streams available for multicast. Therefore, if you had 8 ethernet streams in use on a core, you
    // couldn't have any multicasting streams on that core. 2 multicasting streams on a core would leave a maximum of 6
    // streams available for ethernet

    static constexpr int MAX_EXTRA_STREAMS = MAX_EXTRA_STREAMS_PER_CORE;

    // The number of pipes (gather, multicast, or gather-multicast) that are resident on this pipe
    int resident_gather_multicast_pipes;
    std::vector<std::pair<pipe_segment_hash_t, int>> resident_gather_pipe_input_counts;
    std::vector<pipe_segment_hash_t> resident_multicast_pipes;
    std::unordered_map<pipe_segment_hash_t, int> pipe_segment_instance_counts;

    std::vector<router::unique_id_t> relay_buffers;
    std::vector<router::unique_id_t> input_buffers;
    std::vector<router::unique_id_t> output_buffers;
    std::vector<router::unique_id_t> intermediate_buffers;

    bool resource_constraint_asserts_enabled;
    const int max_num_gather_pipes_before_phase_based_gather;
    std::unordered_set<uint32_t> tile_sizes_used;

    void increment_resident_gather_multicast_pipe_count() { this->resident_gather_multicast_pipes++; }
    void decrement_resident_gather_multicast_pipe_count() { this->resident_gather_multicast_pipes--; }

  public:

    HwCoreAttributes(
        int l1_size, 
        int max_dram_input_slots, 
        int max_dram_output_slots, 
        bool supports_eth_links,
        int max_num_gather_pipes_before_phase_based_gather);
    HwCoreAttributes(HwCoreAttributes const&)=default;

    bool has_available_relay_buffer_slot() const;

    const std::vector<unique_id_t> get_input_buffers() const { return input_buffers; }
    const std::vector<router::unique_id_t> &get_relay_buffers() const { return relay_buffers; };
    const std::vector<router::unique_id_t> &get_output_buffers() const { return output_buffers; };
    const std::vector<router::unique_id_t> &get_intermediate_buffers() const { return intermediate_buffers; };

    // TODO(snijjar): Remove this function and mark the HwCoreAttributes
    int get_relay_buffer_count() const { return this->relay_buffers.size(); }
    void remove_output_buffer(unique_id_t buffer_id);
    void remove_input_buffer(unique_id_t buffer_id);
    void remove_relay_buffer(unique_id_t buffer_id);
    int next_available_relay_buffer_stream() const;
    bool can_add_buffer(const tt::buffer_info &buffer_info, const BufferAttributes &attributes, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {}) const;
    bool can_add_buffer(int size_in_tiles, tt::DataFormat data_format, bool include_tile_header, const BufferAttributes &attributes, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {}) const;
    std::optional<std::vector<router::BufferAllocationFailureReason>> reasons_for_buffer_allocation_failure(const tt::buffer_info &buffer_info, const BufferAttributes &attributes, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {}) const;
    std::optional<std::vector<router::BufferAllocationFailureReason>> reasons_for_buffer_allocation_failure(int buffer_size_in_bytes, int tile_size_in_bytes, const BufferAttributes &attributes, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {}) const;
    void add_buffer(router::unique_id_t id, const tt::buffer_info &buffer_info, const BufferAttributes &attributes);

    static constexpr int RELAY_BUFFER_STREAM_FIRST = ::RELAY_BUFFER_STREAM_FIRST;
    static constexpr int RELAY_BUFFER_STREAM_LAST = ::RELAY_BUFFER_STREAM_LAST;
    static constexpr int RELAY_BUFFER_LIMIT = RELAY_BUFFER_STREAM_LAST - RELAY_BUFFER_STREAM_FIRST;
    static constexpr int NUM_BUFFER_STREAMS = ::NUM_BUFFER_STREAMS;
    
    int get_number_of_resident_gather_multicast_pipes() const { return this->resident_gather_multicast_pipes; }

    int count_extra_streams_from_gather_and_multicast() const;

  protected:

    // Calling count_extra_streams_from_gather_and_multicast at the start and end of each call and adjusting extra streams
    // by the delta isn't the most efficient but it's the safest for now. I can think about a better performing
    // way and API if we see this in perf dumps as an issue.
    void register_resident_gather_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id, int number_of_gather_inputs);
    void deregister_resident_gather_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id);
    void register_resident_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id);
    void deregister_resident_multicast_pipe(pipe_segment_hash_t const& pipe_segment_id);
    void register_resident_gather_pipe(pipe_segment_hash_t const& pipe_segment_id, int number_of_gather_inputs);
    void deregister_resident_gather_pipe(pipe_segment_hash_t const& pipe_segment_id);

    void add_input_buffer(unique_id_t id) { input_buffers.push_back(id); }
    void add_output_buffer(unique_id_t id) { output_buffers.push_back(id); }
    void add_intermediate_buffer(unique_id_t id) { intermediate_buffers.push_back(id); }
    void add_relay_buffer(unique_id_t id) { relay_buffers.push_back(id); }
    void remove_buffer_attributes(const BufferAttributes &attrs);
    void apply_buffer_attrs(const BufferAttributes &attrs);
    void add_to_buffer_count();
    void adjust_extra_streams(int num_extra_streams_diff) { this->used_extra_streams += num_extra_streams_diff; /*TT_ASSERT(get_used_extra_streams() <= this->max_extra_streams);*/ }

    void disable_resource_constraint_checking() { this->resource_constraint_asserts_enabled = false; }
    void enable_resource_constraint_checking() { this->resource_constraint_asserts_enabled = true; }

};

class RouterClusterResourceModel : public tt::ClusterResourceModel {
   public:
    HwCoreAttributes &get_core_attributes(const tt_cxy_pair &location) {
        auto core_attributes = reinterpret_cast<HwCoreAttributes *>(
            reinterpret_cast<ClusterResourceModel *>(this)->get_core_resources(location));
        TT_ASSERT(core_attributes != nullptr, "Casting error");
        return *core_attributes;
    }
    const HwCoreAttributes &get_core_attributes(const tt_cxy_pair &location) const {
        auto core_attributes = reinterpret_cast<HwCoreAttributes const *>(
            reinterpret_cast<ClusterResourceModel const *>(const_cast<RouterClusterResourceModel const *>(this))
                ->get_core_resources(location));
        TT_ASSERT(core_attributes != nullptr, "Casting error");
        return *core_attributes;
    }

    RouterClusterResourceModel() = delete;
    RouterClusterResourceModel(RouterClusterResourceModel&) = delete;
    RouterClusterResourceModel(RouterClusterResourceModel&&) = delete;
    RouterClusterResourceModel(
        std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> &&core_attributes) :
        tt::ClusterResourceModel(std::move(core_attributes)){};
};


struct HwCoreResourceUsageSnapshot {
  static HwCoreResourceUsageSnapshot create(HwCoreAttributes const& resource_tracker, std::vector<ResourceUsageType> const& resources) {
    auto snapshot = HwCoreResourceUsageSnapshot();
    auto const &resources_impl = resources.size() != 0 ? resources
                                                       : std::vector<ResourceUsageType>{
                                                             L1_MEMORY,
                                                             INPUT_FROM_DRAM,
                                                             OUTPUT_TO_DRAM,
                                                             MULTICAST_STREAMS,
                                                             ETHERNET_STREAMS,
                                                             EXTRA_STREAMS,
                                                         };
    for (ResourceUsageType t : resources_impl) {
      switch (t) {
          case L1_MEMORY: snapshot.resources_used[L1_MEMORY] = resource_tracker.get_num_allocated_bytes(); break;
          case INPUT_FROM_DRAM: snapshot.resources_used[INPUT_FROM_DRAM] = resource_tracker.get_used_input_from_dram_streams(); break;
          case OUTPUT_TO_DRAM: snapshot.resources_used[OUTPUT_TO_DRAM] = resource_tracker.get_used_output_to_dram_streams(); break;
          case MULTICAST_STREAMS: snapshot.resources_used[MULTICAST_STREAMS] = resource_tracker.get_used_mcast_stream_count(); break;
          case ETHERNET_STREAMS: snapshot.resources_used[ETHERNET_STREAMS] = resource_tracker.get_used_ethernet_stream_count(); break;
          case EXTRA_STREAMS: snapshot.resources_used[EXTRA_STREAMS] = resource_tracker.get_used_extra_streams(); break;
          default: TT_ASSERT(false, "Unsupported case");
      }
    }

    return snapshot;
  }

  HwCoreResourceUsageSnapshot operator-(HwCoreResourceUsageSnapshot const& rhs) const{ 
    auto result = HwCoreResourceUsageSnapshot();
    for (auto const& [type, amount] : this->resources_used) {
      result.resources_used[type] = (rhs.resources_used.find(type) != rhs.resources_used.end()) ?
        amount - rhs.resources_used.at(type) :
        amount;
    }
    for (auto const& [type, amount] : rhs.resources_used) {
      if (this->resources_used.find(type) == this->resources_used.end()) {
        result.resources_used[type] = -amount;
      }
    }

    return result;
  }

  std::unordered_map<ResourceUsageType, int> resources_used;
};


};  // namespace router

namespace std {
ostream &operator<<(ostream &s, router::HwCoreResourceUsageSnapshot const& snapshot);
}