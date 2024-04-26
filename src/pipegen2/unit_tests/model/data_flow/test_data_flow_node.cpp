// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/data_flow_node.h"

#include <memory>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

/**********************************************************************************************************************
    Tests for function: DataFlowNode::has_processed_all_inputs
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_HasProcessedAllInputs : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock(
        unsigned int number_of_unique_df_paths = 1, unsigned int current_input_group_idx = 0)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        df_node_mock->set_number_of_unique_df_paths(number_of_unique_df_paths);
        for (unsigned int i = 0; i < current_input_group_idx; ++i)
        {
            df_node_mock->move_to_next_input_group();
        }

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_HasProcessedAllInputs, RootNode)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock();

    EXPECT_TRUE(test_df_node->has_processed_all_inputs());
}

TEST_F(Pipegen2_DataFlowNode_HasProcessedAllInputs, CurrentGroupIndexLessThanUniqueDFPaths)
{
    std::unique_ptr<DataFlowNodeMock> root_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        4 /* number_of_unique_df_paths */, 2 /* current_input_group_index */);

    connect_data_flow_graph({ make_df_edge(root_node, test_df_node) });

    EXPECT_FALSE(test_df_node->has_processed_all_inputs());
}

TEST_F(Pipegen2_DataFlowNode_HasProcessedAllInputs, CurrentGroupIndexEqualToUniqueDFPaths)
{
    std::unique_ptr<DataFlowNodeMock> root_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        1 /* number_of_unique_df_paths */, 1 /* current_input_group_index */);

    connect_data_flow_graph({ make_df_edge(root_node, test_df_node) });

    EXPECT_TRUE(test_df_node->has_processed_all_inputs());
}

/**********************************************************************************************************************
    Tests for function: DataFlowNode::get_current_inputs_to_process
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_GetCurrentInputsToProcess : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock(unsigned int number_of_unique_df_paths = 1,
                                                          unsigned int current_input_group_idx = 0,
                                                          bool is_union = false)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        const unsigned int num_input_groups = is_union ? number_of_unique_df_paths : 1;
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(num_input_groups));

        df_node_mock->set_number_of_unique_df_paths(number_of_unique_df_paths);
        for (unsigned int i = 0; i < current_input_group_idx; ++i)
        {
            df_node_mock->move_to_next_input_group();
        }

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_GetCurrentInputsToProcess, NodeAlreadyProessedAllInputsThrows)
{
    std::unique_ptr<DataFlowNodeMock> root_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        1 /* number_of_unique_df_paths */, 1 /* current_input_group_index */);

    connect_data_flow_graph({ make_df_edge(root_node, test_df_node) });

    verify_log_assert(
        [&]()
        {
            test_df_node->get_current_inputs_to_process();
        },
        "^Current input group index out of range for node " + std::to_string(test_df_node->get_id()) + ".*");
}

TEST_F(Pipegen2_DataFlowNode_GetCurrentInputsToProcess, NonUnionNodeReturnsAllInputs)
{
    std::unique_ptr<DataFlowNodeMock> root_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        1 /* number_of_unique_df_paths */, 0 /* current_input_group_index */);

    connect_data_flow_graph({ make_df_edge(root_node, test_df_node, {0, 1, 2, 3, 4, 5, 6} /* offsets */) });

    DataFlowNodeInputRange input_range = test_df_node->get_current_inputs_to_process();

    verify_data_flow_node_input_range(input_range, test_df_node->get_all_inputs());
}

