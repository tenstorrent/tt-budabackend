// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/endpoint_stream_source.h"

namespace pipegen2
{
    BaseStreamSource* EndpointStreamSource::clone_internal() const
    {
        EndpointStreamSource* source = new EndpointStreamSource();
        source->m_ptrs_not_zero = m_ptrs_not_zero;

        return source;
    }
}