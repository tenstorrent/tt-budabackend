// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace pipegen2
{
// Level of detail with which perf info will be dumped.
enum class PerfDumpLevel
{
    kLevel0 = 0,
    kLevel1 = 1,
    kLevel2 = 2
};

// Mode of performance info tracing.
enum class PerfDumpMode
{
    kDisabled = 0,
    kDramSpillPerfTrace = 1,
    kL1PerfTrace = 2,
    kHostSpillPerfTrace = 3
};
}  // namespace pipegen2