TEST_F(Pipegen2_DataFlowNode_GetCurrentInputsToProcess, UnionReturnsInputsInChunks)
{
    std::unique_ptr<DataFlowNodeMock> src_node_1 = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> src_node_2 = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> src_node_3 = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        3 /* number_of_unique_df_paths */, 0 /* current_input_group_index */, true /* is_union */);

    connect_data_flow_graph({
        make_df_edge(src_node_1, test_df_node),
        make_df_edge(src_node_2, test_df_node),
        make_df_edge(src_node_3, test_df_node)
    });

    DataFlowNodeInputRange input_range_1 = test_df_node->get_current_inputs_to_process();
    verify_data_flow_node_input_range(input_range_1, { DataFlowNodeInput(src_node_1.get(), 0 /* offset */) });

    test_df_node->move_to_next_input_group();

    DataFlowNodeInputRange input_range_2 = test_df_node->get_current_inputs_to_process();
    verify_data_flow_node_input_range(input_range_2, { DataFlowNodeInput(src_node_2.get(), 0 /* offset */) });

    test_df_node->move_to_next_input_group();

    DataFlowNodeInputRange input_range_3 = test_df_node->get_current_inputs_to_process();
    verify_data_flow_node_input_range(input_range_3, { DataFlowNodeInput(src_node_3.get(), 0 /* offset */) });
}

/**********************************************************************************************************************
    Tests for function: DataFlowNode::try_update_last_sending_phase
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_TryUpdatingLastSendingPhase : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock(
        unsigned int max_tiles_per_phase = 0, unsigned int tiles_to_send = 0)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);
        df_node_mock->set_tiles_to_send(tiles_to_send);

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_TryUpdatingLastSendingPhase, NullDestinationNodeThrows)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock();

    verify_log_assert(
        [&]()
        {
            test_df_node->try_update_last_sending_phase(nullptr);
        },
        "^Expecting valid destination as argument to DataFlowNode::try_update_last_sending_phase.*");
}

TEST_F(Pipegen2_DataFlowNode_TryUpdatingLastSendingPhase, SrcHasNoSendingPhasesToDest)
{
    std::unique_ptr<DataFlowNodeMock> dest_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock();

    verify_log_assert(
        [&]()
        {
            test_df_node->try_update_last_sending_phase(dest_node.get());
        },
        "^Did not find sending phases from node " + std::to_string(test_df_node->get_id()) +
        " to destination node " + std::to_string(dest_node->get_id()) + ".*");
}

TEST_F(Pipegen2_DataFlowNode_TryUpdatingLastSendingPhase, CanUpdateLastPhase)
{
    std::unique_ptr<DataFlowNodeMock> dest_node = create_df_node_mock(
        100 /* max_tiles_per_phase */, 12 /* tiles_to_send */);
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(100 /* max_tiles_per_phase */);

    const unsigned int last_phase_num_msgs = 48;
    test_df_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, last_phase_num_msgs);

    EXPECT_TRUE(test_df_node->try_update_last_sending_phase(dest_node.get()));

    const PhaseInfo& last_sending_phase = test_df_node->get_sending_phases(dest_node.get()).back();
    EXPECT_EQ(last_sending_phase.num_msgs, last_phase_num_msgs + test_df_node->get_tiles_to_send());
}

TEST_F(Pipegen2_DataFlowNode_TryUpdatingLastSendingPhase, CantUpdateLastPhase)
{
    std::unique_ptr<DataFlowNodeMock> dest_node = create_df_node_mock(100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        100 /* max_tiles_per_phase */, 12 /* tiles_to_send */);

    const unsigned int last_phase_num_msgs = 96;
    test_df_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, last_phase_num_msgs);

    EXPECT_FALSE(test_df_node->try_update_last_sending_phase(dest_node.get()));

    const PhaseInfo& last_sending_phase = test_df_node->get_sending_phases(dest_node.get()).back();
    EXPECT_EQ(last_sending_phase.num_msgs, last_phase_num_msgs);
}

/**********************************************************************************************************************
    Tests for function: DataFlowNode::get_num_sending_phases
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_GetNumSendingPhases : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock()
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_GetNumSendingPhases, NoPhasesTowardsDestination)
{
    std::unique_ptr<DataFlowNodeMock> dest_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock();

    EXPECT_EQ(test_df_node->get_num_sending_phases(dest_node.get()), 0);
}

TEST_F(Pipegen2_DataFlowNode_GetNumSendingPhases, MultipleSendingPhasesTowardsDestination)
{
    std::unique_ptr<DataFlowNodeMock> dest_node = create_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock();

    const unsigned int num_sending_phase = 5;
    for (unsigned int i = 0; i < num_sending_phase; ++i)
    {
        test_df_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 1 /* num_msgs */);
    }

    EXPECT_EQ(test_df_node->get_num_sending_phases(dest_node.get()), num_sending_phase);
}

