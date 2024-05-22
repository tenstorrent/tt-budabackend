// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_creators/stream_creator_wh.h"

namespace pipegen2
{
void StreamCreatorWH::configure_unpacker_stream_receiver_params(StreamConfig& base_stream_config)
{
    base_stream_config.set_receiver_endpoint(true);
}
}  // namespace pipegen2