// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace pipegen2
{
    /*
        You can put here definitions for types used in multiple model graphs.
    */

    using NodeId = std::uint64_t;

    using PhaseId = unsigned long;

    using ChipId = std::size_t;

    using StreamId = unsigned int;

    // Prologue buffer types.
    enum class PrefetchType
    {
        // PRE_TM are prologue buffers that are copied to L1 before any TM is apllied to them.
        // The TM will be applied when they are sent to the unpacker.
        PRE_TM = 0,

        // POST_TM are prologue buffers where the TM is applied first and then saved in L1.
        // This is done because it can improve the bandwidth utilization when sending this to the unpacker.
        POST_TM
    };
}