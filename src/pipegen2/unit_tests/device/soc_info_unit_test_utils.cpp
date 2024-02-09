// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/soc_info_unit_test_utils.h"

#include <gtest/gtest.h>

#include "noc_parameters.h"

void expect_equivalent_vectors(const std::vector<tt_cxy_pair>& vector_cxy_pairs,
                               const std::vector<tt_xy_pair>& vector_xy_pairs)
{
    EXPECT_EQ(vector_cxy_pairs.size(), vector_xy_pairs.size());

    for (std::size_t i = 0; i < vector_cxy_pairs.size(); ++i)
    {
        EXPECT_EQ(vector_cxy_pairs[i].x, vector_xy_pairs[i].x);
        EXPECT_EQ(vector_cxy_pairs[i].y, vector_xy_pairs[i].y);
    }
}

void expect_equivalent_vectors(const std::vector<std::vector<tt_cxy_pair>>& nested_vector_cxy_pairs,
                               const std::vector<std::vector<tt_xy_pair>>& nested_vector_xy_pairs)
{
    EXPECT_EQ(nested_vector_cxy_pairs.size(), nested_vector_xy_pairs.size());

    for (std::size_t i = 0; i < nested_vector_cxy_pairs.size(); ++i)
    {
        expect_equivalent_vectors(nested_vector_cxy_pairs[i], nested_vector_xy_pairs[i]);
    }
}

void compare_logical_and_physical_worker_core_coords_helper(
    const pipegen2::SoCInfo* soc_info,
    std::vector<tt_cxy_pair>& logical_locations,
    std::vector<tt_cxy_pair>& expected_physical_coordinates)
{
    EXPECT_EQ(logical_locations.size(), expected_physical_coordinates.size());

    //for (const tt_cxy_pair& logical_location : logical_locations)
    for (std::size_t i = 0; i < logical_locations.size(); i++)
    {
        EXPECT_EQ(
            soc_info->convert_logical_to_physical_worker_core_coords(logical_locations[i]),
            expected_physical_coordinates[i]);
    }
}

void compare_host_noc_addresses_helper(
    const pipegen2::SoCInfo* soc_info,
    const tt_xy_pair& expected_first_pcie_core,
    const pipegen2::ChipId chip_id,
    const unsigned long additional_offset)
{
    // Dummy address.
    const std::uint64_t host_pcie_buf_addr = 0x1234;

    const std::uint64_t expected_host_noc_addr_through_pcie =
        NOC_XY_ADDR(expected_first_pcie_core.x, expected_first_pcie_core.y, host_pcie_buf_addr) + additional_offset;

    EXPECT_EQ(
        soc_info->get_host_noc_address_through_pcie(host_pcie_buf_addr, chip_id),
        expected_host_noc_addr_through_pcie);
}

void compare_local_pcie_buffer_addresses_helper(
    const pipegen2::SoCInfo* soc_info,
    const tt_cxy_pair& worker_core_location,
    const std::uint64_t expected_local_pcie_buffer_address_with_pcie_downstream,
    const std::uint64_t expected_local_pcie_buffer_address_without_pcie_downstream)
{
    const std::uint64_t l1_buffer_address = 0x1234;

    EXPECT_EQ(
        soc_info->get_local_pcie_buffer_address(l1_buffer_address, worker_core_location, true),
        expected_local_pcie_buffer_address_with_pcie_downstream);

    EXPECT_EQ(
        soc_info->get_local_pcie_buffer_address(l1_buffer_address, worker_core_location, false),
        expected_local_pcie_buffer_address_without_pcie_downstream);
}