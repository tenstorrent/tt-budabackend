// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "router.hpp"

#include "device/tt_xy_pair.h"

#include <unordered_set>
#include <vector>

namespace router {
void validate_all_buffers_and_pipes_assigned_to_cores(const router::Router &router);
void validate_all_buffers_assigned_to_streams(const router::Router &router);

std::unordered_set<int> core_locations_to_rows(const std::vector<tt_cxy_pair> &core_locations);
bool is_forked_dram_buffer(const Router &router, unique_id_t buf_id);
std::vector<unique_id_t> get_forked_dram_buffers(const Router &router);
std::vector<unique_id_t> get_forked_buffers(const Router &router);

DataFormat get_buffer_data_format(const Router &router, unique_id_t buffer_id);

tt_cxy_pair choose_location_for_buffer(const Router &router, chip_id_t chip, unique_id_t buffer_id, const std::vector<tt_cxy_pair> &preferred_locations, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {});
tt_cxy_pair choose_location_for_buffer(const Router &router, chip_id_t chip, const std::vector<tt_cxy_pair> &preferred_locations, unique_id_t buffer_id, const std::unordered_set<BufferAllocationFailureReason> &ignorable_allocation_failure_reasons = {});
tt_cxy_pair choose_location_for_gather_buffer(const Router &router, chip_id_t chip, const std::vector<tt_cxy_pair> &input_buf_locations, const tt_cxy_pair &dest_location, unique_id_t buffer_id);
tt_cxy_pair choose_location_for_multicast_buffer(const Router &router, chip_id_t chip, const tt_cxy_pair &src_location, const std::vector<tt_cxy_pair> &dest_buf_locations, unique_id_t buffer_id);


tt::buffer_info create_relay_buffer_info(int stream_id, int num_allocated_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims);
tt::buffer_info create_relay_buffer_info(int num_allocated_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims);
router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, std::variant<tt_cxy_pair,chip_id_t> location);
router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, chip_id_t chip);
router_buffer_info_t create_mutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location);
router_buffer_info_t create_immutable_relay_buffer(int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location);
router_buffer_info_t create_mutable_relay_buffer_with_explicit_stream(int stream_id, int num_tiles, int scatter_gather_num_tiles, int num_epoch_tiles, DataFormat data_format, tt_xy_pair const& tile_dims, const tt_cxy_pair &location);

std::unordered_map<tt_cxy_pair, int> compute_l1_use_from_buffers(const Router &router);
std::unordered_map<tt_cxy_pair, std::vector<unique_id_t>> get_cores_used_by_buffers(const Router &router);


}; // namespace router