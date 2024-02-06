// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <exception>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pipegen2
{
namespace unit_test_utils
{

// Performs Action which is expected to throw Exception with expected_error_message_regex.
template<typename Exception, typename Action>
std::enable_if_t<std::is_base_of_v<std::exception, Exception>, void>
verify_throws_exception_with_message(const Action& action, const std::string& expected_error_message_regex)
{
    EXPECT_THROW(
    {
        try
        {
            action();
        }
        catch (const Exception& ex)
        {
            EXPECT_THAT(ex.what(), ::testing::MatchesRegex(expected_error_message_regex));
            throw ex;
        }
    },
    Exception);
}

// Performs Action which is expected to throw std::exception with assert_message_regex.
template<typename Action>
void verify_log_assert(const Action& action, const std::string& assert_message_regex)
{
    verify_throws_exception_with_message<std::exception>(action, assert_message_regex);
}

// Checks if function() call does not throw and if it's return value is equal to expected_value.
template<typename ReturnValueType>
void verify_no_throw_and_return_value_eq(
    const std::function<ReturnValueType()>& function,
    const ReturnValueType& expected_value)
{
    ReturnValueType return_value;
    EXPECT_NO_THROW(return_value = function());
    EXPECT_EQ(return_value, expected_value);
}

// Returns whether project is built with env var ARCH_NAME=arch_name.
bool is_built_for_arch(const std::string& arch_name);

} // namespace unit_test_utils
} // namespace pipegen2