// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfers_calculator.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "model/data_flow/subgraph_leaf_groups.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::Contains;
using ::testing::Each;
using ::testing::Key;
using ::testing::Lt;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;

/**********************************************************************************************************************
    Tests for function: TransfersCalculator::calculate_phases
**********************************************************************************************************************/

class Pipegen2_TransfersCalculator_CalculatePhases : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int tiles_to_send,
        unsigned int max_tiles_per_phase,
        unsigned int input_group_count = 1,
        unsigned int leaf_subgraph_id = -1,
        unsigned int number_of_unique_df_paths = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));

        df_node_mock->set_tiles_to_send(tiles_to_send);
        df_node_mock->set_max_num_tiles_per_phase(max_tiles_per_phase);
        df_node_mock->set_leaf_subgraph_id(leaf_subgraph_id);
        df_node_mock->set_number_of_unique_df_paths(number_of_unique_df_paths);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase, 1 /* input_group_count */,
                                 -1 /* leaf_subgraph_id */, 1 /* number_of_unique_df_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_union_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase, unsigned int input_group_count)
    {
        // For union node number of unique data flow paths will be the input group count.
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase, input_group_count,
                                 -1 /* leaf_subgraph_id */, input_group_count /* number_of_unique_df_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_parallel_fork_df_node_mock(
        unsigned int tiles_to_send, unsigned int max_tiles_per_phase)
    {
        return make_df_node_mock(tiles_to_send, max_tiles_per_phase, 1 /* input_group_count */,
                                 -1 /* leaf_subgraph_id */, 1 /* number_of_unique_df_paths */);
    }


    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int max_tiles_per_phase,
        unsigned int leaf_subgraph_id,
        unsigned int input_group_count = 1,
        const std::vector<unsigned int>& root_to_leaf_path_indexes = {0},
        unsigned int num_root_to_leaf_paths = 1,
        unsigned int number_of_unique_df_paths = 1)
    {
        // Assert that every passed index of the root to leaf path which goes through this node will be less than the
        // total number of paths that go rhough this node, to ensure the correct test setup. The error here indicates
        // a test setup error and not production code error, but using gtest asserts for convenience.
        EXPECT_THAT(root_to_leaf_path_indexes, Each(Lt(num_root_to_leaf_paths)));

        m_num_root_to_leaf_paths_per_subgraph[leaf_subgraph_id] = std::max(
            num_root_to_leaf_paths, m_num_root_to_leaf_paths_per_subgraph[leaf_subgraph_id]);

        std::unique_ptr<DataFlowNodeMock> df_node = make_df_node_mock(
            0 /* tiles_to_send */, max_tiles_per_phase, input_group_count, leaf_subgraph_id, number_of_unique_df_paths);
        m_node_to_root_to_leaf_path_indexes[df_node.get()] = root_to_leaf_path_indexes;

        return df_node;
    }

    // Inserts empty leaf group to be added when subgraph leaf groups object is created. Empty leaf groups can be the
    // of multiple paths gathering to a single node, so the subgraph id of the second root node will not get propagated
    // to the leafs (as the gather node will already be visited on the path starting from the first node), and thus
    // we will end up with empty leaf group.
    void insert_empty_leaf_group(unsigned int leaf_subgraph_id)
    {
        // Assert here indicates an error in test setup as we are trying to override the leaf subgraph id.
        EXPECT_THAT(m_num_root_to_leaf_paths_per_subgraph, Not(Contains(Key(leaf_subgraph_id))));

        m_num_root_to_leaf_paths_per_subgraph[leaf_subgraph_id] = 1 /* irrelevant */;
    }

    // Creates a subgraph leaf groups objects based on the configuration of the created leaf nodes in the test setup.
    std::unique_ptr<SubgraphLeafGroups> make_subgraph_leaf_groups()
    {
        std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups = std::make_unique<SubgraphLeafGroups>(
            m_num_root_to_leaf_paths_per_subgraph);

        for (auto& [df_node, root_to_leaf_path_indexes] : m_node_to_root_to_leaf_path_indexes)
        {
            for (unsigned int root_to_leaf_path_index : root_to_leaf_path_indexes)
            {
                subgraph_leaf_groups->add_leaf_node(df_node, root_to_leaf_path_index);
            }
        }

        return subgraph_leaf_groups;
    }

    constexpr static unsigned int c_start_phase_offset = 0;

    NodeId m_node_id;

    // Holds mapping between every leaf node and its list of root to path indexes which will be used to place the
    // leaf node to the appropriate location when creating subgraph leaf groups object.
    std::unordered_map<DataFlowNode*, std::vector<unsigned int>> m_node_to_root_to_leaf_path_indexes;

    // Holds mapping between a subgraph leaf group id and the max number of root to leaf paths that go through a single
    // leaf node in that subgraph, used to initialize subgraph leaf groups object.
    std::unordered_map<unsigned int, unsigned int> m_num_root_to_leaf_paths_per_subgraph;
};

