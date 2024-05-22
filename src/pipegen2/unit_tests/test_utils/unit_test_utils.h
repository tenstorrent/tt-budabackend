// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>
#include <vector>

// clang-format off
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "device/tt_arch_types.h"
// clang-format on

namespace pipegen2
{
namespace unit_test_utils
{

// Verifies that the exception's error message matches the given regex.
void verify_exception_message_regex(const std::exception& ex, const std::string& expected_error_message_regex);

// Performs Action which is expected to throw Exception and then calls VerifyExceptionCallback for that exception to
// verify the message and the relevant exception fields.
template <typename Exception, typename Action, typename VerifyExceptionCallback>
std::enable_if_t<std::is_base_of_v<std::exception, Exception>, void> verify_throws_proper_exception(
    const Action& action, const VerifyExceptionCallback& verify_exception_callback)
{
    EXPECT_THROW(
        {
            try
            {
                action();
            }
            catch (const Exception& ex)
            {
                verify_exception_callback(ex);
                throw ex;
            }
        },
        Exception);
}

// Performs Action which is expected to throw Exception with expected_error_message_regex.
template <typename Exception, typename Action>
void verify_throws_exception_with_message(const Action& action, const std::string& expected_error_message_regex)
{
    verify_throws_proper_exception<Exception>(
        action, [&](const Exception& ex) { verify_exception_message_regex(ex, expected_error_message_regex); });
}

// Performs Action which is expected to throw std::exception with assert_message_regex.
template <typename Action>
void verify_log_assert(const Action& action, const std::string& assert_message_regex)
{
    verify_throws_exception_with_message<std::exception>(action, assert_message_regex);
}

// Checks if function() call does not throw and if it's return value is equal to expected_value.
template <typename ReturnValueType>
void verify_no_throw_and_return_value_eq(
    const std::function<ReturnValueType()>& function, const ReturnValueType& expected_value)
{
    ReturnValueType return_value;
    EXPECT_NO_THROW(return_value = function());
    EXPECT_EQ(return_value, expected_value);
}

// Returns tt::ARCH object for which project was built.
tt::ARCH get_build_arch();

// Converts a list to string
template <typename VectorType>
std::string convert_to_string(const std::vector<VectorType>& list_of_integers)
{
    std::ostringstream oss;
    oss << "[";

    for (int i = 0; i < list_of_integers.size(); i++)
    {
        oss << std::to_string(list_of_integers[i]);
        if (i != list_of_integers.size() - 1)
        {
            oss << ", ";
        }
    }

    oss << "]";
    return oss.str();
}

}  // namespace unit_test_utils
}  // namespace pipegen2