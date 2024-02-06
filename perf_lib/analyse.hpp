// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <fstream>
#include <vector>
#include <map>


#include "perf_descriptor.hpp"
#include "postprocess.hpp"

namespace postprocess {
    bool check_performance(const vector<InstructionInfo>& all_instructions, const perf::PerfDesc &perf_desc);
    bool check_performance_host_events(const string& output_dir, const PerfState &perf_state);
}