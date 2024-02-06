// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/subgraph_leaf_groups_finder_internal.h"

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
using ::testing::Key;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_leaf_subgraphs
**********************************************************************************************************************/

class Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
        m_subgraph_id = c_start_subgraph_id;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock()
    {
        return create_and_configure_df_node_mock(DataFlowType::Serial, 1 /* num_root_to_leaf_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock()
    {
        return create_and_configure_df_node_mock(DataFlowType::ParallelCopy, 1 /* num_root_to_leaf_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_parallel_fork_df_node_mock()
    {
        return create_and_configure_df_node_mock(DataFlowType::Parallel, 1 /* num_root_to_leaf_paths */);
    }

    std::unique_ptr<DataFlowNodeMock> make_serial_fork_df_node_mock(unsigned int num_root_to_leaf_paths)
    {
        return create_and_configure_df_node_mock(DataFlowType::Serial, num_root_to_leaf_paths);
    }

    std::unique_ptr<DataFlowNodeMock> create_and_configure_df_node_mock(
        DataFlowType data_flow_type,
        unsigned int num_root_to_leaf_paths)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();

        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_dataflow_type()).WillByDefault(Return(data_flow_type));
        ON_CALL(*df_node_mock, get_num_root_to_leaf_paths_in_subgraph()).WillByDefault(Return(num_root_to_leaf_paths));

        return df_node_mock;
    }

    int m_subgraph_id;
    NodeId m_node_id;
    std::unordered_map<unsigned int, unsigned int> m_max_root_to_leaf_paths_per_subgraph;

    constexpr static int c_start_subgraph_id = 0;
};

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, DirectPathUnicast)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> mid1_df_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> mid2_df_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, mid1_df_node),
        make_df_edge(mid1_df_node, mid2_df_node),
        make_df_edge(mid2_df_node, leaf_df_node)
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_TRUE(leaf_df_node->has_assigned_leaf_subgraph_id());
    EXPECT_EQ(leaf_df_node->get_leaf_subgraph_id(), c_start_subgraph_id);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id);

    EXPECT_THAT(m_max_root_to_leaf_paths_per_subgraph, Contains(Key(c_start_subgraph_id)));
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.at(c_start_subgraph_id), 1);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, DirectPathMulticast)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> mid_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3 = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, mid_df_node),
        make_df_edge(mid_df_node, leaf_df_node_1),
        make_df_edge(mid_df_node, leaf_df_node_2),
        make_df_edge(mid_df_node, leaf_df_node_3)
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_EQ(leaf_df_node_1->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_2->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_3->get_leaf_subgraph_id(), c_start_subgraph_id);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id);

    EXPECT_THAT(m_max_root_to_leaf_paths_per_subgraph, Contains(Key(c_start_subgraph_id)));
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.at(c_start_subgraph_id), 1);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, DataFlowNodeAlreadyProcessed)
{
    std::unique_ptr<DataFlowNodeMock> data_flow_node = make_df_node_mock();

    const int df_node_subgraph_id = 55;
    data_flow_node->set_leaf_subgraph_id(df_node_subgraph_id);

    data_flow_internal::find_leaf_subgraphs(data_flow_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_TRUE(data_flow_node->has_assigned_leaf_subgraph_id());
    EXPECT_EQ(data_flow_node->get_leaf_subgraph_id(), df_node_subgraph_id);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, MultipleRootNodesDoesntOverrideLeafSubgraph)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_df_node_3 = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> gather_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node_1, gather_df_node),
        make_df_edge(root_df_node_2, gather_df_node),
        make_df_edge(root_df_node_3, gather_df_node),
        make_df_edge(gather_df_node, leaf_df_node)
    });

    data_flow_internal::find_leaf_subgraphs(
        root_df_node_1.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    int discarded_subgraph_id = 44;
    data_flow_internal::find_leaf_subgraphs(
        root_df_node_2.get(), discarded_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);
    data_flow_internal::find_leaf_subgraphs(
        root_df_node_3.get(), discarded_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_TRUE(leaf_df_node->has_assigned_leaf_subgraph_id());
    EXPECT_EQ(leaf_df_node->get_leaf_subgraph_id(), m_subgraph_id);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, ParallelFork)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> pf_df_node_1 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_2 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_3 = make_parallel_fork_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3 = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, pf_df_node_1),
        make_df_edge(pf_df_node_1, leaf_df_node_1),
        make_df_edge(root_df_node, pf_df_node_2),
        make_df_edge(pf_df_node_2, leaf_df_node_2),
        make_df_edge(root_df_node, pf_df_node_3),
        make_df_edge(pf_df_node_3, leaf_df_node_3)
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_EQ(leaf_df_node_1->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_2->get_leaf_subgraph_id(), c_start_subgraph_id + 1);
    EXPECT_EQ(leaf_df_node_3->get_leaf_subgraph_id(), c_start_subgraph_id + 2);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id + 3);
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.size(), 3);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, ParallelForkMulticast)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> pf_df_node_1 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_2 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_3 = make_parallel_fork_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_2 = make_leaf_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_2 = make_leaf_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3_2 = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, pf_df_node_1),
        make_df_edge(pf_df_node_1, leaf_df_node_1_1),
        make_df_edge(pf_df_node_1, leaf_df_node_1_2),
        make_df_edge(root_df_node, pf_df_node_2),
        make_df_edge(pf_df_node_2, leaf_df_node_2_1),
        make_df_edge(pf_df_node_2, leaf_df_node_2_2),
        make_df_edge(root_df_node, pf_df_node_3),
        make_df_edge(pf_df_node_3, leaf_df_node_3_1),
        make_df_edge(pf_df_node_3, leaf_df_node_3_2)
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_EQ(leaf_df_node_1_1->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_1_2->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_2_1->get_leaf_subgraph_id(), c_start_subgraph_id + 1);
    EXPECT_EQ(leaf_df_node_2_2->get_leaf_subgraph_id(), c_start_subgraph_id + 1);
    EXPECT_EQ(leaf_df_node_3_1->get_leaf_subgraph_id(), c_start_subgraph_id + 2);
    EXPECT_EQ(leaf_df_node_3_2->get_leaf_subgraph_id(), c_start_subgraph_id + 2);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id + 3);
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.size(), 3);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, SerialFork)
{
    const unsigned int num_root_to_leaf_paths = 7;

    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> sf_df_node_1 = make_serial_fork_df_node_mock(num_root_to_leaf_paths);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_2 = make_serial_fork_df_node_mock(num_root_to_leaf_paths);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_3 = make_serial_fork_df_node_mock(num_root_to_leaf_paths);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_4 = make_serial_fork_df_node_mock(num_root_to_leaf_paths);

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_4 = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, sf_df_node_1),
        make_df_edge(root_df_node, sf_df_node_2),
        make_df_edge(root_df_node, sf_df_node_3),
        make_df_edge(root_df_node, sf_df_node_4),
        make_df_edge(sf_df_node_1, leaf_df_node_1),
        make_df_edge(sf_df_node_2, leaf_df_node_2),
        make_df_edge(sf_df_node_3, leaf_df_node_3),
        make_df_edge(sf_df_node_4, leaf_df_node_4),
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_EQ(leaf_df_node_1->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_2->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_3->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_4->get_leaf_subgraph_id(), c_start_subgraph_id);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id);
    EXPECT_THAT(m_max_root_to_leaf_paths_per_subgraph, Contains(Key(c_start_subgraph_id)));
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.at(c_start_subgraph_id), num_root_to_leaf_paths);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafSubgraphs, ParallelForkSerialFork)
{
    const unsigned int num_root_to_leaf_paths_fork_1 = 3;
    const unsigned int num_root_to_leaf_paths_fork_2 = 5;

    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> pf_df_node_1 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_2 = make_parallel_fork_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_1 = make_serial_fork_df_node_mock(num_root_to_leaf_paths_fork_1);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_2 = make_serial_fork_df_node_mock(num_root_to_leaf_paths_fork_1);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_3 = make_serial_fork_df_node_mock(num_root_to_leaf_paths_fork_1);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_2_1 = make_serial_fork_df_node_mock(num_root_to_leaf_paths_fork_2);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_2_2 = make_serial_fork_df_node_mock(num_root_to_leaf_paths_fork_2);

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_2 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_3 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_1 = make_leaf_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_2 = make_leaf_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_df_node, pf_df_node_1),
        make_df_edge(root_df_node, pf_df_node_2),
        make_df_edge(pf_df_node_1, sf_df_node_1_1),
        make_df_edge(pf_df_node_1, sf_df_node_1_2),
        make_df_edge(pf_df_node_1, sf_df_node_1_3),
        make_df_edge(pf_df_node_2, sf_df_node_2_1),
        make_df_edge(pf_df_node_2, sf_df_node_2_2),
        make_df_edge(sf_df_node_1_1, leaf_df_node_1_1),
        make_df_edge(sf_df_node_1_2, leaf_df_node_1_2),
        make_df_edge(sf_df_node_1_3, leaf_df_node_1_3),
        make_df_edge(sf_df_node_2_1, leaf_df_node_2_1),
        make_df_edge(sf_df_node_2_2, leaf_df_node_2_2)
    });

    data_flow_internal::find_leaf_subgraphs(root_df_node.get(), m_subgraph_id, m_max_root_to_leaf_paths_per_subgraph);

    EXPECT_EQ(leaf_df_node_1_1->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_1_2->get_leaf_subgraph_id(), c_start_subgraph_id);
    EXPECT_EQ(leaf_df_node_1_3->get_leaf_subgraph_id(), c_start_subgraph_id);

    EXPECT_EQ(leaf_df_node_2_1->get_leaf_subgraph_id(), c_start_subgraph_id + 1);
    EXPECT_EQ(leaf_df_node_2_2->get_leaf_subgraph_id(), c_start_subgraph_id + 1);

    EXPECT_EQ(m_subgraph_id, c_start_subgraph_id + 2);
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.at(c_start_subgraph_id), num_root_to_leaf_paths_fork_1);
    EXPECT_EQ(m_max_root_to_leaf_paths_per_subgraph.at(c_start_subgraph_id + 1), num_root_to_leaf_paths_fork_2);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_leaf_groups_per_subgraph
