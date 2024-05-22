// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include "device/tt_xy_pair.h"

namespace pipegen2
{

// Base interface for all pipes writing using NCRISC.
class INcriscWriterPipe
{
public:
    virtual ~INcriscWriterPipe() {}

    // Returns core locations where NCRISC streams will be employed to conduct writes for the pipe.
    virtual std::vector<tt_cxy_pair> get_ncrisc_writer_streams_locations() const = 0;
};

}  // namespace pipegen2
