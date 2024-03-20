// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "stream_creator.h"
#include "stream_creator_wh.h"

namespace pipegen2
{

// Implements specific stream creation for Blackhole architecture.
class StreamCreatorBH : public StreamCreatorWH
{
public:
    StreamCreatorBH() : StreamCreatorWH() {}
};

} // namespace pipegen2