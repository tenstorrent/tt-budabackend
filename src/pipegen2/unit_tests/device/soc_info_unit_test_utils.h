// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/soc_info.h"
#include "model/typedefs.h"
// clang-format on

// Helper function used to compare equality of two vectors, disregarding chip attribute in the first one.
void expect_equivalent_vectors(
    const std::vector<tt_cxy_pair>& vector_cxy_pairs, const std::vector<tt_xy_pair>& vector_xy_pairs);

// Helper function used to compare equality of two nested vectors, disregarding chip attribute in the first one.
void expect_equivalent_vectors(
    const std::vector<std::vector<tt_cxy_pair>>& nested_vector_cxy_pairs,
    const std::vector<std::vector<tt_xy_pair>>& nested_vector_xy_pairs);

// Helper function used to test convert_logical_to_physical_worker_core_coords.
void verify_convert_logical_to_physical_worker_core_coords(
    const pipegen2::SoCInfo* soc_info,
    std::vector<tt_cxy_pair>& logical_locations,
    std::vector<tt_cxy_pair>& expected_physical_coordinates);

// Helper function used to test get_host_noc_address_through_pcie.
void verify_get_host_noc_address_through_pcie(
    const pipegen2::SoCInfo* soc_info,
    const tt_xy_pair& expected_first_pcie_core,
    const pipegen2::ChipId chip_id,
    const unsigned long additional_offset = 0);

// Helper function used to test get_local_pcie_buffer_address.
void verify_get_local_pcie_buffer_address(
    const pipegen2::SoCInfo* soc_info,
    const tt_cxy_pair& worker_core_location,
    const std::uint64_t expected_local_pcie_buffer_address_with_pcie_downstream,
    const std::uint64_t expected_local_pcie_buffer_address_without_pcie_downstream);