TEST_F(Pipegen2_TransfersCalculator_CalculatePhases, SubgraphLeafClusterWithEmptyLeafGroups)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_root_df_node_mock(
        3 /* tiles_to_send */, 2000 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_1 = make_parallel_fork_df_node_mock(
        3 /* tiles_to_send */, 2000 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> parallel_fork_node_2 = make_parallel_fork_df_node_mock(
        3 /* tiles_to_send */, 200 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_leaf_df_node_mock(
        2000 /* max_tiles_per_phase */,
        0 /* leaf_subgraph_id */,
        1 /* input_group_count */,
        {0} /* root_to_leaf_path_indexes */,
        1 /* num_root_to_leaf_paths */,
        1 /* number_of_unique_df_paths */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 = make_leaf_df_node_mock(
        2000 /* max_tiles_per_phase */,
        2 /* leaf_subgraph_id */,
        1 /* input_group_count */,
        {0} /* root_to_leaf_path_indexes */,
        1 /* num_root_to_leaf_paths */,
        1 /* number_of_unique_df_paths */);

    connect_data_flow_graph({
        make_df_edge(root_node, parallel_fork_node_1),
        make_df_edge(root_node, parallel_fork_node_2),

        make_df_edge(parallel_fork_node_1, leaf_node_1, {0, 3, 9} /* offsets */),
        make_df_edge(parallel_fork_node_2, leaf_node_2, {9, 0} /* offsets */)
    });

    // Subgraph 0 is leaf_node_1, subgraph 1 is empty, subgraph 2 is leaf_node_2.
    insert_empty_leaf_group(1 /* leaf_subgraph_id */);
    std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups = make_subgraph_leaf_groups();

    TransfersCalculator transfers_calculator;
    transfers_calculator.calculate_phases(subgraph_leaf_groups.get());

    // Verify sending phases on parallel forks.
    const std::vector<PhaseInfo>& pf_1_sending_phases = parallel_fork_node_1->get_sending_phases(leaf_node_1.get());
    verify_phases(pf_1_sending_phases, {
        PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 6 /* num_msgs */),
        PhaseInfo(c_start_phase_offset + 1, 9 /* data_offset */, 3 /* num_msgs */)
    });

    const std::vector<PhaseInfo>& pf_2_sending_phases = parallel_fork_node_2->get_sending_phases(leaf_node_2.get());
    verify_phases(pf_2_sending_phases, {
        PhaseInfo(c_start_phase_offset, 9 /* data_offset */, 3 /* num_msgs */),
        PhaseInfo(c_start_phase_offset + 1, 0 /* data_offset */, 3 /* num_msgs */)
    });

    // Verify receiving phases on leaf nodes.
    const std::map<unsigned int, std::vector<PhaseInfo>>& leaf_1_receiving_phases =
        leaf_node_1->get_receiving_phases_per_input_group();
    EXPECT_EQ(leaf_1_receiving_phases.size(), 1);
    EXPECT_THAT(leaf_1_receiving_phases, Contains(Key(0)));
    verify_phases(leaf_1_receiving_phases.at(0), { PhaseInfo(c_start_phase_offset, 9 /* num_msgs */) });

    const std::map<unsigned int, std::vector<PhaseInfo>>& leaf_2_receiving_phases =
        leaf_node_2->get_receiving_phases_per_input_group();
    EXPECT_EQ(leaf_2_receiving_phases.size(), 1);
    EXPECT_THAT(leaf_2_receiving_phases, Contains(Key(0)));
    verify_phases(leaf_2_receiving_phases.at(0), { PhaseInfo(c_start_phase_offset, 6 /* num_msgs */) });
}

TEST_F(Pipegen2_TransfersCalculator_CalculatePhases, PhasesFromFirstLeafCopiedToAllMulticastDests)
{
    std::unique_ptr<DataFlowNodeMock> root_node_1 = make_root_df_node_mock(
        2 /* tiles_to_send */, 2000 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> root_node_2 = make_root_df_node_mock(
        2 /* tiles_to_send */, 2000 /* max_tiles_per_phase */);
    std::unique_ptr<DataFlowNodeMock> root_node_3 = make_root_df_node_mock(
        2 /* tiles_to_send */, 2000 /* max_tiles_per_phase */);

    std::unique_ptr<DataFlowNodeMock> union_node = make_union_df_node_mock(
        2 /* tiles_to_send */, 2000 /* max_tiles_per_phase */, 3 /* input_group_count */);

    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_leaf_df_node_mock(
        2000 /* max_tiles_per_phase */,
        0 /* leaf_subgraph_id */,
        1 /* input_group_count */,
        {0, 1, 2} /* root_to_leaf_path_indexes */,
        3 /* num_root_to_leaf_paths */,
        3 /* number_of_unique_df_paths */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 = make_leaf_df_node_mock(
        2000 /* max_tiles_per_phase */,
        0 /* leaf_subgraph_id */,
        1 /* input_group_count */,
        {0, 1, 2} /* root_to_leaf_path_indexes */,
        3 /* num_root_to_leaf_paths */,
        3 /* number_of_unique_df_paths */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_3 = make_leaf_df_node_mock(
        2000 /* max_tiles_per_phase */,
        0 /* leaf_subgraph_id */,
        1 /* input_group_count */,
        {0, 1, 2} /* root_to_leaf_path_indexes */,
        3 /* num_root_to_leaf_paths */,
        3 /* number_of_unique_df_paths */);

    connect_data_flow_graph({
        make_df_edge(root_node_1, union_node),
        make_df_edge(root_node_2, union_node),
        make_df_edge(root_node_3, union_node),

        make_df_edge(union_node, leaf_node_1),
        make_df_edge(union_node, leaf_node_2),
        make_df_edge(union_node, leaf_node_3)
    });

    std::unique_ptr<SubgraphLeafGroups> subgraph_leaf_groups = make_subgraph_leaf_groups();

    TransfersCalculator transfers_calculator;
    transfers_calculator.calculate_phases(subgraph_leaf_groups.get());

    // Verify that union has the same sending phases towards all multicast destinations.
    const std::vector<PhaseInfo>& original_sending_phases = union_node->get_sending_phases(leaf_node_1.get());
    verify_phases(original_sending_phases, {
        PhaseInfo(c_start_phase_offset, 0 /* data_offset */, 2 /* num_msgs */),
        PhaseInfo(c_start_phase_offset + 1, 0 /* data_offset */, 2 /* num_msgs */),
        PhaseInfo(c_start_phase_offset + 2, 0 /* data_offset */, 2 /* num_msgs */)
    });
    verify_phases(original_sending_phases, union_node->get_sending_phases(leaf_node_2.get()));
    verify_phases(original_sending_phases, union_node->get_sending_phases(leaf_node_3.get()));

    // Verify that all multicast destinations have the same receiving phases.
    const std::map<unsigned int, std::vector<PhaseInfo>>& original_receiving_phases_per_input_group =
        leaf_node_1->get_receiving_phases_per_input_group();
    EXPECT_EQ(original_receiving_phases_per_input_group.size(), 3);

    verify_phases(original_receiving_phases_per_input_group.at(0), {
        PhaseInfo(c_start_phase_offset, 2 /* num_msgs */)
    });
    verify_phases(original_receiving_phases_per_input_group.at(1), {
        PhaseInfo(c_start_phase_offset + 1, 2 /* num_msgs */)
    });
    verify_phases(original_receiving_phases_per_input_group.at(2), {
        PhaseInfo(c_start_phase_offset + 2, 2 /* num_msgs */)
    });

    for (const auto& [input_group_idx, original_phases_for_input_group] : original_receiving_phases_per_input_group)
    {
        EXPECT_THAT(leaf_node_2->get_receiving_phases_per_input_group(), Contains(Key(input_group_idx)));
        verify_phases(
            original_phases_for_input_group, leaf_node_2->get_receiving_phases_per_input_group().at(input_group_idx));

        EXPECT_THAT(leaf_node_3->get_receiving_phases_per_input_group(), Contains(Key(input_group_idx)));
        verify_phases(
            original_phases_for_input_group, leaf_node_3->get_receiving_phases_per_input_group().at(input_group_idx));
    }
}