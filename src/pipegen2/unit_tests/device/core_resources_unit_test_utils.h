// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "pipegen2_exceptions.h"
#include "test_utils/unit_test_utils.h"

// Verifies that the thrown IllegalCoreResourceAllocationException exception has all the expected fields configured.
void verify_illegal_resource_allocation_exception(
    const pipegen2::IllegalCoreResourceAllocationException& ex,
    const tt_cxy_pair& physical_core_location,
    const tt_cxy_pair& logical_core_location,
    pipegen2::IllegalCoreResourceAllocationException::CoreResourceType core_resource_type);

// Verifies that the thrown OutOfCoreResourcesException exception has all the expected fields configured.
void verify_out_of_core_resource_exception(
    const pipegen2::OutOfCoreResourcesException& ex,
    const tt_cxy_pair& physical_core_location,
    const tt_cxy_pair& logical_core_location,
    pipegen2::OutOfCoreResourcesException::CoreResourceType core_resource_type,
    unsigned int expected_available_core_resouce_count,
    unsigned int expected_used_core_resource_count);

// Checks if repeated function() calls will return ReturnValueType in the same order as they are in expected_values, and
// that the following call after that will throw exception of expected type and pass it to callback function to validate
// the exception fields.
template<typename ReturnValueType, typename Exception, typename Callback>
void test_function_repeated_calls_until_exception_thrown(
    const std::function<ReturnValueType()>& function,
    const std::vector<ReturnValueType>& expected_values,
    const Callback& callback)
{
    for (const ReturnValueType& expected_return_value : expected_values)
    {
        pipegen2::unit_test_utils::verify_no_throw_and_return_value_eq<ReturnValueType>(
            [&]() -> ReturnValueType
            {
                return function();
            },
            expected_return_value);
    }

    pipegen2::unit_test_utils::verify_throws_proper_exception<Exception>(function, callback);
}

// Verifies that the thrown NoPhysicalCoreException exception has all the expected fields configured.
void verify_no_physical_core_exception(
    const pipegen2::NoPhysicalCoreException& ex,
    const tt_cxy_pair& logical_core_location);