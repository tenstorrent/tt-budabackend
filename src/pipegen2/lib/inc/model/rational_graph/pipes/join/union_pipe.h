// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "join_pipe.h"

namespace pipegen2
{
class UnionPipe : public JoinPipe
{
public:
    UnionPipe(const tt_cxy_pair& physical_location) :
        JoinPipe(RGPipeType::Union, DataFlowType::Serial, RGPipeProperties(), physical_location)
    {
    }
};
}  // namespace pipegen2