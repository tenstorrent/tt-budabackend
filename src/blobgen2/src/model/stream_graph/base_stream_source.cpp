// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/base_stream_source.h"

namespace pipegen2
{
    BaseStreamSource* BaseStreamSource::clone() const
    {
        BaseStreamSource* source = clone_internal();

        return source;
    }
}