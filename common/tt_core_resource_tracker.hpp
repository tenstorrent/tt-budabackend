// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

// #include "common/base.hpp"
#include "netlist/tt_backend_api_types.hpp" 
#include "device/tt_xy_pair.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tt {
struct resource_usage_entry_t {
  std::string resource_name;
  int used;
  int limit;
};


enum ResourceUsageType {
  L1_MEMORY,
  INPUT_FROM_DRAM,
  OUTPUT_TO_DRAM,
  MULTICAST_STREAMS,
  ETHERNET_STREAMS,
  EXTRA_STREAMS,
  ACTIVE_DRAM_QUEUES,
};

struct CoreResources {

    const int l1_memory_size;
    const int max_input_from_dram_streams;
    const int max_output_to_dram_streams;
    const int max_mcast_streams_per_core;
    const int max_num_non_mcast_eth_streams; // MAX_NUM_NON_MCAST_ETH_STREAMS
    const int extra_tile_size_header_buffer_size; 

    // "Extra" streams are just those that are under the miscellaneous bucket in that they don't fit under the categories
    // of 1) Input 2) Output 3) Intermediate or 4)Prolog buffers. 
    // Extra streams include
    //   - relay (buffer) streams
    //   - DRAM read and write streams
    //   - forked buffer streams (excluding the first stream)
    //     - if a buffer forks 4 ways, it will use 3 "extra" streams
    // The number of extra streams used to map to the streams that would be generated in the blob yaml
    //   - for example, a relay buffer that reads from DRAM and only forwards to a single consumer would
    //     use only one "extra" stream in total, since the same stream both reads from DRAM and implements the relay
    const int max_extra_streams;
    const int max_active_dram_queues;
    const int max_num_eth_streams_total;

    int used_l1_memory = 0;
    int used_input_from_dram_slots = 0;
    int used_output_to_dram_slots = 0;
    int used_io_buffer_count = 0;
    int used_multicast_non_eth_stream_slots = 0;
    int used_ethernet_stream_slots = 0;
    int used_extra_streams = 0;
    int used_active_dram_streams = 0;

    std::unordered_set<std::uint32_t> used_tile_sizes = {};

    

  protected:
    uint32_t extra_bytes_required_from_buffer_tile_size(int tile_size_in_bytes) const;
    void validate_stream_counts_not_negative();

  public:
    void add_input_from_dram();
    void add_outputs_to_dram(int num_outputs);
    void add_multicast_streams(int num_streams);
    void add_ethernet_streams(int num_streams);

    void adjust_input_from_dram(int input_from_dram_diff) { this->used_input_from_dram_slots += input_from_dram_diff; }
    void adjust_outputs_to_dram(int num_outputs_diff) { this->used_output_to_dram_slots += num_outputs_diff; }
    void adjust_multicast_streams(int num_streams_diff) { this->used_multicast_non_eth_stream_slots += num_streams_diff; }
    void adjust_ethernet_streams(int num_streams_diff) { this->used_ethernet_stream_slots += num_streams_diff; }

    int used_mcast_streams_by_ethernet() const;


   bool any_resource_limits_exceeded_with_ignore_list(const std::unordered_set<ResourceUsageType> &ignored) const;
   std::vector<resource_usage_entry_t> get_exceeded_resources_with_ignore_list(
       const std::unordered_set<ResourceUsageType> &ignored) const;
   bool tile_size_is_used(uint32_t tile_size_in_bytes);
   bool data_format_tile_size_is_used(DataFormat df, bool include_tile_header);

   int get_num_allocated_bytes() const { return this->used_l1_memory; }
   int total_l1_size() const { return this->l1_memory_size; }
   bool can_allocate_bytes(int num_bytes) const;
   bool has_available_multicast_stream_slots(int num_streams = 1) const;
   bool has_available_output_to_dram_slots(int num_outputs = 1) const;
   bool has_available_input_from_dram_slot() const;
   int number_of_available_input_from_dram_slots() const;
   bool has_available_ethernet_stream_slots(int num_streams) const;
   bool has_extra_streams_available(int num = 1) const;
   bool has_available_active_dram_queue_slots(int num = 1) const {
       return this->max_active_dram_queues - this->get_used_active_dram_queues() >= num; }


    int get_used_extra_streams() const { int used = this->used_extra_streams; /*log_assert(used <= max_extra_streams, "Exceeded maximum 'extra' streams allowed");*/ return used; }
    int number_of_available_extra_streams() const { int available = this->max_extra_streams - get_used_extra_streams(); /*log_assert(available >= 0, "Exceeded max_extra_streams");*/ return available; }
    int number_of_available_active_dram_queues() const { int available = this->max_active_dram_queues - get_used_active_dram_queues(); /*log_assert(available >= 0, "Exceeded max active DRAM queues");*/ return available; }
    int get_used_input_from_dram_streams() const { return this->used_input_from_dram_slots; }
    int get_used_output_to_dram_streams() const { return this->used_output_to_dram_slots; }
    int get_buffer_count() const;
    int get_used_ethernet_stream_count() const;
    int get_used_active_dram_queues() const { return this->used_active_dram_streams; }
    int get_max_extra_streams() const { return this->max_extra_streams; }
    int get_max_input_from_dram_streams() const { return this->max_input_from_dram_streams;}
    int get_max_output_to_dram_streams() const { return this->max_output_to_dram_streams; }
    int get_max_mcast_streams_per_core() const { return this->max_mcast_streams_per_core; }

    int available_l1_bytes() const;
    constexpr int get_max_num_ethernet_streams_total() const { return this->max_num_eth_streams_total; }

    /* Includes mcast streams used by ethernet */
    int get_used_mcast_stream_count() const;

    void allocate_bytes(int num_bytes);
};

class ClusterResourceModel {
   public:
    ClusterResourceModel()=delete;
    ClusterResourceModel(ClusterResourceModel&)=delete;
    ClusterResourceModel(ClusterResourceModel&&)=delete;
    ~ClusterResourceModel()=default;
    ClusterResourceModel(std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> &&core_attributes);
    CoreResources *get_core_resources(const tt_cxy_pair &location);

    const CoreResources *get_core_resources(const tt_cxy_pair &location) const;

    bool any_resource_limits_exceeded_with_ignore_list(const std::unordered_set<ResourceUsageType> &ignored) const;

   private:
    std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> chip_hw_cores;
};

}; // namespace tt