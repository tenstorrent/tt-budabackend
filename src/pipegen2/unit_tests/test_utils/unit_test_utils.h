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

// Returns set of elements as a string "{e1, e2, e3}".
template<typename T>
std::string set_to_string(const std::set<T>& s)
{
    std::stringstream result_stream;
    result_stream << "{";

    for (const T& element: s)
    {
        result_stream << element << (element == *s.rbegin() ? "" : ", ");
    }

    result_stream << "}";
    return result_stream.str();
}

// Returns whether project is built with env var ARCH_NAME=arch_name.
bool is_built_for_arch(const std::string& arch_name);

} // namespace unit_test_utils
} // namespace pipegen2