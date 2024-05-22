// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"

namespace pipegen2
{
class ParallelForkPipe : public ForkPipe
{
public:
    ParallelForkPipe(const tt_cxy_pair& physical_location) :
        ForkPipe(RGPipeType::ParallelFork, DataFlowType::Parallel, RGPipeProperties(), physical_location)
    {
    }
};
}  // namespace pipegen2