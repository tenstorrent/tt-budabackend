// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/rational_graph/pipes/join/shared_packer_intermed_pipe.h"

#include "utils/logger.hpp"

namespace pipegen2
{
const PipeInput& SharedPackerIntermedPipe::get_shared_packer_virtual_node() const
{
    log_assert(m_inputs.size() - m_inputs_without_virtual.size() == 1, "Shared virtual node doesn't exist");

    return m_inputs.back();
}

const std::vector<PipeInput>& SharedPackerIntermedPipe::get_inputs() const
{
    if (m_inputs_without_virtual.empty())
    {
        log_assert(m_inputs.size() > 1, "Expected pipe to have more than one input");
        m_inputs_without_virtual.assign(m_inputs.begin(), m_inputs.end() - 1);
    }

    return m_inputs_without_virtual;
}
}  // namespace pipegen2