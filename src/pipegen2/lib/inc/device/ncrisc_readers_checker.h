// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <unordered_map>

#include "device/tt_xy_pair.h"

#include "model/rational_graph/rational_graph.h"

namespace pipegen2
{

class RGBasePipe;

// Class modelling constraint that number of streams communicating with NCRISC to read data from DRAM or PCIe is limited
// to core_resources_constants::max_num_ncrisc_reading_streams per core. Each DRAM queue takes up one DRAM descriptor
// structures in NCRISC L0, thus we are limited by space. PCIe buffers are handled similarily.
class NcriscReadersChecker
{
public:
    NcriscReadersChecker() = default;

    ~NcriscReadersChecker() = default;

    // Checks stream usage per worker core.
    void check(const RationalGraph* rational_graph);

private:
    // Counts streams per worker core location that will be created from pipe which read through NCRISC.
    void count_stream_usage(
        const RGBasePipe* pipe,
        std::unordered_map<tt_cxy_pair, unsigned int>& num_ncrisc_reading_streams_per_core);
};

} // namespace pipegen2
