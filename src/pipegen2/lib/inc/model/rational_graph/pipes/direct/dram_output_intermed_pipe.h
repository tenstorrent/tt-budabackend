// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"

namespace pipegen2
{
    class DramOutputIntermedPipe : public DirectPipe
    {
    public:
        DramOutputIntermedPipe(NodeId pipe_id) :
            DirectPipe(RGPipeType::DramOutputIntermed, RGPipeProperties(pipe_id), tt_cxy_pair())
        {
        }
    };
}