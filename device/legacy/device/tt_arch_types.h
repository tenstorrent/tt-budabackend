// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace tt {
/**
 * @brief ARCH Enums
 */
enum class ARCH {
    JAWBRIDGE = 0,
    GRAYSKULL = 1,
    WORMHOLE = 2,
    WORMHOLE_B0 = 3,
    Invalid = 0xFF,
};
}