**********************************************************************************************************************/

class Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock()
    {
        return create_and_configure_df_node_mock(c_unassigned_leaf_subgraph_id,
                                                 std::nullopt /* root_to_leaf_path_index */);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(int leaf_subgraph_id)
    {
        return create_and_configure_df_node_mock(leaf_subgraph_id,
                                                 std::nullopt /* root_to_leaf_path_index */);
    }

    std::unique_ptr<DataFlowNodeMock> make_parallel_fork_df_node_mock()
    {
        return make_df_node_mock();
    }

    std::unique_ptr<DataFlowNodeMock> make_serial_fork_df_node_mock(unsigned int root_to_leaf_path_index)
    {
        return create_and_configure_df_node_mock(c_unassigned_leaf_subgraph_id,
                                                 std::make_optional(root_to_leaf_path_index));
    }

    std::unique_ptr<DataFlowNodeMock> create_and_configure_df_node_mock(
        int leaf_subgraph_id,
        std::optional<unsigned int> root_to_leaf_path_index)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();

        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_root_to_leaf_path_index()).WillByDefault(Return(root_to_leaf_path_index));

        df_node_mock->set_leaf_subgraph_id(leaf_subgraph_id);
        df_node_mock->set_number_of_unique_df_paths(1);

        return df_node_mock;
    }

    NodeId m_node_id;
    std::unordered_map<DataFlowNode*, unsigned int> m_node_visit_count;

    constexpr static int c_unassigned_leaf_subgraph_id = -1;
};

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph, DataFlowNodeAlreadyProcessed)
{
    std::unique_ptr<DataFlowNodeMock> data_flow_node = make_df_node_mock();

    std::unordered_map<unsigned int, unsigned int> root_to_leaf_paths_per_subgrpah = { {0u, 1u} };
    SubgraphLeafGroups subgraph_leaf_groups(root_to_leaf_paths_per_subgrpah);
    m_node_visit_count = { {data_flow_node.get(), 1u} };

    data_flow_internal::find_leaf_groups_per_subgraph(
        data_flow_node.get(), m_node_visit_count, 0 /* root_to_leaf_path_index */, &subgraph_leaf_groups);

    EXPECT_EQ(m_node_visit_count.at(data_flow_node.get()), 1);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph, LeafNodeDoesntHaveSubgraphIDAssigned)
{
    std::unique_ptr<DataFlowNodeMock> leaf_df_node = make_leaf_df_node_mock(c_unassigned_leaf_subgraph_id);

    verify_log_assert(
        [&]()
        {
            data_flow_internal::find_leaf_groups_per_subgraph(
                leaf_df_node.get(), m_node_visit_count,
                0 /* root_to_leaf_path_index */, nullptr /* subgraph_leaf_groups */);
        },
        "^Expecting leaf node [0-9]+ to have leaf subgraph id assigned.*");
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph, MultipleLeafNodesInGroup)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> mid_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_3 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_4 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_5 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);

    connect_data_flow_graph({
        make_df_edge(root_df_node, mid_df_node),
        make_df_edge(mid_df_node, leaf_df_node_1),
        make_df_edge(mid_df_node, leaf_df_node_2),
        make_df_edge(mid_df_node, leaf_df_node_3),
        make_df_edge(mid_df_node, leaf_df_node_4),
        make_df_edge(mid_df_node, leaf_df_node_5)
    });

    std::unordered_map<unsigned int, unsigned int> root_to_leaf_paths_per_subgrpah = {{ 0u, 1u }};
    SubgraphLeafGroups subgraph_leaf_groups(root_to_leaf_paths_per_subgrpah);

    data_flow_internal::find_leaf_groups_per_subgraph(
        root_df_node.get(), m_node_visit_count, 0 /* root_to_leaf_path_index */, &subgraph_leaf_groups);

    std::unordered_map<unsigned int, LeafGroupOrder> subgraph_leaves = subgraph_leaf_groups.get_subgraph_leaves();

    EXPECT_EQ(subgraph_leaves.size(), 1);
    EXPECT_THAT(subgraph_leaves, Contains(Key(0)));

    LeafGroupOrder subgraph_0_leaf_groups = subgraph_leaves.at(0);
    EXPECT_EQ(subgraph_0_leaf_groups.get_leaf_groups().size(), 1);

    EXPECT_THAT(
        subgraph_0_leaf_groups.get_leaf_groups()[0].get_leaf_nodes(),
        UnorderedElementsAre(
            leaf_df_node_1.get(),
            leaf_df_node_2.get(),
            leaf_df_node_3.get(),
            leaf_df_node_4.get(),
            leaf_df_node_5.get()));
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph, MultipleUniqueDFPathsThroughNode)
{
    std::unique_ptr<DataFlowNodeMock> root_df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_df_node_3 = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> union_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> leaf_df_node = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);

    connect_data_flow_graph({
        make_df_edge(root_df_node_1, union_df_node),
        make_df_edge(root_df_node_2, union_df_node),
        make_df_edge(root_df_node_3, union_df_node),
        make_df_edge(union_df_node, leaf_df_node)
    });

    union_df_node->set_number_of_unique_df_paths(3);
    leaf_df_node->set_number_of_unique_df_paths(3);

    std::unordered_map<unsigned int, unsigned int> root_to_leaf_paths_per_subgrpah = {{ 0u, 3u }};
    SubgraphLeafGroups subgraph_leaf_groups(root_to_leaf_paths_per_subgrpah);

    data_flow_internal::find_leaf_groups_per_subgraph(
        root_df_node_1.get(), m_node_visit_count, 0 /* root_to_leaf_path_index */, &subgraph_leaf_groups);
    EXPECT_EQ(m_node_visit_count.at(leaf_df_node.get()), 1);

    data_flow_internal::find_leaf_groups_per_subgraph(
        root_df_node_2.get(), m_node_visit_count, 1 /* root_to_leaf_path_index */, &subgraph_leaf_groups);
    EXPECT_EQ(m_node_visit_count.at(leaf_df_node.get()), 2);

    data_flow_internal::find_leaf_groups_per_subgraph(
        root_df_node_3.get(), m_node_visit_count, 2 /* root_to_leaf_path_index */, &subgraph_leaf_groups);
    EXPECT_EQ(m_node_visit_count.at(leaf_df_node.get()), 3);

    std::unordered_map<unsigned int, LeafGroupOrder> subgraph_leaves = subgraph_leaf_groups.get_subgraph_leaves();

    EXPECT_EQ(subgraph_leaves.size(), 1);
    EXPECT_THAT(subgraph_leaves, Contains(Key(0)));

    LeafGroupOrder subgraph_0_leaf_groups = subgraph_leaves.at(0);
    EXPECT_EQ(subgraph_0_leaf_groups.get_leaf_groups().size(), 3);

    EXPECT_THAT(subgraph_0_leaf_groups.get_leaf_groups()[0].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node.get()));
    EXPECT_THAT(subgraph_0_leaf_groups.get_leaf_groups()[1].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node.get()));
    EXPECT_THAT(subgraph_0_leaf_groups.get_leaf_groups()[2].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node.get()));
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_FindLeafGroupsPerSubgraph, ParallelForkSerialFork)
{
    const unsigned int num_root_to_leaf_paths_0 = 3;
    const unsigned int num_root_to_leaf_paths_1 = 5;

    std::unique_ptr<DataFlowNodeMock> root_df_node = make_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> pf_df_node_1 = make_parallel_fork_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> pf_df_node_2 = make_parallel_fork_df_node_mock();

    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_1 = make_serial_fork_df_node_mock(0 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_2 = make_serial_fork_df_node_mock(1 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_1_3 = make_serial_fork_df_node_mock(2 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_2_1 = make_serial_fork_df_node_mock(1 /* root_to_leaf_path_index */);
    std::unique_ptr<DataFlowNodeMock> sf_df_node_2_2 = make_serial_fork_df_node_mock(4 /* root_to_leaf_path_index */);

    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_1 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_2 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_1_3 = make_leaf_df_node_mock(0 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_1 = make_leaf_df_node_mock(1 /* leaf_subgraph_id */);
    std::unique_ptr<DataFlowNodeMock> leaf_df_node_2_2 = make_leaf_df_node_mock(1 /* leaf_subgraph_id */);

    connect_data_flow_graph({
        make_df_edge(root_df_node, pf_df_node_1),
        make_df_edge(root_df_node, pf_df_node_2),
        make_df_edge(pf_df_node_1, sf_df_node_1_1),
        make_df_edge(pf_df_node_1, sf_df_node_1_2),
        make_df_edge(pf_df_node_1, sf_df_node_1_3),
        make_df_edge(pf_df_node_2, sf_df_node_2_1),
        make_df_edge(pf_df_node_2, sf_df_node_2_2),
        make_df_edge(sf_df_node_1_1, leaf_df_node_1_1),
        make_df_edge(sf_df_node_1_2, leaf_df_node_1_2),
        make_df_edge(sf_df_node_1_3, leaf_df_node_1_3),
        make_df_edge(sf_df_node_2_1, leaf_df_node_2_1),
        make_df_edge(sf_df_node_2_2, leaf_df_node_2_2)
    });

    std::unordered_map<unsigned int, unsigned int> root_to_leaf_paths_per_subgrpah = {
        { 0u, num_root_to_leaf_paths_0 }, { 1u, num_root_to_leaf_paths_1 }};
    SubgraphLeafGroups subgraph_leaf_groups(root_to_leaf_paths_per_subgrpah);

    data_flow_internal::find_leaf_groups_per_subgraph(
        root_df_node.get(), m_node_visit_count, 0 /* root_to_leaf_path_index */, &subgraph_leaf_groups);

    std::unordered_map<unsigned int, LeafGroupOrder> subgraph_leaves = subgraph_leaf_groups.get_subgraph_leaves();

    EXPECT_EQ(subgraph_leaves.size(), 2);
    EXPECT_THAT(subgraph_leaves, Contains(Key(0)));
    EXPECT_THAT(subgraph_leaves, Contains(Key(1)));

    LeafGroupOrder subgraph_0_leaf_groups = subgraph_leaves.at(0);
    LeafGroupOrder subgraph_1_leaf_groups = subgraph_leaves.at(1);

    EXPECT_EQ(subgraph_0_leaf_groups.get_leaf_groups().size(), num_root_to_leaf_paths_0);
    EXPECT_EQ(subgraph_1_leaf_groups.get_leaf_groups().size(), num_root_to_leaf_paths_1);

    EXPECT_THAT(
        subgraph_0_leaf_groups.get_leaf_groups()[0].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node_1_1.get()));
    EXPECT_THAT(
        subgraph_0_leaf_groups.get_leaf_groups()[1].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node_1_2.get()));
    EXPECT_THAT(
        subgraph_0_leaf_groups.get_leaf_groups()[2].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node_1_3.get()));

    EXPECT_THAT(
        subgraph_1_leaf_groups.get_leaf_groups()[1].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node_2_1.get()));
    EXPECT_THAT(
        subgraph_1_leaf_groups.get_leaf_groups()[4].get_leaf_nodes(), UnorderedElementsAre(leaf_df_node_2_2.get()));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::did_process_node_subtree
**********************************************************************************************************************/

class Pipegen2_SubgraphLeafGroupsFinderInternal_DidProcessNodeSubtree : public testing::Test
{
protected:
    void SetUp() override
    {
        NodeId node_id = 100;
        m_data_flow_node = std::make_unique<NiceMock<DataFlowNodeMock>>();
        m_data_flow_node->set_number_of_unique_df_paths(m_number_of_unique_df_paths);
    }

    std::unique_ptr<DataFlowNodeMock> m_data_flow_node;
    std::unordered_map<DataFlowNode*, unsigned int> m_node_visit_count;
    unsigned int m_number_of_unique_df_paths = 5;
};

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_DidProcessNodeSubtree, NodeDoesntExistInMap)
{
    EXPECT_FALSE(data_flow_internal::did_process_node_subtree(m_data_flow_node.get(), m_node_visit_count));
    EXPECT_THAT(m_node_visit_count, Contains(Key(m_data_flow_node.get())));
    EXPECT_EQ(m_node_visit_count.at(m_data_flow_node.get()), 0);
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_DidProcessNodeSubtree, DidNotProcess)
{
    m_node_visit_count[m_data_flow_node.get()] = m_number_of_unique_df_paths - 1;

    EXPECT_FALSE(data_flow_internal::did_process_node_subtree(m_data_flow_node.get(), m_node_visit_count));
}

TEST_F(Pipegen2_SubgraphLeafGroupsFinderInternal_DidProcessNodeSubtree, DidProcess)
{
    m_node_visit_count[m_data_flow_node.get()] = m_number_of_unique_df_paths;

    EXPECT_TRUE(data_flow_internal::did_process_node_subtree(m_data_flow_node.get(), m_node_visit_count));
}