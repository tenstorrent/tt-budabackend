// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/local_stream_destination.h"

namespace pipegen2
{
    BaseStreamDestination* LocalStreamDestination::clone_internal() const
    {
        LocalStreamDestination* destination = new LocalStreamDestination();
        destination->m_local_receiver_tile_clearing = m_local_receiver_tile_clearing;

        return destination;
    }
}