// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"

namespace pipegen2
{
    class RelayPipe : public DirectPipe
    {
    public:
        RelayPipe(RGPipeProperties&& rg_pipe_properties, const tt_cxy_pair& physical_location) :
            DirectPipe(RGPipeType::Relay, std::move(rg_pipe_properties), physical_location)
        {
        }
    };
}