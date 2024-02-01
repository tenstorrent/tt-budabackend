// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/env_lib.hpp"
#include "gtest/gtest.h"
#include "op_model.hpp"
#include "op_params.hpp"
#include "utils/logger.hpp"

// Suppress stdout from an arbitrary func that may be too chatty
//
template <typename Func>
static inline auto quiet_call(Func func) {
    static bool verbose = testing::internal::BoolFromGTestEnv("verbose", false);  // GTEST_VERBOSE envvar
    // Only one stdout capturer can exist at a time, use lock to prevent multi-threaded access
    static std::mutex capturer_mutex;
    // Protect the capturer and capture stdout
    std::lock_guard<std::mutex> lock(capturer_mutex);
    testing::internal::CaptureStdout();
    auto ret_val = func();
    auto output = testing::internal::GetCapturedStdout();
    if (verbose) {
        std::cout << output;
    }
    return ret_val;
}

// Helper for providing test home
//
inline std::string test_path() { return tt::buda_home() + "/perf_lib/op_model/unit_tests/"; }
