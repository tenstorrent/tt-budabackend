// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>

namespace llk {

enum TensorTileLayout {
    COL_MAJOR,
    COL_MAJOR_ZIGZAG,
    ROW_MAJOR,
    ROW_MAJOR_ZIGZAG,
};

}  // namespace llk