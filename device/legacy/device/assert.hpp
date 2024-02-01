// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cxxabi.h>
#include <execinfo.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <vector>

#include "device/logger.hpp"

namespace tt {
template <typename A, typename B>
struct OStreamJoin {
    OStreamJoin(A const& a, B const& b, char const* delim = " ") : a(a), b(b), delim(delim) {}
    A const& a;
    B const& b;
    char const* delim;
};

template <typename A, typename B>
std::ostream& operator<<(std::ostream& os, tt::OStreamJoin<A, B> const& join) {
    os << join.a << join.delim << join.b;
    return os;
}
}  // namespace tt

namespace tt::assert {

static std::string demangle(const char *str)
{
    size_t size = 0;
    int status = 0;
    std::string rt(256, '\0');
    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0]))
    {
        char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if (v)
        {
            std::string result(v);
            free(v);
            return result;
        }
    }
    return str;
}

// https://www.fatalerrors.org/a/backtrace-function-and-assert-assertion-macro-encapsulation.html

/**
 * @brief Get the current call stack
 * @param[out] bt Save Call Stack
 * @param[in] size Maximum number of return layers
 * @param[in] skip Skip the number of layers at the top of the stack
 */
inline std::vector<std::string> backtrace(int size = 64, int skip = 1)
{
    std::vector<std::string> bt;
    void **array = (void **)malloc((sizeof(void *) * size));
    size_t s = ::backtrace(array, size);
    char **strings = backtrace_symbols(array, s);
    if (strings == NULL)
    {
        std::cout << "backtrace_symbols error." << std::endl;
        return bt;
    }
    for (size_t i = skip; i < s; ++i)
    {
        bt.push_back(demangle(strings[i]));
    }
    free(strings);
    free(array);

    return bt;
}

/**
 * @brief String to get current stack information
 * @param[in] size Maximum number of stacks
 * @param[in] skip Skip the number of layers at the top of the stack
 * @param[in] prefix Output before stack information
 */
inline std::string backtrace_to_string(int size = 64, int skip = 2, const std::string &prefix = "")
{
    std::vector<std::string> bt = backtrace(size, skip);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i)
    {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

inline void tt_assert_message(std::ostream& os) {}

template <typename T, typename... Ts>
void tt_assert_message(std::ostream& os, T const& t, Ts const&... ts) {
    os << t << std::endl;
    tt_assert_message(os, ts...);
}

template <typename... Ts>
[[ noreturn ]] void tt_throw(char const* file, int line, const std::string& assert_type, char const* condition_str, Ts const&... messages) {
    std::stringstream trace_message_ss = {};
    trace_message_ss << assert_type << " @ " << file << ":" << line << ": " << condition_str << std::endl;
    if constexpr (sizeof...(messages) > 0) {
        trace_message_ss << "info:" << std::endl;
        tt_assert_message(trace_message_ss, messages...);
    }
    trace_message_ss << "backtrace:\n";
    trace_message_ss << tt::assert::backtrace_to_string(100, 3, " --- ");
    trace_message_ss << std::flush;
    tt_device_logger::Logger::get().flush();
    throw std::runtime_error(trace_message_ss.str());

}

template <typename... Ts>
void tt_assert(char const* file, int line, const std::string& assert_type, bool condition, char const* condition_str, Ts const&... messages) {
    if (not condition) {
        ::tt::assert::tt_throw(file, line, assert_type, condition_str, messages...);
    }
}

}  // namespace tt::assert

#define log_assert(condition, ...) ::tt::assert::tt_assert(__FILE__, __LINE__, "log_assert", (condition), #condition,      ##__VA_ARGS__)
#define TT_THROW(...)             ::tt::assert::tt_throw(__FILE__, __LINE__, "TT_THROW",     "tt::exception", ##__VA_ARGS__)
