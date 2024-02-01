// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/base_stream_destination.h"

namespace pipegen2
{
    BaseStreamDestination* BaseStreamDestination::clone() const
    {
        BaseStreamDestination* destination = clone_internal();

        return destination;
    }
}