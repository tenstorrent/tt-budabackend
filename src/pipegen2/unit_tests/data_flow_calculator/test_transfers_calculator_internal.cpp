// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/transfers_calculator_internal.h"

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

using ::testing::Contains;
using ::testing::Key;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_phases_for_root
**********************************************************************************************************************/

class Pipegen2_TransfersCalculatorInternal_CalculatePhasesForRoot : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_src_node_mock(unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase);
    }

    std::unique_ptr<DataFlowNodeMock> make_dest_node_mock()
    {
        return make_df_node_mock(0 /* tiles_to_send */, 0 /* max_tiles_per_phase */);
    }

    NodeId m_node_id;
    static constexpr unsigned int c_data_offset = 10;
    static constexpr unsigned int c_start_phase_offset = 100;
};

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculatePhasesForRoot, RootNodeWithNoDest)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(12 /* tiles_to_send */, 2010 /* max_tiles_per_phase */);

    verify_log_assert(
        [&]()
        {
            data_flow_internal::calculate_phases_for_root(
                src_node.get(), 0 /* data_offset */, nullptr /* dest */, 12 /* start_phase_offset */);
        },
        "^Expecting destination node to be set for root node [0-9]+.*");
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculatePhasesForRoot, LessThanMaxTilesPerPhaseTilesToSend)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(12 /* tiles_to_send */, 2010 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({make_df_edge(src_node, dest_node)});

    int phase_offset = data_flow_internal::calculate_phases_for_root(
        src_node.get(), c_data_offset, dest_node.get() /* dest */, c_start_phase_offset);

    EXPECT_EQ(phase_offset, 1);

    verify_phases(src_node->get_sending_phases(dest_node.get()), {PhaseInfo(c_start_phase_offset, c_data_offset, 12)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculatePhasesForRoot, MoreThanMaxTilesPerPhaseTilesToSend)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(5000 /* tiles_to_send */, 2010 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({make_df_edge(src_node, dest_node)});

    int phase_offset = data_flow_internal::calculate_phases_for_root(
        src_node.get(), c_data_offset, dest_node.get() /* dest */, c_start_phase_offset);

    EXPECT_EQ(phase_offset, 3);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, c_data_offset, 2010),
         PhaseInfo(c_start_phase_offset + 1, c_data_offset, 2010),
         PhaseInfo(c_start_phase_offset + 2, c_data_offset, 980)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculatePhasesForRoot, TilesToSendIsMultipleOfMaxTilesPerPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(6400 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({make_df_edge(src_node, dest_node)});

    int phase_offset = data_flow_internal::calculate_phases_for_root(
        src_node.get(), c_data_offset, dest_node.get() /* dest */, c_start_phase_offset);

    EXPECT_EQ(phase_offset, 5);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {
            PhaseInfo(c_start_phase_offset, c_data_offset, 1280),
            PhaseInfo(c_start_phase_offset + 1, c_data_offset, 1280),
            PhaseInfo(c_start_phase_offset + 2, c_data_offset, 1280),
            PhaseInfo(c_start_phase_offset + 3, c_data_offset, 1280),
            PhaseInfo(c_start_phase_offset + 4, c_data_offset, 1280),
        });
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::can_sender_accumulate
**********************************************************************************************************************/

class Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int tiles_to_send,
        unsigned int max_tiles_per_phase,
        bool is_dram_or_pcie_input,
        bool is_dram_parallel_fork)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_dram_or_pcie_input()).WillByDefault(Return(is_dram_or_pcie_input));
        ON_CALL(*df_node_mock, is_dram_parallel_fork()).WillByDefault(Return(is_dram_parallel_fork));
        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_src_node_mock(unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(
            tiles_to_send, max_tiles_per_phase, false /* is_dram_or_pcie_input */, false /* is_dram_parallel_fork */);
    }

    std::unique_ptr<DataFlowNodeMock> make_dram_or_pcie_src_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(
            tiles_to_send, max_tiles_per_phase, true /* is_dram_or_pcie_input */, false /* is_dram_parallel_fork */);
    }

    std::unique_ptr<DataFlowNodeMock> make_dram_parallel_fork_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(
            tiles_to_send, max_tiles_per_phase, false /* is_dram_or_pcie_input */, true /* is_dram_parallel_fork */);
    }

    std::unique_ptr<DataFlowNodeMock> make_dest_node_mock(unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(
            0 /* tiles_to_send */,
            max_tiles_per_phase,
            false /* is_dram_or_pcie_input */,
            false /* is_dram_parallel_fork */);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, DestChangesSender)
{
    std::unique_ptr<DataFlowNodeMock> prev_src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> curr_src_node =
        make_src_node_mock(8 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({make_df_edge(prev_src_node, dest_node), make_df_edge(curr_src_node, dest_node)});

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = 0;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), prev_src_node.get(), prev_data_offset, curr_src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SrcIsDramOrPcieNode)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_dram_or_pcie_src_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 4 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SrcIsDramParallelFork)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_dram_parallel_fork_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 4 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, DataOffsetsNotAdjacent)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 4 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 42;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SrcHasNoSendingPhasesForDest)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    verify_log_assert(
        [&]()
        {
            data_flow_internal::can_sender_accumulate(
                dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);
        },
        "^Did not find sending phases from node [0-9]+ to destination node [0-9]+.*");
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SrcCantUpdateLastSendingPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 1278 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, DestCantExpandLastReceivingPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(1280 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    dest_node->add_receiving_phase(0 /* phase_offset */, 1278 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_FALSE(can_sender_acc);
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SenderAbleToAccumulate)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(2010 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 1276 /* num_msgs */);
    dest_node->add_receiving_phase(0 /* phase_offset */, 2006 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_TRUE(can_sender_acc);
    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(0 /* phase_offset */, 0 /* data_offset */, 1280 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CanSenderAccumulate, SenderAbleToAccumulateDestHasNoReceivngPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(2010 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    src_node->add_sending_phase(dest_node.get(), 0 /* phase_offset */, 0 /* data_offset */, 1276 /* num_msgs */);

    unsigned int prev_data_offset = 0;
    unsigned int current_data_offset = prev_data_offset + 4;
    const bool can_sender_acc = data_flow_internal::can_sender_accumulate(
        dest_node.get(), src_node.get(), prev_data_offset, src_node.get(), current_data_offset);

    EXPECT_TRUE(can_sender_acc);
    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(0 /* phase_offset */, 0 /* data_offset */, 1280 /* num_msgs */)});
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_sending_phases
**********************************************************************************************************************/

class Pipegen2_TransfersCalculatorInternal_CalculateSendingPhases : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));
        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_src_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count = 1)
    {
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase, input_group_count);
    }

    std::unique_ptr<DataFlowNodeMock> make_dest_node_mock()
    {
        return make_df_node_mock(0 /* tiles_to_send */, 0 /* max_tiles_per_phase */, 1 /* input_group_count */);
    }

    NodeId m_node_id;
    constexpr static unsigned int c_data_offset = 10;
    constexpr static unsigned int c_start_phase_offset = 100;
};

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateSendingPhases, SingleSendingPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 1280 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    const unsigned int num_receiving_phases = 12;
    for (unsigned int i = 0; i < num_receiving_phases; ++i)
    {
        src_node->add_receiving_phase(c_start_phase_offset + i /* phase_offset */, 8 /* num_msgs */);
    }

    data_flow_internal::calculate_sending_phases(src_node.get(), c_data_offset, dest_node.get(), c_start_phase_offset);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, c_data_offset, 12 * 8 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateSendingPhases, MultipleFullSendingPhases)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 32 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    const unsigned int num_receiving_phases = 12;
    for (unsigned int i = 0; i < num_receiving_phases; ++i)
    {
        src_node->add_receiving_phase(c_start_phase_offset + i /* phase_offset */, 8 /* num_msgs */);
    }

    data_flow_internal::calculate_sending_phases(src_node.get(), c_data_offset, dest_node.get(), c_start_phase_offset);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, c_data_offset, 32 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 4, c_data_offset, 32 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 8, c_data_offset, 32 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateSendingPhases, MultipleSendingPhasesWithReminder)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 32 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    const unsigned int num_receiving_phases = 14;
    for (unsigned int i = 0; i < num_receiving_phases; ++i)
    {
        src_node->add_receiving_phase(c_start_phase_offset + i /* phase_offset */, 8 /* num_msgs */);
    }

    data_flow_internal::calculate_sending_phases(src_node.get(), c_data_offset, dest_node.get(), c_start_phase_offset);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, c_data_offset, 32 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 4, c_data_offset, 32 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 8, c_data_offset, 32 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 12, c_data_offset, 16 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateSendingPhases, SendingPhasesAfterInputGroupChange)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 32 /* max_tiles_per_phase */, 2 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock();

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node),
    });

    const unsigned int num_receiving_phases_1 = 14;
    for (unsigned int i = 0; i < num_receiving_phases_1; ++i)
    {
        src_node->add_receiving_phase(c_start_phase_offset + i /* phase_offset */, 8 /* num_msgs */);
    }

    src_node->move_to_next_input_group();

    const unsigned int num_receiving_phases_2 = 3;
    for (unsigned int i = 0; i < num_receiving_phases_2; ++i)
    {
        src_node->add_receiving_phase(
            c_start_phase_offset + num_receiving_phases_1 + i /* phase_offset */, 4 /* num_msgs */);
    }

    data_flow_internal::calculate_sending_phases(
        src_node.get(), c_data_offset, dest_node.get(), c_start_phase_offset + num_receiving_phases_1);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset + num_receiving_phases_1, c_data_offset, 12 /* num_msgs */)});
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_sending_phases
**********************************************************************************************************************/

class Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));
        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);
        df_node_mock->set_number_of_unique_df_paths(input_group_count);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_src_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count = 1)
    {
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase, input_group_count);
    }

    std::unique_ptr<DataFlowNodeMock> make_dest_node_mock(
        unsigned int max_tiles_per_phase, unsigned int input_group_count = 1)
    {
        return make_df_node_mock(0 /* tiles_to_send */, max_tiles_per_phase, input_group_count);
    }

    NodeId m_node_id;
    constexpr static unsigned int c_start_phase_offset = 100;
};

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases, SameSenderCanAccumulateReceiverCanExpand)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 32 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(40 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node, {0, 4, 8}),
    });

    unsigned int inputs_offset = data_flow_internal::calculate_receiving_phases(dest_node.get(), c_start_phase_offset);

    EXPECT_EQ(inputs_offset, 1);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {
            PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 12 /* num_msgs */),
        });

    const std::map<unsigned int, std::vector<PhaseInfo>>& receiving_phases_per_input_group =
        dest_node->get_receiving_phases_per_input_group();

    EXPECT_EQ(receiving_phases_per_input_group.size(), 1);
    EXPECT_THAT(receiving_phases_per_input_group, Contains(Key(0)));
    verify_phases(
        receiving_phases_per_input_group.at(0),
        {
            PhaseInfo(c_start_phase_offset, 12 /* num_msgs */),
        });
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases, SameSenderCantAccumulateReceiverCanExpand)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(4 /* tiles_to_send */, 32 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(40 /* max_tiles_per_phase */);

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node, {0, 8, 4, 12}),
    });

    unsigned int inputs_offset = data_flow_internal::calculate_receiving_phases(dest_node.get(), c_start_phase_offset);

    EXPECT_EQ(inputs_offset, 4);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 4 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 1, 8 /* data_offset */, 4 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 2, 4 /* data_offset */, 4 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 3, 12 /* data_offset */, 4 /* num_msgs */)});

    const std::map<unsigned int, std::vector<PhaseInfo>>& receiving_phases_per_input_group =
        dest_node->get_receiving_phases_per_input_group();

    EXPECT_EQ(receiving_phases_per_input_group.size(), 1);
    EXPECT_THAT(receiving_phases_per_input_group, Contains(Key(0)));
    verify_phases(
        receiving_phases_per_input_group.at(0),
        {
            PhaseInfo(c_start_phase_offset, 16 /* num_msgs */),
        });
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases, SameSenderReceiverCantExpandReceivingPhase)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(53 /* max_tiles_per_phase */);

    // Offsets will be {0, 5, 10, ...}
    std::vector<unsigned int> offsets(20);
    std::generate(offsets.begin(), offsets.end(), [n = 0]() mutable { return 5 * n++; });

    connect_data_flow_graph({
        make_df_edge(src_node, dest_node, offsets),
    });

    unsigned int inputs_offset = data_flow_internal::calculate_receiving_phases(dest_node.get(), c_start_phase_offset);

    EXPECT_EQ(inputs_offset, 2);

    verify_phases(
        src_node->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 50 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 1, 50 /* data_offset */, 50 /* num_msgs */)});

    const std::map<unsigned int, std::vector<PhaseInfo>>& receiving_phases_per_input_group =
        dest_node->get_receiving_phases_per_input_group();

    EXPECT_EQ(receiving_phases_per_input_group.size(), 1);
    EXPECT_THAT(receiving_phases_per_input_group, Contains(Key(0)));
    verify_phases(
        receiving_phases_per_input_group.at(0),
        {PhaseInfo(c_start_phase_offset, 50 /* num_msgs */), PhaseInfo(c_start_phase_offset + 1, 50 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases, DifferentSender)
{
    std::unique_ptr<DataFlowNodeMock> src_node_1 =
        make_src_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> src_node_2 =
        make_src_node_mock(3 /* tiles_to_send */, 10 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node = make_dest_node_mock(77 /* max_tiles_per_phase */);

    connect_data_flow_graph(
        {make_df_edge(src_node_1, dest_node, {} /* offsets */), make_df_edge(src_node_2, dest_node, {} /* offsets */)});

    configure_df_inputs(
        dest_node,
        {make_df_input(src_node_1, 0),
         make_df_input(src_node_1, 5),
         make_df_input(src_node_1, 10),
         make_df_input(src_node_2, 0),
         make_df_input(src_node_2, 3),
         make_df_input(src_node_2, 6),
         make_df_input(src_node_2, 9),
         make_df_input(src_node_2, 0),
         make_df_input(src_node_2, 3),
         make_df_input(src_node_2, 6),
         make_df_input(src_node_2, 9),
         make_df_input(src_node_1, 15),
         make_df_input(src_node_1, 15)});

    unsigned int inputs_offset = data_flow_internal::calculate_receiving_phases(dest_node.get(), c_start_phase_offset);

    EXPECT_EQ(inputs_offset, 7);

    verify_phases(
        src_node_1->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 15 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 5, 15 /* data_offset */, 5 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 6, 15 /* data_offset */, 5 /* num_msgs */)});

    verify_phases(
        src_node_2->get_sending_phases(dest_node.get()),
        {PhaseInfo(c_start_phase_offset + 1, 0 /* data_offset */, 9 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 2, 9 /* data_offset */, 3 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 3, 0 /* data_offset */, 9 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 4, 9 /* data_offset */, 3 /* num_msgs */)});

    const std::map<unsigned int, std::vector<PhaseInfo>>& receiving_phases_per_input_group =
        dest_node->get_receiving_phases_per_input_group();

    EXPECT_EQ(receiving_phases_per_input_group.size(), 1);
    EXPECT_THAT(receiving_phases_per_input_group, Contains(Key(0)));
    verify_phases(
        receiving_phases_per_input_group.at(0),
        {PhaseInfo(c_start_phase_offset, 15 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 1, 24 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 5, 10 /* num_msgs */)});
}

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculateReceivingPhases, DestHasDuplicatesSenderChangesDestination)
{
    std::unique_ptr<DataFlowNodeMock> src_node =
        make_src_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> dest_node_1 =
        make_dest_node_mock(77 /* max_tiles_per_phase */, 2 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> dest_node_2 = make_dest_node_mock(77 /* max_tiles_per_phase */);

    connect_data_flow_graph(
        {make_df_edge(src_node, dest_node_1, {0, 0} /* offsets */),
         make_df_edge(src_node, dest_node_2, {0} /* offsets */)});

    int offset_dest_1 = data_flow_internal::calculate_receiving_phases(dest_node_1.get(), c_start_phase_offset);
    EXPECT_EQ(offset_dest_1, 1);

    int offset_dest_2 =
        data_flow_internal::calculate_receiving_phases(dest_node_2.get(), c_start_phase_offset + offset_dest_1);
    EXPECT_EQ(offset_dest_2, 1);

    dest_node_1->move_to_next_input_group();

    offset_dest_1 = data_flow_internal::calculate_receiving_phases(
        dest_node_1.get(), c_start_phase_offset + offset_dest_1 + offset_dest_2);
    EXPECT_EQ(offset_dest_1, 1);

    const std::map<unsigned int, std::vector<PhaseInfo>>& dest_1_phases =
        dest_node_1->get_receiving_phases_per_input_group();
    const std::map<unsigned int, std::vector<PhaseInfo>>& dest_2_phases =
        dest_node_2->get_receiving_phases_per_input_group();

    EXPECT_EQ(dest_2_phases.size(), 1);
    EXPECT_THAT(dest_2_phases, Contains(Key(0)));
    verify_phases(dest_2_phases.at(0), {PhaseInfo(c_start_phase_offset + 1, 5 /* num_msgs */)});

    EXPECT_EQ(dest_1_phases.size(), 2);
    EXPECT_THAT(dest_1_phases, Contains(Key(0)));
    verify_phases(dest_1_phases.at(0), {PhaseInfo(c_start_phase_offset, 5 /* num_msgs */)});
    EXPECT_THAT(dest_1_phases, Contains(Key(1)));
    verify_phases(dest_1_phases.at(1), {PhaseInfo(c_start_phase_offset + 2, 5 /* num_msgs */)});
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_sending_phases
**********************************************************************************************************************/

class Pipegen2_TransfersCalculatorInternal_CalculatePhases : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));
        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);
        df_node_mock->set_number_of_unique_df_paths(input_group_count);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_dest_node_mock(
        unsigned int max_tiles_per_phase, unsigned int input_group_count = 1)
    {
        return make_df_node_mock(0 /* tiles_to_send */, max_tiles_per_phase, input_group_count);
    }

    NodeId m_node_id;
    constexpr static unsigned int c_start_phase_offset = 100;
};

