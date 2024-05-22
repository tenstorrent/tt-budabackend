// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/transfer_limits_calculator.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::NiceMock;
using ::testing::Return;

/**********************************************************************************************************************
    Tests for function: TransferLimitsCalculator::calculate_transfer_limits
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculator_CalculateTransferLimits : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        bool is_scatter,
        unsigned int num_input_groups,
        unsigned int epoch_tiles,
        unsigned int consume_granularity,
        bool can_do_epoch_in_single_iter,
        unsigned int scatter_gather_num_tiles,
        unsigned int repeat_factor,
        unsigned int serialization_factor,
        bool is_dram_or_pcie_input,
        unsigned int size_tiles,
        unsigned int tiles_per_input)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(num_input_groups));
        ON_CALL(*df_node_mock, get_num_epoch_tiles()).WillByDefault(Return(epoch_tiles));
        ON_CALL(*df_node_mock, get_consume_granularity()).WillByDefault(Return(consume_granularity));
        ON_CALL(*df_node_mock, get_scatter_gather_num_tiles()).WillByDefault(Return(scatter_gather_num_tiles));
        ON_CALL(*df_node_mock, get_repeat_factor()).WillByDefault(Return(repeat_factor));
        ON_CALL(*df_node_mock, get_serialization_factor()).WillByDefault(Return(serialization_factor));
        ON_CALL(*df_node_mock, is_dram_or_pcie_input()).WillByDefault(Return(is_dram_or_pcie_input));
        ON_CALL(*df_node_mock, get_size_tiles()).WillByDefault(Return(size_tiles));
        ON_CALL(*df_node_mock, get_num_tiles_per_input()).WillByDefault(Return(tiles_per_input));

        // Can do epoch in single iteration is true if the node is either untilized dram output or the intermediate
        // node. All combinations are tested in the appropriate internal tests so here always configuring that the
        // node is untilized dram output.
        if (can_do_epoch_in_single_iter)
        {
            ON_CALL(*df_node_mock, is_untilized_dram_output()).WillByDefault(Return(true));
        }
        else
        {
            ON_CALL(*df_node_mock, is_untilized_dram_output()).WillByDefault(Return(false));
            ON_CALL(*df_node_mock, is_intermediate()).WillByDefault(Return(false));
        }

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(
        unsigned int epoch_tiles,
        unsigned int scatter_gather_num_tiles,
        bool is_dram_or_pcie_input,
        unsigned int size_tiles,
        unsigned int tiles_per_input)
    {
        return make_df_node_mock(
            scatter_gather_num_tiles > 0 /* is_scatter */,
            1 /* num_input_groups */,
            epoch_tiles,
            1 /* consume_granularity */,
            false /* can_do_epoch_in_single_iter */,
            scatter_gather_num_tiles,
            1 /* repeat_factor */,
            1 /* serialization_factor */,
            is_dram_or_pcie_input,
            size_tiles,
            tiles_per_input);
    }

    std::unique_ptr<DataFlowNodeMock> make_virtual_df_node_mock(
        unsigned int repeat_factor, unsigned int serialization_factor)
    {
        return make_df_node_mock(
            false /* is_scatter */,
            1 /* num_input_groups */,
            1 /* epoch_tiles */,
            1 /* consume_granularity */,
            false /* can_do_epoch_in_single_iter */,
            1 /* scatter_gather_num_tiles */,
            repeat_factor,
            serialization_factor,
            false /* is_dram_or_pcie_input */,
            1 /* size_tiles */,
            1 /* tiles_per_input */);
    }

    std::unique_ptr<DataFlowNodeMock> make_union_df_node_mock(unsigned int num_input_groups)
    {
        return make_df_node_mock(
            false /* is_scatter */,
            num_input_groups,
            1 /* epoch_tiles */,
            1 /* consume_granularity */,
            false /* can_do_epoch_in_single_iter */,
            1 /* scatter_gather_num_tiles */,
            1 /* repeat_factor */,
            1 /* serialization_factor */,
            false /* is_dram_or_pcie_input */,
            1 /* size_tiles */,
            1 /* tiles_per_input */);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int consume_granularity, bool can_do_epoch_in_single_iter)
    {
        return make_df_node_mock(
            false /* is_scatter */,
            1 /* num_input_groups */,
            1 /* epoch_tiles */,
            consume_granularity,
            can_do_epoch_in_single_iter,
            1 /* scatter_gather_num_tiles */,
            1 /* repeat_factor */,
            1 /* serialization_factor */,
            false /* is_dram_or_pcie_input */,
            1 /* size_tiles */,
            1 /* tiles_per_input */);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculator_CalculateTransferLimits, CompundSanityDataFlowGraph)
{
    std::unique_ptr<DataFlowNodeMock> packer_root_1 = make_root_df_node_mock(
        2400 /* epoch_tiles */,
        2 /* scatter_gather_num_tiles */,
        false /* is_dram_or_pcie_input */,
        4 /* size_tiles */,
        80 /* tiles_per_input */);
    std::unique_ptr<DataFlowNodeMock> packer_root_2 = make_root_df_node_mock(
        2400 /* epoch_tiles */,
        2 /* scatter_gather_num_tiles */,
        false /* is_dram_or_pcie_input */,
        4 /* size_tiles */,
        80 /* tiles_per_input */);

    std::unique_ptr<DataFlowNodeMock> gather_node_1 =
        make_virtual_df_node_mock(3 /* repeat_factor */, 2 /* serialization_factor */);
    std::unique_ptr<DataFlowNodeMock> gather_node_2 =
        make_virtual_df_node_mock(3 /* repeat_factor */, 2 /* serialization_factor */);

    std::unique_ptr<DataFlowNodeMock> union_node = make_union_df_node_mock(2 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> unpacker_leaf =
        make_leaf_df_node_mock(8 /* consume_granularity */, false /* can_do_epoch_in_single_iter */);

    connect_data_flow_graph(
        {make_df_edge(packer_root_1, gather_node_1, {2, 4, 6}),
         make_df_edge(packer_root_2, gather_node_1, {8, 10}),

         make_df_edge(packer_root_1, gather_node_2, {6, 10}),
         make_df_edge(packer_root_2, gather_node_2, {2, 4, 8}),

         make_df_edge(gather_node_1, union_node),
         make_df_edge(gather_node_2, union_node),

         make_df_edge(union_node, unpacker_leaf)});

    std::vector<DataFlowNode*> root_nodes{packer_root_1.get(), packer_root_2.get()};
    std::vector<DataFlowNode*> leaf_nodes{unpacker_leaf.get()};

    TransferLimitsCalculator transfer_limits_calculator(
        2048 /* max_num_tiles_per_phase */, 15 /* max_num_phases_per_iteration */);
    transfer_limits_calculator.calculate_transfer_limits(root_nodes, leaf_nodes);

    verify_transfer_limits(
        packer_root_1.get(),
        false /* is_on_single_source_path */,
        1200 /* num_iterations */,
        2 /* tiles_to_send */,
        1 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);

    verify_transfer_limits(
        packer_root_2.get(),
        false /* is_on_single_source_path */,
        1200 /* num_iterations */,
        2 /* tiles_to_send */,
        1 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);

    verify_transfer_limits(
        gather_node_1.get(),
        false /* is_on_single_source_path */,
        1800 /* num_iterations */,
        10 /* tiles_to_send */,
        1 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);

    verify_transfer_limits(
        gather_node_2.get(),
        false /* is_on_single_source_path */,
        1800 /* num_iterations */,
        10 /* tiles_to_send */,
        1 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);

    verify_transfer_limits(
        union_node.get(),
        false /* is_on_single_source_path */,
        1800 /* num_iterations */,
        10 /* tiles_to_send */,
        2 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);

    verify_transfer_limits(
        unpacker_leaf.get(),
        false /* is_on_single_source_path */,
        1800 /* num_iterations */,
        10 /* tiles_to_send */,
        2 /* number_of_unique_df_paths_through_node */,
        8 /* subtree_common_divisor */,
        2000 /* max_num_tiles_per_phase */);
}