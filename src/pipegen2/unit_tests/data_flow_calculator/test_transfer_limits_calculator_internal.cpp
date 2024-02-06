// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfer_limits_calculator_internal.h"

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

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Key;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

/**********************************************************************************************************************
    Tests for function: data_flow_internal::visit_nodes_in_topological_order
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 1;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock()
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        return df_node_mock;
    }

    void call_visit_nodes_in_topological_order()
    {
        auto visit_func = [this](DataFlowNode* df_node)
        {
            m_topo_sort.push_back(df_node->get_id());
        };

        data_flow_internal::visit_nodes_in_topological_order(m_root_nodes, visit_func, m_visited_nodes);
    }

    NodeId m_node_id;
    std::vector<DataFlowNode*> m_root_nodes;
    std::vector<NodeId> m_topo_sort;
    std::unordered_set<DataFlowNode*> m_visited_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, EmptyDataFlowGraph)
{
    m_root_nodes = {};
    call_visit_nodes_in_topological_order();

    EXPECT_TRUE(m_topo_sort.empty());
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, SingleNode)
{
    std::unique_ptr<DataFlowNodeMock> node_1 = make_df_node_mock();

    m_root_nodes = {node_1.get()};
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(1));
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(node_1.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, EntireGraphVisitedFromSingleRootNode)
{
    std::unique_ptr<DataFlowNodeMock> node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_5 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_6 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_7 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_8 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(node_1, node_2),
        make_df_edge(node_2, node_3),
        make_df_edge(node_1, node_4),
        make_df_edge(node_3, node_5),
        make_df_edge(node_4, node_5),
        make_df_edge(node_5, node_6),
        make_df_edge(node_6, node_7),
        make_df_edge(node_5, node_8)
    });

    m_root_nodes = {node_1.get()};
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(1, 4, 2, 3, 5, 8, 6, 7));
    EXPECT_THAT(
        m_visited_nodes,
        UnorderedElementsAre(node_1.get(), node_2.get(), node_3.get(), node_4.get(),
                             node_5.get(), node_6.get(), node_7.get(), node_8.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, NodeAlreadyVisitedFromDifferentRoot)
{
    std::unique_ptr<DataFlowNodeMock> node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_2 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(node_1, node_2)
    });

    m_root_nodes = {node_1.get()};
    m_visited_nodes.insert(node_2.get());
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(1));
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(node_1.get(), node_2.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, EntieGraphVisitedFromMultipleRootNodes)
{
    std::unique_ptr<DataFlowNodeMock> node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_5 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_6 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_7 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_8 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(node_1, node_4),
        make_df_edge(node_4, node_5),
        make_df_edge(node_3, node_5),
        make_df_edge(node_5, node_6),
        make_df_edge(node_2, node_6),
        make_df_edge(node_6, node_7),
        make_df_edge(node_5, node_8)
    });

    m_root_nodes = {node_3.get(), node_1.get(), node_2.get()};
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(2, 1, 4, 3, 5, 8, 6, 7));
    EXPECT_THAT(
        m_visited_nodes,
        UnorderedElementsAre(node_1.get(), node_2.get(), node_3.get(), node_4.get(),
                             node_5.get(), node_6.get(), node_7.get(), node_8.get()));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_single_source_paths
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(bool is_scatter = false)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));
        m_data_flow_nodes.push_back(df_node_mock.get());

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_scatter_df_node_mock()
    {
        return make_df_node_mock(true /* is_scatter */);
    }

    void verify_all_nodes_on_path(bool expected_single_source_path)
    {
        for (const DataFlowNode* df_node : m_data_flow_nodes)
        {
            EXPECT_EQ(df_node->is_on_single_source_path(), expected_single_source_path);
        }
    }

    NodeId m_node_id;
    std::vector<DataFlowNode*> m_data_flow_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, NodeAlreadyNotOnDirectPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node = make_df_node_mock();
    df_node->set_is_on_single_source_path(false);

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, DirectPathWithNonDirectUpstream_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, df_node_2)
    });

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), false /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, ScatterPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> scatter_node = make_scatter_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(scatter_node, df_node_1),
        make_df_edge(df_node_1, df_node_2)
    });

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(scatter_node.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, SingleNodeGatherPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, gather_node, {0, 2, 4, 6, 8} /* offsets */),
        make_df_edge(gather_node, df_node_2)
    });

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, MultiNodeGatherPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, gather_node),
        make_df_edge(df_node_2, gather_node),
        make_df_edge(gather_node, df_node_3)
    });

    // Start finding direct paths from both rootes.
    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_2.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, DirectPathWithScatterDownstream_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> scatter_node = make_scatter_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, df_node_2),
        make_df_edge(df_node_2, scatter_node),
        make_df_edge(scatter_node, df_node_3)
    });

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, DirectPathWithOneScatterFork_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> scatter_node = make_scatter_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_5 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, df_node_2),
        make_df_edge(df_node_2, df_node_3),
        make_df_edge(df_node_3, scatter_node),
        make_df_edge(scatter_node, df_node_4),
        make_df_edge(df_node_2, df_node_5)
    });

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, NoForkDirectPath_SingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_5 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, df_node_2),
        make_df_edge(df_node_2, df_node_3),
        make_df_edge(df_node_3, df_node_4),
        make_df_edge(df_node_4, df_node_5)
    });

    EXPECT_TRUE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(true /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, DirectForkPath_Direct)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_5 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(df_node_1, df_node_2),
        make_df_edge(df_node_2, df_node_3),
        make_df_edge(df_node_2, df_node_4),
        make_df_edge(df_node_4, df_node_5)
    });

    EXPECT_TRUE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(true /* expected_single_source_path */);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_tiles_to_send
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(unsigned int input_group_count = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(unsigned int epoch_tiles, unsigned int num_iterations)
    {
        assert(epoch_tiles % num_iterations == 0);

        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_num_epoch_tiles()).WillByDefault(Return(epoch_tiles));
        df_node_mock->set_num_iterations(num_iterations);

        return df_node_mock;
    }

    NodeId m_node_id;
    std::unordered_set<DataFlowNode*> m_visited_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, RootTilesToSendInitAndPropagatesSingleInput)
{
    std::unique_ptr<DataFlowNodeMock> root = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, leaf)
    });

    data_flow_internal::calculate_tiles_to_send(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_tiles_to_send(), 250);
    EXPECT_EQ(leaf->get_tiles_to_send(), 250);
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(root.get(), leaf.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, RootTilesToSendInitAndPropagatesMultiInput)
{
    std::unique_ptr<DataFlowNodeMock> root = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, leaf, {0, 2, 4, 6, 8, 10} /* offsets */)
    });

    data_flow_internal::calculate_tiles_to_send(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_tiles_to_send(), 250);
    EXPECT_EQ(leaf->get_tiles_to_send(), 6 * 250);
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(root.get(), leaf.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, GatherMultiInputMultiNode)
{
    std::unique_ptr<DataFlowNodeMock> root1 = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> root2 = make_root_df_node_mock(12 /* epoch_tiles */, 3 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> root3 = make_root_df_node_mock(88 /* epoch_tiles */, 11 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root1, leaf, {0, 2} /* offsets */),
        make_df_edge(root2, leaf, {0, 2, 5} /* offsets */),
        make_df_edge(root3, leaf, {3} /* offsets */)
    });

    data_flow_internal::calculate_tiles_to_send(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root1->get_tiles_to_send(), 250);
    EXPECT_EQ(root2->get_tiles_to_send(), 4);
    EXPECT_EQ(root3->get_tiles_to_send(), 8);
    EXPECT_EQ(leaf->get_tiles_to_send(), 520);
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(root1.get(), root2.get(), root3.get(), leaf.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, UnionSplitsTilesToSendPerInputGroup)
{
    std::unique_ptr<DataFlowNodeMock> root = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> union_node = make_df_node_mock(5 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, union_node, {1, 2, 3} /* offsets */),
        make_df_edge(union_node, leaf)
    });

    data_flow_internal::calculate_tiles_to_send(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_tiles_to_send(), 250);
    EXPECT_EQ(union_node->get_tiles_to_send(), 150);
    EXPECT_EQ(leaf->get_tiles_to_send(), 150);
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(root.get(), union_node.get(), leaf.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, MultipleLeafNodes)
{
    std::unique_ptr<DataFlowNodeMock> root1 = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> root2 = make_root_df_node_mock(4 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> root3 = make_root_df_node_mock(21 /* epoch_tiles */, 7 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> union_node = make_df_node_mock(2 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> leaf1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf2 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root1, leaf1, {0, 2} /* offsets */),
        make_df_edge(root2, leaf1, {0, 2, 5} /* offsets */),
        make_df_edge(root3, leaf1, {3} /* offsets */),
        make_df_edge(root2, union_node, {1, 2, 3, 4, 5}),
        make_df_edge(root3, union_node, {4, 5, 1}),
        make_df_edge(union_node, leaf2)
    });


    data_flow_internal::calculate_tiles_to_send(leaf1.get(), m_visited_nodes);
    data_flow_internal::calculate_tiles_to_send(leaf2.get(), m_visited_nodes);

    EXPECT_EQ(root1->get_tiles_to_send(), 250);
    EXPECT_EQ(root2->get_tiles_to_send(), 1);
    EXPECT_EQ(root3->get_tiles_to_send(), 3);
    EXPECT_EQ(union_node->get_tiles_to_send(), 7);
    EXPECT_EQ(leaf1->get_tiles_to_send(), 506);
    EXPECT_EQ(leaf2->get_tiles_to_send(), 7);
    EXPECT_THAT(
        m_visited_nodes,
        UnorderedElementsAre(root1.get(), root2.get(), root3.get(), union_node.get(), leaf1.get(), leaf2.get()));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_number_of_paths_through_node
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(unsigned int input_group_count = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_input_group_count()).WillByDefault(Return(input_group_count));

        return df_node_mock;
    }

    NodeId m_node_id;
    std::unordered_set<DataFlowNode*> m_visited_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode, RootPropagatesOneDFPath)
{
    std::unique_ptr<DataFlowNodeMock> root = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, leaf)
    });

    data_flow_internal::calculate_number_of_paths_through_node(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf->get_number_of_unique_df_paths(), 1);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode, UnionPropagesInputGroups)
{
    std::unique_ptr<DataFlowNodeMock> root = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> union_node = make_df_node_mock(42 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, union_node),
        make_df_edge(union_node, leaf)
    });

    data_flow_internal::calculate_number_of_paths_through_node(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(union_node->get_number_of_unique_df_paths(), 42);
    EXPECT_EQ(leaf->get_number_of_unique_df_paths(), 42);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode, NoUnionOnPathHasGather)
{
    std::unique_ptr<DataFlowNodeMock> root_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_4 = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root_1, fork_1),
        make_df_edge(root_1, fork_2),
        make_df_edge(root_2, fork_2),
        make_df_edge(root_2, fork_3),
        make_df_edge(fork_1, leaf_1),
        make_df_edge(fork_2, leaf_2),
        make_df_edge(fork_3, serial_fork_1),
        make_df_edge(fork_3, serial_fork_2),
        make_df_edge(serial_fork_1, leaf_3),
        make_df_edge(serial_fork_2, leaf_4)
    });

    data_flow_internal::calculate_number_of_paths_through_node(leaf_1.get(), m_visited_nodes);
    data_flow_internal::calculate_number_of_paths_through_node(leaf_2.get(), m_visited_nodes);
    data_flow_internal::calculate_number_of_paths_through_node(leaf_3.get(), m_visited_nodes);
    data_flow_internal::calculate_number_of_paths_through_node(leaf_4.get(), m_visited_nodes);


    EXPECT_EQ(root_1->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(root_2->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(fork_1->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(fork_2->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(fork_3->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_1->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_2->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf_1->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf_2->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf_3->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf_4->get_number_of_unique_df_paths(), 1);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode, HasUnionOnPath)
{
    std::unique_ptr<DataFlowNodeMock> root = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_4 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> serial_fork_5 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> union_node = make_df_node_mock(5 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> relay = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({
        make_df_edge(root, serial_fork_1),
        make_df_edge(root, serial_fork_2),
        make_df_edge(root, serial_fork_3),
        make_df_edge(root, serial_fork_4),
        make_df_edge(root, serial_fork_5),
        make_df_edge(serial_fork_1, union_node),
        make_df_edge(serial_fork_2, union_node),
        make_df_edge(serial_fork_3, union_node),
        make_df_edge(serial_fork_4, union_node),
        make_df_edge(serial_fork_5, union_node),
        make_df_edge(union_node, relay),
        make_df_edge(relay, leaf),
    });

    data_flow_internal::calculate_number_of_paths_through_node(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_1->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_2->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_3->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_4->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(serial_fork_5->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(union_node->get_number_of_unique_df_paths(), 5);
    EXPECT_EQ(relay->get_number_of_unique_df_paths(), 5);
    EXPECT_EQ(leaf->get_number_of_unique_df_paths(), 5);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_subtree_common_divisor
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateSubtreeCommonDivisor : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
    }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(unsigned int consume_granularity = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_consume_granularity()).WillByDefault(Return(consume_granularity));

        return df_node_mock;
    }

    NodeId m_node_id;
    std::unordered_set<DataFlowNode*> m_visited_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateSubtreeCommonDivisor, MultipleLeafNodesSingleRoot)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_1 = make_df_node_mock(5 /* consume_granularity */);
    std::unique_ptr<DataFlowNodeMock> leaf_2 = make_df_node_mock(3 /* consume_granularity */);
    std::unique_ptr<DataFlowNodeMock> relay_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> relay_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_3 = make_df_node_mock(7 /* consume_granularity */);

    connect_data_flow_graph({
        make_df_edge(root_node, fork_node_1),
        make_df_edge(root_node, fork_node_2),
        make_df_edge(fork_node_1, leaf_1),
        make_df_edge(fork_node_1, leaf_2),
        make_df_edge(fork_node_2, relay_1),
        make_df_edge(relay_1, relay_2),
        make_df_edge(relay_2, leaf_3)
    });

    data_flow_internal::calculate_subtree_common_divisor(root_node.get(), m_visited_nodes);

    EXPECT_EQ(leaf_3->get_subtree_common_divisor(), 7);
    EXPECT_EQ(relay_2->get_subtree_common_divisor(), 7);
    EXPECT_EQ(relay_1->get_subtree_common_divisor(), 7);
    EXPECT_EQ(fork_node_2->get_subtree_common_divisor(), 7);

    EXPECT_EQ(leaf_1->get_subtree_common_divisor(), 5);
    EXPECT_EQ(leaf_2->get_subtree_common_divisor(), 3);
    EXPECT_EQ(fork_node_1->get_subtree_common_divisor(), 15);

    EXPECT_EQ(root_node->get_subtree_common_divisor(), 105);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateSubtreeCommonDivisor, MultipleLeafNodesMultiRoot)
{
    std::unique_ptr<DataFlowNodeMock> root_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> root_3 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> fork_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_1 = make_df_node_mock(4 /* consume_granularity */);
    std::unique_ptr<DataFlowNodeMock> leaf_2 = make_df_node_mock(2 /* consume_granularity */);
    std::unique_ptr<DataFlowNodeMock> relay = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_3 = make_df_node_mock(1 /* consume_granularity */);

    connect_data_flow_graph({
        make_df_edge(root_1, fork_node_1),
        make_df_edge(root_1, fork_node_2),
        make_df_edge(root_2, fork_node_1),
        make_df_edge(root_2, fork_node_2),
        make_df_edge(root_3, fork_node_1),
        make_df_edge(root_3, fork_node_2),
        make_df_edge(fork_node_1, leaf_1),
        make_df_edge(fork_node_1, leaf_2),
        make_df_edge(fork_node_2, relay),
        make_df_edge(relay, leaf_3)
    });

    data_flow_internal::calculate_subtree_common_divisor(root_1.get(), m_visited_nodes);
    data_flow_internal::calculate_subtree_common_divisor(root_2.get(), m_visited_nodes);
    data_flow_internal::calculate_subtree_common_divisor(root_3.get(), m_visited_nodes);

    EXPECT_EQ(leaf_3->get_subtree_common_divisor(), 1);
    EXPECT_EQ(relay->get_subtree_common_divisor(), 1);
    EXPECT_EQ(fork_node_2->get_subtree_common_divisor(), 1);

    EXPECT_EQ(leaf_1->get_subtree_common_divisor(), 4);
    EXPECT_EQ(leaf_2->get_subtree_common_divisor(), 2);
    EXPECT_EQ(fork_node_1->get_subtree_common_divisor(), 4);

    EXPECT_EQ(root_1->get_subtree_common_divisor(), 4);
    EXPECT_EQ(root_2->get_subtree_common_divisor(), 4);
    EXPECT_EQ(root_3->get_subtree_common_divisor(), 4);
}
