// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <gmock/gmock.h>

#include "model/data_flow/data_flow_node.h"

namespace pipegen2
{
class DataFlowNodeMock : public DataFlowNode
{
public:
    MOCK_METHOD(NodeId, get_id, (), (const override));

    MOCK_METHOD(DataFlowType, get_dataflow_type, (), (const, override));

    MOCK_METHOD(std::optional<unsigned int>, get_root_to_leaf_path_index, (), (const, override));

    MOCK_METHOD(unsigned int, get_num_root_to_leaf_paths_in_subgraph, (), (const, override));

    MOCK_METHOD(bool, is_padding, (), (const, override));
};
} // namespace pipegen2
