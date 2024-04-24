// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/tt_core_resource_tracker.hpp"
#include "common/size_lib.hpp"
#include "device/tt_xy_pair.h"
#include "utils/logger.hpp"

#include <type_traits>
#include <memory>

namespace tt {

ClusterResourceModel::ClusterResourceModel(
    std::unordered_map<tt_cxy_pair, std::unique_ptr<CoreResources>> &&core_attributes) :
    chip_hw_cores(std::move(core_attributes)) {
    log_assert(
        chip_hw_cores.size() > 0,
        "ClusterResourceModel was constructed with no cores. This implies there is an issue with converying "
        "architecture information to the router");
}

CoreResources* ClusterResourceModel::get_core_resources(const tt_cxy_pair &location) {
    log_assert(
        this->chip_hw_cores.find(location) != this->chip_hw_cores.end(),
        "Core (c={}, y={}, x={}) not available in current temporal epoch, maybe missing empty graphs for this chip",
        location.chip, location.y, location.x);
    return this->chip_hw_cores.at(location).get();
}
const CoreResources* ClusterResourceModel::get_core_resources(const tt_cxy_pair &location) const {
    log_assert(
        this->chip_hw_cores.find(location) != this->chip_hw_cores.end(),
        "Core (c={}, y={}, x={}) not available in current temporal epoch, maybe missing empty graphs for this chip",
        location.chip, location.y, location.x);
    return this->chip_hw_cores.at(location).get();
}

bool ClusterResourceModel::any_resource_limits_exceeded_with_ignore_list(
    const std::unordered_set<ResourceUsageType> &ignored) const {
    bool any_exceeded = std::any_of(
        this->chip_hw_cores.begin(),
        this->chip_hw_cores.end(),
        [&ignored](const auto &core) {
            return core.second->any_resource_limits_exceeded_with_ignore_list(ignored);
        });
    return any_exceeded;
}


int CoreResources::available_l1_bytes() const { 
    int bytes_available = total_l1_size() - get_num_allocated_bytes();
    // log_assert(!this->resource_constraint_asserts_enabled || bytes_available >= 0, "Should have a non-negative number of l1 bytes available");
    return bytes_available;
}

bool CoreResources::has_extra_streams_available(int num) const {
    return this->get_used_extra_streams() + num <= this->max_extra_streams;
}


bool CoreResources::any_resource_limits_exceeded_with_ignore_list(const std::unordered_set<ResourceUsageType> &ignored) const {
    auto check_enabled = [&](ResourceUsageType type) {
        return ignored.find(type) == ignored.end();
    };
    return (check_enabled(ResourceUsageType::EXTRA_STREAMS) && this->get_used_extra_streams() > this->max_extra_streams) ||
        (check_enabled(ResourceUsageType::L1_MEMORY) && this->used_l1_memory > this->l1_memory_size) ||
        (check_enabled(ResourceUsageType::INPUT_FROM_DRAM) && this->used_input_from_dram_slots > this->max_input_from_dram_streams) ||
        (check_enabled(ResourceUsageType::OUTPUT_TO_DRAM) && this->used_output_to_dram_slots > this->max_output_to_dram_streams) ||
        (check_enabled(ResourceUsageType::ETHERNET_STREAMS) && this->get_used_ethernet_stream_count() > this->max_num_eth_streams_total) ||
        (check_enabled(ResourceUsageType::MULTICAST_STREAMS) && this->used_multicast_non_eth_stream_slots > this->max_mcast_streams_per_core);
}


uint32_t CoreResources::extra_bytes_required_from_buffer_tile_size(int tile_size_in_bytes) const {
    bool need_new_tile_size_buffer = this->used_tile_sizes.find(tile_size_in_bytes) == this->used_tile_sizes.end();
    if (need_new_tile_size_buffer && this->used_tile_sizes.size() >= 1) {
        return this->extra_tile_size_header_buffer_size;// MAX_TILES_MSG_INFO_BUF_PER_PHASE * TILE_HEADER_SIZE_BYTES;
    } else {
        return 0;
    }
}

void CoreResources::validate_stream_counts_not_negative() {
    // log_assert(!this->resource_constraint_asserts_enabled || get_used_ethernet_stream_count() >= 0, "Non-negative number of ethernet streams should be used");
    // log_assert(!this->resource_constraint_asserts_enabled || get_used_output_to_dram_streams() >= 0, "Non-negative number of output dram streams should be used");
    // log_assert(!this->resource_constraint_asserts_enabled || get_used_input_from_dram_streams() >= 0, "Non-negative number of input dram streams should be used");
}


bool CoreResources::has_available_multicast_stream_slots(int num_streams) const {
    return this->used_multicast_non_eth_stream_slots + used_mcast_streams_by_ethernet() + num_streams <= this->max_mcast_streams_per_core && has_extra_streams_available(num_streams);
}

bool CoreResources::has_available_ethernet_stream_slots(int num_streams) const {
    return (this->used_ethernet_stream_slots + this->used_multicast_non_eth_stream_slots + num_streams) <= this->max_num_eth_streams_total;
}

int CoreResources::get_used_ethernet_stream_count() const {
    return this->used_ethernet_stream_slots;
}

int CoreResources::get_used_mcast_stream_count() const {
    return this->used_multicast_non_eth_stream_slots + this->used_mcast_streams_by_ethernet();
}


void CoreResources::add_multicast_streams(int num_streams) {
    this->used_multicast_non_eth_stream_slots += num_streams;
}


bool CoreResources::tile_size_is_used(uint32_t tile_size_in_bytes) {
    return this->used_tile_sizes.find(tile_size_in_bytes) != this->used_tile_sizes.end();
}

bool CoreResources::data_format_tile_size_is_used(DataFormat df, bool include_tile_header) {
    return this->tile_size_is_used(tt::size::get_tile_size_in_bytes(df, include_tile_header));
}

bool CoreResources::can_allocate_bytes(int num_bytes) const {
    return this->used_l1_memory + num_bytes <= this->l1_memory_size;
}

bool CoreResources::has_available_output_to_dram_slots(int num_outputs) const {
    return this->used_output_to_dram_slots + num_outputs <= this->max_output_to_dram_streams;
}


int CoreResources::number_of_available_input_from_dram_slots() const {
    int num_available = this->max_input_from_dram_streams - this->used_input_from_dram_slots;
    // log_assert(!this->resource_constraint_asserts_enabled || num_available >= 0, 
    //     "Number of available input from dram slots is negative. Too many were allocated.");
    return num_available;
}

bool CoreResources::has_available_input_from_dram_slot() const {
    return this->used_input_from_dram_slots < this->max_input_from_dram_streams;
}

int CoreResources::get_buffer_count() const {
    return this->used_io_buffer_count;
}

void CoreResources::add_input_from_dram() {
    this->used_input_from_dram_slots += 1;
}

void CoreResources::add_outputs_to_dram(int num_outputs) {
    this->used_output_to_dram_slots += num_outputs;
}

void CoreResources::allocate_bytes(int num_bytes) {
    this->used_l1_memory += num_bytes;
    // log_assert(!this->resource_constraint_asserts_enabled || this->used_l1_memory <= total_l1_size(), "Expected used L1 memory to be less than total L1 size");
}

void CoreResources::add_ethernet_streams(int num_ethernet_streams) {
    this->used_ethernet_stream_slots += num_ethernet_streams;
}

int CoreResources::used_mcast_streams_by_ethernet() const {
    return std::max<int>(0, this->used_ethernet_stream_slots - this->max_num_non_mcast_eth_streams);
}

std::vector<resource_usage_entry_t> CoreResources::get_exceeded_resources_with_ignore_list(const std::unordered_set<ResourceUsageType> &ignored) const {
    auto check_enabled = [&ignored](ResourceUsageType type) {
        return ignored.find(type) == ignored.end();
    };
    std::vector<resource_usage_entry_t> exceeded_resources = {};
    if (check_enabled(ResourceUsageType::EXTRA_STREAMS) && this->get_used_extra_streams() > this->max_extra_streams) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="extra streams", .used=this->get_used_extra_streams(), .limit=this->max_extra_streams});
    }
    if (check_enabled(ResourceUsageType::L1_MEMORY) && this->used_l1_memory > this->l1_memory_size) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="l1 memory", .used=this->used_l1_memory, .limit=this->l1_memory_size});
    }
    if (check_enabled(ResourceUsageType::INPUT_FROM_DRAM) && this->used_input_from_dram_slots > this->max_input_from_dram_streams) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="input from dram slots", .used=this->used_input_from_dram_slots, .limit=this->max_input_from_dram_streams});
    }
    if (check_enabled(ResourceUsageType::OUTPUT_TO_DRAM) && this->used_output_to_dram_slots > this->max_output_to_dram_streams) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="output to dram slots", .used=this->used_output_to_dram_slots, .limit=this->max_output_to_dram_streams});
    }
    if (check_enabled(ResourceUsageType::ETHERNET_STREAMS) && this->get_used_ethernet_stream_count() > this->max_num_eth_streams_total) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="ethernet streams", .used=this->get_used_ethernet_stream_count(), .limit=this->max_num_eth_streams_total});
    }
    if (check_enabled(ResourceUsageType::MULTICAST_STREAMS) && this->used_multicast_non_eth_stream_slots > this->max_mcast_streams_per_core) {
        exceeded_resources.push_back(resource_usage_entry_t{.resource_name="multicast streams", .used=this->used_multicast_non_eth_stream_slots, .limit=this->max_mcast_streams_per_core});
    }
    return exceeded_resources;
}

};  // namespace tt
