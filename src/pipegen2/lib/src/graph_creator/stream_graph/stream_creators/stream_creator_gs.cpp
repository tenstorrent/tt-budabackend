// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_creators/stream_creator_gs.h"

namespace pipegen2
{
void StreamCreatorGS::configure_unpacker_stream_receiver_params(StreamConfig& base_stream_config)
{
    base_stream_config.set_local_receiver(true);
    base_stream_config.set_local_receiver_tile_clearing(true);
}
}  // namespace pipegen2