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

    MOCK_METHOD(bool, is_scatter, (), (const, override));

    MOCK_METHOD(unsigned int, get_input_group_count, (), (const, override));

    MOCK_METHOD(unsigned int, get_consume_granularity, (), (const, override));

    MOCK_METHOD(unsigned int, get_num_epoch_tiles, (), (const, override));

    MOCK_METHOD(bool, is_dram_or_pcie_input, (), (const, override));

    MOCK_METHOD(bool, is_dram_parallel_fork, (), (const, override));

    MOCK_METHOD(bool, is_intermediate, (), (const, override));

    MOCK_METHOD(bool, is_untilized_dram_output, (), (const, override));

    MOCK_METHOD(bool, is_dram_or_pcie_output, (), (const, override));

    MOCK_METHOD(unsigned int, get_scatter_gather_num_tiles, (), (const, override));

    MOCK_METHOD(unsigned int, get_repeat_factor, (), (const, override));

    MOCK_METHOD(unsigned int, get_serialization_factor, (), (const, override));

    MOCK_METHOD(unsigned int, get_size_tiles, (), (const, override));

    MOCK_METHOD(unsigned int, get_num_tiles_per_input, (), (const, override));
};

} // namespace pipegen2