TEST_F(Pipegen2_TransfersCalculatorInternal_CalculatePhases, AllDataMovementTypesCombined)
{
    std::unique_ptr<DataFlowNodeMock> src_node_1 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> src_node_2 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> fork_1_1 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> fork_1_2 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> fork_1_3 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> fork_2_1 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> fork_2_2 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> fork_2_3 =
        make_df_node_mock(5 /* tiles_to_send */, 100 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> gather_1 =
        make_df_node_mock(40 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> gather_2 =
        make_df_node_mock(40 /* tiles_to_send */, 100 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> gather_3 =
        make_df_node_mock(15 /* tiles_to_send */, 100 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> union_node =
        make_df_node_mock(40 /* tiles_to_send */, 100 /* max_tiles_per_phase */, 2 /* input_group_count */);

    std::unique_ptr<DataFlowNodeMock> dest_1 =
        make_dest_node_mock(100 /* max_tiles_per_phase */, 2 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> dest_2 =
        make_dest_node_mock(100 /* max_tiles_per_phase */, 1 /* input_group_count */);

    connect_data_flow_graph(
        {make_df_edge(src_node_1, fork_1_1),
         make_df_edge(src_node_1, fork_1_2),
         make_df_edge(src_node_1, fork_1_3),

         make_df_edge(src_node_2, fork_2_1),
         make_df_edge(src_node_2, fork_2_2),
         make_df_edge(src_node_2, fork_2_3),

         // Gather DF inputs will be configured later on so pasing an empty offsets vector to avoid adding default {0}.
         make_df_edge(fork_1_1, gather_1, {}),
         make_df_edge(fork_2_1, gather_1, {}),

         make_df_edge(fork_1_2, gather_2, {}),
         make_df_edge(fork_2_2, gather_2, {}),

         make_df_edge(fork_1_3, gather_3, {}),
         make_df_edge(fork_2_3, gather_3, {}),

         make_df_edge(gather_1, union_node),
         make_df_edge(gather_2, union_node),

         // Because input_group_count of dest_1 is 2 it needs to have 2 inputs.
         make_df_edge(union_node, dest_1, {0, 0}),
         make_df_edge(gather_3, dest_2)});

    // Df inputs are split in lines how they will end up being grouped in phases on the sender nodes.
    configure_df_inputs(
        gather_1,
        {make_df_input(fork_1_1, 0),
         make_df_input(fork_1_1, 5),
         make_df_input(fork_1_1, 10),
         make_df_input(fork_2_1, 10),
         make_df_input(fork_2_1, 15),
         make_df_input(fork_2_1, 0),
         make_df_input(fork_2_1, 0),
         make_df_input(fork_1_1, 40)});

    configure_df_inputs(
        gather_2,
        {make_df_input(fork_2_2, 0),
         make_df_input(fork_2_2, 5),
         make_df_input(fork_2_2, 10),
         make_df_input(fork_1_2, 10),
         make_df_input(fork_1_2, 15),
         make_df_input(fork_1_2, 0),
         make_df_input(fork_1_2, 0),
         make_df_input(fork_2_2, 40)});

    configure_df_inputs(
        gather_3,
        {
            make_df_input(fork_2_3, 0),
            make_df_input(fork_2_3, 5),
            make_df_input(fork_1_3, 10),
        });

    int start_phase_offset = c_start_phase_offset;

    start_phase_offset +=
        data_flow_internal::calculate_phases(dest_1.get(), 0 /* data_offset */, nullptr /* dest */, start_phase_offset);

    start_phase_offset +=
        data_flow_internal::calculate_phases(dest_1.get(), 0 /* data_offset */, nullptr /* dest */, start_phase_offset);

    start_phase_offset +=
        data_flow_internal::calculate_phases(dest_2.get(), 0 /* data_offset */, nullptr /* dest */, start_phase_offset);

    // Verify all sending / receiving phases on all data flow nodes.
    // src_node_1
    verify_phases(
        src_node_1->get_sending_phases(fork_1_1.get()),
        {PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        src_node_1->get_sending_phases(fork_1_2.get()),
        {PhaseInfo(c_start_phase_offset + 6, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        src_node_1->get_sending_phases(fork_1_3.get()),
        {PhaseInfo(c_start_phase_offset + 11, 0 /* data_offset */, 5 /* num_msgs */)});

    // src_node_2
    verify_phases(
        src_node_2->get_sending_phases(fork_2_1.get()),
        {PhaseInfo(c_start_phase_offset + 1, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        src_node_2->get_sending_phases(fork_2_2.get()),
        {PhaseInfo(c_start_phase_offset + 5, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        src_node_2->get_sending_phases(fork_2_3.get()),
        {PhaseInfo(c_start_phase_offset + 10, 0 /* data_offset */, 5 /* num_msgs */)});

    // fork_1_1
    verify_phases(
        fork_1_1->get_receiving_phases_per_input_group().at(0), {PhaseInfo(c_start_phase_offset, 5 /* num_msgs */)});
    verify_phases(
        fork_1_1->get_sending_phases(gather_1.get()),
        {PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 15 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 4, 40 /* data_offset */, 5 /* num_msgs */)});

    // fork_1_2
    verify_phases(
        fork_1_2->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(c_start_phase_offset + 6, 5 /* num_msgs */)});
    verify_phases(
        fork_1_2->get_sending_phases(gather_2.get()),
        {PhaseInfo(c_start_phase_offset + 6, 10 /* data_offset */, 10 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 7, 0 /* data_offset */, 5 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 8, 0 /* data_offset */, 5 /* num_msgs */)});

    // fork_1_3
    verify_phases(
        fork_1_3->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(c_start_phase_offset + 11, 5 /* num_msgs */)});
    verify_phases(
        fork_1_3->get_sending_phases(gather_3.get()),
        {
            PhaseInfo(c_start_phase_offset + 11, 10 /* data_offset */, 5 /* num_msgs */),
        });

    // fork_2_1
    verify_phases(
        fork_2_1->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(c_start_phase_offset + 1, 5 /* num_msgs */)});
    verify_phases(
        fork_2_1->get_sending_phases(gather_1.get()),
        {PhaseInfo(c_start_phase_offset + 1, 10 /* data_offset*/, 10 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 2, 0 /* data_offset */, 5 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 3, 0 /* data_offset */, 5 /* num_msgs */)});

    // fork_2_2
    verify_phases(
        fork_2_2->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(c_start_phase_offset + 5, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        fork_2_2->get_sending_phases(gather_2.get()),
        {PhaseInfo(c_start_phase_offset + 5, 0 /* data_offset */, 15 /* num_msgs */),
         PhaseInfo(c_start_phase_offset + 9, 40 /* data_offset */, 5 /* num_msgs */)});

    // fork_2_3
    verify_phases(
        fork_2_3->get_receiving_phases_per_input_group().at(0),
        {PhaseInfo(c_start_phase_offset + 10, 0 /* data_offset */, 5 /* num_msgs */)});
    verify_phases(
        fork_2_3->get_sending_phases(gather_3.get()),
        {
            PhaseInfo(c_start_phase_offset + 10, 10 /* num_msgs */),
        });

    // gather_1
    verify_phases(
        gather_1->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset, 15 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 1, 20 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 4, 5 /* num_msgs */),
        });
    verify_phases(
        gather_1->get_sending_phases(union_node.get()),
        {
            PhaseInfo(c_start_phase_offset, 40 /* num_msgs */),
        });

    // gather_2
    verify_phases(
        gather_2->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset + 5, 15 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 6, 20 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 9, 5 /* num_msgs */),
        });
    verify_phases(
        gather_2->get_sending_phases(union_node.get()),
        {
            PhaseInfo(c_start_phase_offset + 5, 40 /* num_msgs */),
        });

    // gather_3
    verify_phases(
        gather_3->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset + 10, 10 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 11, 5 /* num_msgs */),
        });
    verify_phases(
        gather_3->get_sending_phases(dest_2.get()),
        {
            PhaseInfo(c_start_phase_offset + 10, 15 /* num_msgs */),
        });

    // union_node
    verify_phases(
        union_node->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset, 40 /* num_msgs */),
        });
    verify_phases(
        union_node->get_receiving_phases_per_input_group().at(1),
        {
            PhaseInfo(c_start_phase_offset + 5, 40 /* num_msgs */),
        });
    verify_phases(
        union_node->get_sending_phases(dest_1.get()),
        {
            PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 40 /* num_msgs */),
            PhaseInfo(c_start_phase_offset + 5, 0 /* data_offset */, 40 /* num_msgs */),
        });

    // dest_1
    verify_phases(
        dest_1->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset, 40 /* num_msgs */),
        });
    verify_phases(
        dest_1->get_receiving_phases_per_input_group().at(1),
        {
            PhaseInfo(c_start_phase_offset + 5, 40 /* num_msgs */),
        });

    // dest_2
    verify_phases(
        dest_2->get_receiving_phases_per_input_group().at(0),
        {
            PhaseInfo(c_start_phase_offset + 10, 15 /* num_msgs */),
        });
}