/**********************************************************************************************************************
    Tests for function: DataFlowNode::can_expand_last_receiving_phase
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_CanExpandLastReceivingPhase : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock(unsigned int max_num_tiles_per_phase,
                                                          unsigned int num_input_groups = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        df_node_mock->set_max_num_tiles_per_phase(max_num_tiles_per_phase);
        df_node_mock->set_number_of_unique_df_paths(num_input_groups);

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_CanExpandLastReceivingPhase, NoReceivingPhasesInOnlyInputGroup)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(100 /* max_num_tiles_per_phase */);

    EXPECT_TRUE(test_df_node->can_expand_last_receiving_phase(42 /* receiving_chunk_size */));
}

TEST_F(Pipegen2_DataFlowNode_CanExpandLastReceivingPhase, NoReceivingPhasesInNewlyOpenedInputGroup)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(
        50 /* max_num_tiles_per_phase */, 2 /* input_group_count */);

    test_df_node->add_receiving_phase(0 /* phase_offset */, 21 /* num_msgs */);

    EXPECT_FALSE(test_df_node->can_expand_last_receiving_phase(42 /* receiving_chunk_size */));

    test_df_node->move_to_next_input_group();

    EXPECT_TRUE(test_df_node->can_expand_last_receiving_phase(42 /* receiving_chunk_size */));
}

TEST_F(Pipegen2_DataFlowNode_CanExpandLastReceivingPhase, CanExpand)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(50 /* max_num_tiles_per_phase */);

    test_df_node->add_receiving_phase(0 /* phase_offset */, 21 /* num_msgs */);

    EXPECT_TRUE(test_df_node->can_expand_last_receiving_phase(29 /* receiving_chunk_size */));
}

/**********************************************************************************************************************
    Tests for function: DataFlowNode::update_receiving_phases
**********************************************************************************************************************/

class Pipegen2_DataFlowNode_UpdateReceivingPhases : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> create_df_node_mock(unsigned int max_num_tiles_per_phase)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        df_node_mock->set_max_num_tiles_per_phase(max_num_tiles_per_phase);

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_DataFlowNode_UpdateReceivingPhases, EmptyReceivingPhases)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(100 /* max_num_tiles_per_phase */);

    verify_log_assert(
        [&]()
        {
            test_df_node->update_receiving_phases(0 /* phase_offset */, 42 /* receiving_chunk_size */);
        },
        "^Receiving phases of node " + std::to_string(test_df_node->get_id()) + " should not be empty when updating.*");
}

TEST_F(Pipegen2_DataFlowNode_UpdateReceivingPhases, CanExpand)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(100 /* max_num_tiles_per_phase */);

    test_df_node->add_receiving_phase(0 /* phase_offset */, 21 /* num_msgs */);

    test_df_node->update_receiving_phases(12 /* phase_offset */, 42 /* receiving_chunk_size */);

    verify_phases(
        test_df_node->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(0 /* phase_offset */, 21 + 42 /* num_msgs */)});
}

TEST_F(Pipegen2_DataFlowNode_UpdateReceivingPhases, MustAddNewReceivingPhase)
{
    std::unique_ptr<DataFlowNodeMock> test_df_node = create_df_node_mock(40 /* max_num_tiles_per_phase */);

    test_df_node->add_receiving_phase(0 /* phase_offset */, 21 /* num_msgs */);

    test_df_node->update_receiving_phases(12 /* phase_offset */, 42 /* receiving_chunk_size */);

    verify_phases(
        test_df_node->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(0 /* phase_offset */, 21 /* num_msgs */), PhaseInfo(12 /* phase_offset */, 42 /* num_msgs */)});
}
