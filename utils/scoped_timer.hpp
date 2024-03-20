// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef TT_ENABLE_CODE_TIMERS
#include <chrono>
#include <string>

#include "logger.hpp"

// Internal namespace since we should never use this struct directly, instead we should use macros because they are
// compiled out when TT_ENABLE_CODE_TIMERS=0.
namespace scoped_timer_internal {
template <typename TimeUnit>
struct ScopedTimer {
    using Clock = std::chrono::high_resolution_clock;
    using TimeInstant = std::chrono::time_point<Clock, std::chrono::nanoseconds>;
    using Duration = std::chrono::duration<std::uint64_t, typename TimeUnit::period>;

    static std::string time_unit_to_string() {
        if constexpr (std::is_same_v<TimeUnit, std::chrono::milliseconds>) {
            return "ms";
        } else if constexpr (std::is_same_v<TimeUnit, std::chrono::microseconds>) {
            return "µs";
        } else if constexpr (std::is_same_v<TimeUnit, std::chrono::seconds>) {
            return "s";
        } else {
            return "time_unit";
        }
    }

    ScopedTimer(std::string name_) : name(name_) {
        this->start = std::chrono::time_point_cast<std::chrono::nanoseconds>(Clock::now());
    }

    ~ScopedTimer() {
        static bool profile = std::getenv("TT_BACKEND_PROFILER") ? atoi(std::getenv("TT_BACKEND_PROFILER")) : false;
        if (profile) {
            TimeInstant end = std::chrono::time_point_cast<std::chrono::nanoseconds>(Clock::now());
            Duration elapsed = std::chrono::duration_cast<TimeUnit>(end - this->start);
            std::string info = fmt::format(fmt::fg(fmt::color::green), "{}", this->name);
            log_profile("{} -- elapsed: {}{}", info, elapsed.count(), time_unit_to_string());
        }
    }

    std::string name;
    TimeInstant start;
};
} // namespace scoped_timer_internal

#define DURATION_TYPE(timeunit) std::chrono::timeunit
// Runs timer in given time units for the current scope.
#define PROFILE_SCOPE(timeunit) scoped_timer_internal::ScopedTimer<DURATION_TYPE(timeunit)> timer(__PRETTY_FUNCTION__)
// Runs timer in milliseconds for the current scope.
#define PROFILE_SCOPE_MS() scoped_timer_internal::ScopedTimer<DURATION_TYPE(milliseconds)> timer(__PRETTY_FUNCTION__)
#else
// Runs timer in given time units for the current scope.
#define PROFILE_SCOPE(timeunit) ((void)0)
// Runs timer in milliseconds for the current scope.
#define PROFILE_SCOPE_MS() ((void)0)
#endif