// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe.hpp"
#include "grid.hpp"

namespace analyzer {

std::vector<Pipe*> routeData(Grid* src, Grid* dst);

}