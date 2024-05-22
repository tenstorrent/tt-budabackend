// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "serial_fork_pipe.h"

namespace pipegen2
{
class PaddingSerialForkPipe : public SerialForkPipe
{
public:
    PaddingSerialForkPipe(
        RGPipeProperties&& rg_pipe_properties,
        const tt_cxy_pair& physical_location,
        unsigned int num_serial_outputs,
        unsigned int num_serial_padding_outputs) :
        SerialForkPipe(std::move(rg_pipe_properties), physical_location, num_serial_outputs, num_serial_padding_outputs)
    {
        m_pipe_type = RGPipeType::PaddingSerialFork;
    }
};
}  // namespace pipegen2