// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/fork/fork_pipe.h"

#include <stdexcept>

#include "pipegen2_utils.h"

namespace pipegen2
{
    void ForkPipe::set_input(const PipeInput& input)
    {
        if (m_inputs.empty())
        {
            m_inputs.push_back(input);
        }
        else
        {
            m_inputs[0] = input;
        }
    }

    const PipeInput& ForkPipe::get_input() const
    {
        log_assert(!m_inputs.empty(), "ForkPipe: Fork pipe has no input set");

        return m_inputs[0];
    }
}