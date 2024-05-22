// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"

namespace pipegen2
{
class DramPrefetchPreTMParallelForkPipe : public ForkPipe
{
public:
    DramPrefetchPreTMParallelForkPipe(const tt_cxy_pair& physical_location) :
        ForkPipe(
            RGPipeType::DramPrefetchPreTMParallelFork, DataFlowType::Parallel, RGPipeProperties(), physical_location)
    {
    }
};
}  // namespace pipegen2