// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <vector>

#include <gtest/gtest.h>

// Checks if repeated allocate_function() calls will return ValueTypes in the same order as they are in
// expected_values, and that the following call after that will throw a runtime_error.
template<typename ReturnValueType>
void test_function_repeated_calls_until_exception_thrown(
    const std::function<ReturnValueType()>& function,
    const std::vector<ReturnValueType>& expected_values)
{
    for (const ReturnValueType& expected_return_value : expected_values)
    {
        EXPECT_EQ(function(), expected_return_value);
    }

    EXPECT_THROW(function(), std::runtime_error);
}
