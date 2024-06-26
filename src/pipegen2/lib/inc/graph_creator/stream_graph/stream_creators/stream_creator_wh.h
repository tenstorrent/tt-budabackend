// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "stream_creator.h"

namespace pipegen2
{
// Implements specific stream creation for Wormhole architecture.
class StreamCreatorWH : public StreamCreator
{
public:
    StreamCreatorWH() : StreamCreator() {}

protected:
    // Configures unpacker stream receiver parameters.
    virtual void configure_unpacker_stream_receiver_params(StreamConfig& base_stream_config) override;
};
}  // namespace pipegen2