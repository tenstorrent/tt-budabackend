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
}