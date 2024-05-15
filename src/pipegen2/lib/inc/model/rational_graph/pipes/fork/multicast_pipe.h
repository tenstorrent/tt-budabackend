// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"

namespace pipegen2
{
    class MulticastPipe : public ForkPipe
    {
    public:
        MulticastPipe(RGPipeProperties&& rg_pipe_properties,
                      const tt_cxy_pair& physical_location,
                      const bool packer_multicast_optimization_enabled) :
            ForkPipe(RGPipeType::Multicast, DataFlowType::ParallelCopy, std::move(rg_pipe_properties),
                     physical_location),
            m_packer_multicast_optimization_enabled(packer_multicast_optimization_enabled)
        {
        }

        // Returns true if pipe has packer multicast optimization enabled, false otherwise.
        bool is_packer_multicast_optimization_enabled() const { return m_packer_multicast_optimization_enabled; }

    private:
        // True if pipe has packer multicast optimization enabled where we can avoid creating both packer and
        // multicast streams and just use multicast stream directly from the packer core.
        bool m_packer_multicast_optimization_enabled;
    };
}