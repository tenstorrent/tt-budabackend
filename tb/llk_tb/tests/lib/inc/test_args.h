// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace tests {
struct TestArgs {
    std::string device;
    std::string coverage_dir;
    std::string test_descriptor;
    std::string gitroot;
    bool dump_tensors;
    std::vector<std::string> plusargs;
    std::vector<std::string> dump_cores;
    bool regression_mode;
    bool force_seed;
    int seed;
    int num_configs_to_randomize;
    int jobs;
    bool perf_dump;
};

}  // namespace tests
