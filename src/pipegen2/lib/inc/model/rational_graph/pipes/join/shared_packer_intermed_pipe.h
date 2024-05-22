// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "join_pipe.h"

namespace pipegen2
{
class SharedPackerIntermedPipe : public JoinPipe
{
public:
    SharedPackerIntermedPipe(NodeId pipe_id) :
        JoinPipe(RGPipeType::SharedPackerIntermed, DataFlowType::Serial, RGPipeProperties(pipe_id), tt_cxy_pair())
    {
    }

    const PipeInput& get_shared_packer_virtual_node() const;

    const std::vector<PipeInput>& get_inputs() const override;

private:
    mutable std::vector<PipeInput> m_inputs_without_virtual;
};
}  // namespace pipegen2