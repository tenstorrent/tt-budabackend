// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fork_pipe.h"

namespace pipegen2
{
    class SerialForkPipe : public ForkPipe
    {
    public:
        SerialForkPipe(RGPipeProperties&& rg_pipe_properties,
                       const tt_cxy_pair& physical_location,
                       unsigned int num_serial_outputs,
                       unsigned int num_serial_padding_outputs) :
            ForkPipe(RGPipeType::SerialFork, DataFlowType::Serial, std::move(rg_pipe_properties), physical_location),
            m_num_serial_outputs(num_serial_outputs),
            m_num_serial_padding_outputs(num_serial_padding_outputs)
        {
        }

        unsigned int get_num_serial_outputs() const { return m_num_serial_outputs; }

        unsigned int get_num_serial_padding_outputs() const { return m_num_serial_padding_outputs; }

        unsigned int get_num_serial_non_padding_outputs() const
        {
            return m_num_serial_outputs - m_num_serial_padding_outputs;
        }

    private:
        // Number of scatter outputs of a PG pipe this serial fork pipe is part of.
        unsigned int m_num_serial_outputs;

        // Number of scatter outputs in which a PG pipe this serial fork pipe is created from reads from output
        // padding buffers.
        unsigned int m_num_serial_padding_outputs;
    };
}