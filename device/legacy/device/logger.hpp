// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <type_traits>
#if defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) && (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 1)
#include <pybind11/iostream.h>
#endif

#include "fmt/color.h"
#include "fmt/core.h"
#include "fmt/ostream.h"

namespace tt_device_logger {

#define LOGGER_TYPES   \
    X(Always)          \
    X(Test)            \
    X(Device)          \
    X(SiliconDriver)

enum LogType : uint32_t {
// clang-format off
#define X(a) Log ## a,
    LOGGER_TYPES
#undef X
    LogType_Count,
    // clang-format on
};
static_assert(LogType_Count < 64, "Exceeded number of log types");

#pragma GCC visibility push(hidden)
class Logger {
   public:
    static constexpr char const* type_names[LogType_Count] = {
    // clang-format off
#define X(a) #a,
      LOGGER_TYPES
#undef X
        // clang-format on
    };

    enum class Level {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Fatal = 5,

        Count,
    };

    static constexpr char const* level_names[] = {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARNING",
        "ERROR",
        "FATAL",
    };

    static_assert(
        (sizeof(level_names) / sizeof(level_names[0])) == static_cast<std::underlying_type_t<Level>>(Level::Count));

    static constexpr fmt::color level_color[] = {
        fmt::color::gray,
        fmt::color::gray,
        fmt::color::cornflower_blue,
        fmt::color::orange,
        fmt::color::red,
        fmt::color::red,
    };

    static_assert(
        (sizeof(level_color) / sizeof(level_color[0])) == static_cast<std::underlying_type_t<Level>>(Level::Count));

    // TODO: we should sink this into some common cpp file, marking inline so maybe it's per lib instead of per TU
    static inline Logger& get() {
        static Logger logger;
        return logger;
    }

    template <typename... Args>
    void log_level_type(Level level, LogType type, char const* fmt, Args&&... args) {
        if (static_cast<std::underlying_type_t<Level>>(level) < static_cast<std::underlying_type_t<Level>>(min_level))
            return;

        if ((1 << type) & mask) {
#if defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) && (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 1)
            pybind11::scoped_ostream_redirect stream(*fd);
#endif
            std::string timestamp_str = get_current_time();
            fmt::terminal_color timestamp_color = fmt::terminal_color::green;
            timestamp_str = fmt::format(fmt::fg(timestamp_color), "{}", timestamp_str);

            std::string level_str = fmt::format(
                fmt::fg(level_color[static_cast<std::underlying_type_t<Level>>(level)]) | fmt::emphasis::bold,
                "{:8}",
                level_names[static_cast<std::underlying_type_t<Level>>(level)]);

            fmt::terminal_color type_color = fmt::terminal_color::cyan;
            std::string type_str = fmt::format(fmt::fg(type_color), "{:15}", type_names[type]);

            fmt::print(*fd, "{} | {} | {} - ", timestamp_str, level_str, type_str);
            fmt::print(*fd, fmt, std::forward<Args>(args)...);
            *fd << std::endl;
        }
    }

    void flush() { *fd << std::flush; }

   private:
    Logger() {
        static char const* env = std::getenv("LOGGER_TYPES");
        if (env) {
            if (strstr(env, "All")) {
                mask = 0xFFFFFFFFFFFFFFFF;
            } else {
                std::uint32_t mask_index = 0;
                for (char const* type_name : type_names) {
                    mask |= (strstr(env, type_name) != nullptr) << mask_index;
                    mask_index++;
                }
            }
        } else {
            // For now default to all
            mask = 0xFFFFFFFFFFFFFFFF;
        }

        static char const* level_env = std::getenv("LOGGER_LEVEL");
        if (level_env) {
            std::string level_str = level_env;
            std::transform(
                level_str.begin(), level_str.end(), level_str.begin(), [](unsigned char c) { return std::toupper(c); });
            std::underlying_type_t<Level> level_index = 0;
            for (char const* level_name : level_names) {
                if (level_str == level_name)
                {
                    min_level = static_cast<Level>(level_index);
                }
                level_index++;
            }
        }

#if !defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) || (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 0)
        static char const* file_env = std::getenv("LOGGER_FILE");
        if (file_env)
        {
            log_file.open(file_env);
            if (log_file.is_open())
            {
                fd = &log_file;
            }
        }
#endif
    }

    // Returns the current timestamp in 'YYYY-MM-DD HH:MM:SS.MMM' format, e.g. 2023-06-26 11:39:38.432
    static std::string get_current_time() {
        auto now = std::chrono::system_clock::now();
        auto current_time = std::chrono::system_clock::to_time_t(now);

        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ms.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&current_time), "%F %T");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::ofstream log_file;
    std::ostream* fd = &std::cout;
    std::uint64_t mask = (1 << LogAlways);
    Level min_level = Level::Info;
};
#pragma GCC visibility pop

#ifdef DEBUG
template <typename... Args>
static void log_debug(LogType type, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Debug, type, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_debug(char const* fmt, Args&&... args) {
    log_debug(LogAlways, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_trace_(LogType type, std::string const& src_info, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Trace, type, fmt, src_info, std::forward<Args>(args)...);
}

#define log_trace(log_type, ...) \
    log_trace_(log_type, fmt::format(fmt::fg(fmt::color::green), "{}:{}", __FILE__, __LINE__), "{} - " __VA_ARGS__)
#else
template <typename... Args>
static void log_debug(LogType type, char const* fmt, Args&&... args) {}
template <typename... Args>
static void log_debug(char const* fmt, Args&&... args) {}

#define log_trace(log_type, ...) \
    log_trace_(log_type, fmt::format(fmt::fg(fmt::color::green), "{}:{}", __FILE__, __LINE__), "{} - " __VA_ARGS__)
template <typename... Args>
static void log_trace_(LogType type, std::string const& src_info, char const* fmt, Args&&... args) {}

#endif

template <typename... Args>
static void log(Logger::Level log_level, LogType type, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(log_level, type, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log(Logger::Level log_level, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(log_level, LogAlways, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_info(LogType type, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Info, type, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_info(char const* fmt, Args&&... args) {
    log_info(LogAlways, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_warning(LogType type, char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Warning, type, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_warning(char const* fmt, Args&&... args) {
    log_warning(LogAlways, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_error(char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Error, type, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_error(char const* fmt, Args&&... args) {
    log_error(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_fatal(char const* fmt, Args&&... args) {
    Logger::get().log_level_type(Logger::Level::Fatal, type, fmt, std::forward<Args>(args)...);
    Logger::get().flush();
    throw std::runtime_error(fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
static void log_fatal(char const* fmt, Args&&... args) {
    log_fatal(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
static void log_assert(bool cond, char const* fmt, Args&&... args) {
    if (not cond) {
        Logger::get().log_level_type(Logger::Level::Fatal, type, fmt, std::forward<Args>(args)...);
        Logger::get().flush();
        throw std::runtime_error(fmt::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
static void log_assert(bool cond, char const* fmt, Args&&... args) {
    log_assert(cond, fmt, std::forward<Args>(args)...);
}

#undef LOGGER_TYPES

}  // namespace tt_device_logger
