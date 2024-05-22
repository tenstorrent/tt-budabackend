// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "direct_pipe.h"

namespace pipegen2
{
class NonSharedIntermedPipe : public DirectPipe
{
public:
    NonSharedIntermedPipe(NodeId pipe_id) :
        DirectPipe(RGPipeType::NonSharedIntermed, RGPipeProperties(pipe_id), tt_cxy_pair())
    {
    }
};
}  // namespace pipegen2