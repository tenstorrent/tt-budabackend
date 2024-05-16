// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include "device/tt_xy_pair.h"

namespace pipegen2
{

// Base interface for all pipes reading using NCRISC.
class INcriscReaderPipe
{
public:
    virtual ~INcriscReaderPipe() {}

    // Returns core locations where NCRISC streams will be employed to conduct reads for the pipe.
    virtual std::vector<tt_cxy_pair> get_ncrisc_reader_streams_locations() const = 0;
};

} // namespace pipegen2
