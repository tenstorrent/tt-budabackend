// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/data_flow_node_input.h"

// clang-format off
#include "utils/logger.hpp"

#include "model/data_flow/data_flow_node.h"
// clang-format on

namespace pipegen2
{
DataFlowNodeInputRange::DataFlowNodeInputRange(
    const std::vector<DataFlowNodeInput>& df_inputs, unsigned int start_index, unsigned int range_size) :
    m_df_inputs(df_inputs), m_start_index(start_index), m_range_size(range_size)
{
    log_assert(start_index < df_inputs.size(), "Start index out of bounds");
    log_assert(start_index + range_size <= df_inputs.size(), "Range size is not within the bounds");
}

std::vector<DataFlowNodeInput>::const_iterator DataFlowNodeInputRange::begin() const
{
    return m_df_inputs.begin() + m_start_index;
}

std::vector<DataFlowNodeInput>::const_iterator DataFlowNodeInputRange::end() const { return begin() + m_range_size; }

const DataFlowNodeInput& DataFlowNodeInputRange::first() const
{
    log_assert(m_range_size > 0, "Trying to get the first element of an empty input range");

    return *begin();
}
}  // namespace pipegen2
