// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "core_resources_unit_test_utils.h"

using namespace pipegen2;

void verify_illegal_resource_allocation_exception(
    const IllegalCoreResourceAllocationException& ex,
    const tt_cxy_pair& physical_core_location,
    const tt_cxy_pair& logical_core_location,
    IllegalCoreResourceAllocationException::CoreResourceType core_resource_type)
{
    EXPECT_TRUE(ex.get_physical_core_location().has_value());
    EXPECT_EQ(ex.get_physical_core_location().value(), physical_core_location);

    EXPECT_EQ(ex.get_core_resource_type(), core_resource_type);
}

void verify_out_of_core_resource_exception(
    const OutOfCoreResourcesException& ex,
    const tt_cxy_pair& physical_core_location,
    const tt_cxy_pair& logical_core_location,
    OutOfCoreResourcesException::CoreResourceType core_resource_type,
    unsigned int expected_available_core_resouce_count,
    unsigned int expected_used_core_resource_count)
{
    verify_illegal_resource_allocation_exception(ex, physical_core_location, logical_core_location, core_resource_type);

    EXPECT_EQ(ex.get_total_core_resources_available(), expected_available_core_resouce_count);
    EXPECT_EQ(ex.get_core_resources_used(), expected_used_core_resource_count);
}

void verify_no_physical_core_exception(const NoPhysicalCoreException& ex, const tt_cxy_pair& logical_core_location)
{
    EXPECT_EQ(ex.get_logical_core_location(), logical_core_location);
}