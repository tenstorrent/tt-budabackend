// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
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
// clang-format on

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
    void SetUp() override { m_node_id = 1; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock()
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        return df_node_mock;
    }

    void call_visit_nodes_in_topological_order()
    {
        auto visit_func = [this](DataFlowNode* df_node) { m_topo_sort.push_back(df_node->get_id()); };

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

    connect_data_flow_graph(
        {make_df_edge(node_1, node_2),
         make_df_edge(node_2, node_3),
         make_df_edge(node_1, node_4),
         make_df_edge(node_3, node_5),
         make_df_edge(node_4, node_5),
         make_df_edge(node_5, node_6),
         make_df_edge(node_6, node_7),
         make_df_edge(node_5, node_8)});

    m_root_nodes = {node_1.get()};
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(1, 4, 2, 3, 5, 8, 6, 7));
    EXPECT_THAT(
        m_visited_nodes,
        UnorderedElementsAre(
            node_1.get(),
            node_2.get(),
            node_3.get(),
            node_4.get(),
            node_5.get(),
            node_6.get(),
            node_7.get(),
            node_8.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_VisitNodesInTopologicalOrder, NodeAlreadyVisitedFromDifferentRoot)
{
    std::unique_ptr<DataFlowNodeMock> node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> node_2 = make_df_node_mock();

    connect_data_flow_graph({make_df_edge(node_1, node_2)});

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

    connect_data_flow_graph(
        {make_df_edge(node_1, node_4),
         make_df_edge(node_4, node_5),
         make_df_edge(node_3, node_5),
         make_df_edge(node_5, node_6),
         make_df_edge(node_2, node_6),
         make_df_edge(node_6, node_7),
         make_df_edge(node_5, node_8)});

    m_root_nodes = {node_3.get(), node_1.get(), node_2.get()};
    call_visit_nodes_in_topological_order();

    EXPECT_THAT(m_topo_sort, ElementsAre(2, 1, 4, 3, 5, 8, 6, 7));
    EXPECT_THAT(
        m_visited_nodes,
        UnorderedElementsAre(
            node_1.get(),
            node_2.get(),
            node_3.get(),
            node_4.get(),
            node_5.get(),
            node_6.get(),
            node_7.get(),
            node_8.get()));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_single_source_paths
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(bool is_scatter = false)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));
        m_data_flow_nodes.push_back(df_node_mock.get());

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_scatter_df_node_mock() { return make_df_node_mock(true /* is_scatter */); }

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

    connect_data_flow_graph({make_df_edge(df_node_1, df_node_2)});

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), false /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, ScatterPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> scatter_node = make_scatter_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();

    connect_data_flow_graph({make_df_edge(scatter_node, df_node_1), make_df_edge(df_node_1, df_node_2)});

    EXPECT_FALSE(
        data_flow_internal::find_single_source_paths(scatter_node.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, SingleNodeGatherPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(df_node_1, gather_node, {0, 2, 4, 6, 8} /* offsets */), make_df_edge(gather_node, df_node_2)});

    EXPECT_FALSE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(false /* expected_single_source_path */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_FindSingleSourcePaths, MultiNodeGatherPath_NotSingleSource)
{
    std::unique_ptr<DataFlowNodeMock> df_node_1 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_2 = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> gather_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> df_node_3 = make_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(df_node_1, gather_node),
         make_df_edge(df_node_2, gather_node),
         make_df_edge(gather_node, df_node_3)});

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

    connect_data_flow_graph(
        {make_df_edge(df_node_1, df_node_2),
         make_df_edge(df_node_2, scatter_node),
         make_df_edge(scatter_node, df_node_3)});

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

    connect_data_flow_graph(
        {make_df_edge(df_node_1, df_node_2),
         make_df_edge(df_node_2, df_node_3),
         make_df_edge(df_node_3, scatter_node),
         make_df_edge(scatter_node, df_node_4),
         make_df_edge(df_node_2, df_node_5)});

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

    connect_data_flow_graph(
        {make_df_edge(df_node_1, df_node_2),
         make_df_edge(df_node_2, df_node_3),
         make_df_edge(df_node_3, df_node_4),
         make_df_edge(df_node_4, df_node_5)});

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

    connect_data_flow_graph(
        {make_df_edge(df_node_1, df_node_2),
         make_df_edge(df_node_2, df_node_3),
         make_df_edge(df_node_2, df_node_4),
         make_df_edge(df_node_4, df_node_5)});

    EXPECT_TRUE(data_flow_internal::find_single_source_paths(df_node_1.get(), true /* is_upstream_single_source */));
    verify_all_nodes_on_path(true /* expected_single_source_path */);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_tiles_to_send
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

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

    connect_data_flow_graph({make_df_edge(root, leaf)});

    data_flow_internal::calculate_tiles_to_send(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_tiles_to_send(), 250);
    EXPECT_EQ(leaf->get_tiles_to_send(), 250);
    EXPECT_THAT(m_visited_nodes, UnorderedElementsAre(root.get(), leaf.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateTilesToSend, RootTilesToSendInitAndPropagatesMultiInput)
{
    std::unique_ptr<DataFlowNodeMock> root = make_root_df_node_mock(1000 /* epoch_tiles */, 4 /* num_iterations */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({make_df_edge(root, leaf, {0, 2, 4, 6, 8, 10} /* offsets */)});

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

    connect_data_flow_graph(
        {make_df_edge(root1, leaf, {0, 2} /* offsets */),
         make_df_edge(root2, leaf, {0, 2, 5} /* offsets */),
         make_df_edge(root3, leaf, {3} /* offsets */)});

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

    connect_data_flow_graph({make_df_edge(root, union_node, {1, 2, 3} /* offsets */), make_df_edge(union_node, leaf)});

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

    connect_data_flow_graph(
        {make_df_edge(root1, leaf1, {0, 2} /* offsets */),
         make_df_edge(root2, leaf1, {0, 2, 5} /* offsets */),
         make_df_edge(root3, leaf1, {3} /* offsets */),
         make_df_edge(root2, union_node, {1, 2, 3, 4, 5}),
         make_df_edge(root3, union_node, {4, 5, 1}),
         make_df_edge(union_node, leaf2)});

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
    void SetUp() override { m_node_id = 100; }

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

    connect_data_flow_graph({make_df_edge(root, leaf)});

    data_flow_internal::calculate_number_of_paths_through_node(leaf.get(), m_visited_nodes);

    EXPECT_EQ(root->get_number_of_unique_df_paths(), 1);
    EXPECT_EQ(leaf->get_number_of_unique_df_paths(), 1);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumberOfPathsThroughNode, UnionPropagesInputGroups)
{
    std::unique_ptr<DataFlowNodeMock> root = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> union_node = make_df_node_mock(42 /* input_group_count */);
    std::unique_ptr<DataFlowNodeMock> leaf = make_df_node_mock();

    connect_data_flow_graph({make_df_edge(root, union_node), make_df_edge(union_node, leaf)});

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

    connect_data_flow_graph(
        {make_df_edge(root_1, fork_1),
         make_df_edge(root_1, fork_2),
         make_df_edge(root_2, fork_2),
         make_df_edge(root_2, fork_3),
         make_df_edge(fork_1, leaf_1),
         make_df_edge(fork_2, leaf_2),
         make_df_edge(fork_3, serial_fork_1),
         make_df_edge(fork_3, serial_fork_2),
         make_df_edge(serial_fork_1, leaf_3),
         make_df_edge(serial_fork_2, leaf_4)});

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
    void SetUp() override { m_node_id = 100; }

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

    connect_data_flow_graph(
        {make_df_edge(root_node, fork_node_1),
         make_df_edge(root_node, fork_node_2),
         make_df_edge(fork_node_1, leaf_1),
         make_df_edge(fork_node_1, leaf_2),
         make_df_edge(fork_node_2, relay_1),
         make_df_edge(relay_1, relay_2),
         make_df_edge(relay_2, leaf_3)});

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

    connect_data_flow_graph(
        {make_df_edge(root_1, fork_node_1),
         make_df_edge(root_1, fork_node_2),
         make_df_edge(root_2, fork_node_1),
         make_df_edge(root_2, fork_node_2),
         make_df_edge(root_3, fork_node_1),
         make_df_edge(root_3, fork_node_2),
         make_df_edge(fork_node_1, leaf_1),
         make_df_edge(fork_node_1, leaf_2),
         make_df_edge(fork_node_2, relay),
         make_df_edge(relay, leaf_3)});

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

/**********************************************************************************************************************
    Tests for function: data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CanAllLeafNodesDoEpochInSingleIteration : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        bool is_untilized_dram_output = false, bool is_intermediate = false)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_intermediate()).WillByDefault(Return(is_intermediate));
        ON_CALL(*df_node_mock, is_untilized_dram_output()).WillByDefault(Return(is_untilized_dram_output));

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_untilized_dram_output_df_node_mock()
    {
        return make_df_node_mock(true /* is_untilized_dram_output */, false /* is_intermediate */);
    }

    std::unique_ptr<DataFlowNodeMock> make_intermediate_df_node_mock()
    {
        return make_df_node_mock(false /* is_untilized_dram_output */, true /* is_intermediate */);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CanAllLeafNodesDoEpochInSingleIteration, UntilizedDramOutputCanDo)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_untilized_dram_output_df_node_mock();

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    EXPECT_TRUE(data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration({leaf_node.get()}));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CanAllLeafNodesDoEpochInSingleIteration, IntermediateCanDo)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_intermediate_df_node_mock();

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    EXPECT_TRUE(data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration({leaf_node.get()}));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CanAllLeafNodesDoEpochInSingleIteration, MultipleLeafNodesCanDo)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_untilized_dram_output_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 = make_untilized_dram_output_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_3 = make_intermediate_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(root_node, leaf_node_1),
         make_df_edge(root_node, leaf_node_2),
         make_df_edge(root_node, leaf_node_3)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node_1.get(), leaf_node_2.get(), leaf_node_3.get()};
    EXPECT_TRUE(data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration(leaf_nodes));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CanAllLeafNodesDoEpochInSingleIteration, AtLeastOneLeafCantDo)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_untilized_dram_output_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_which_cant_do_epoch_in_single_iter = make_df_node_mock();
    std::unique_ptr<DataFlowNodeMock> leaf_node_3 = make_intermediate_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(root_node, leaf_node_1),
         make_df_edge(root_node, leaf_node_which_cant_do_epoch_in_single_iter),
         make_df_edge(root_node, leaf_node_3)});

    std::vector<DataFlowNode*> leaf_nodes{
        leaf_node_1.get(), leaf_node_which_cant_do_epoch_in_single_iter.get(), leaf_node_3.get()};
    EXPECT_FALSE(data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration(leaf_nodes));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::are_all_leaf_nodes_on_single_source_path
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_AreAllLeafNodesOnSingleSourcePath : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(bool is_on_single_source_path)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        df_node_mock->set_is_on_single_source_path(is_on_single_source_path);

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_AreAllLeafNodesOnSingleSourcePath, AllLeavesAre)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_3 = make_df_node_mock(true /* is_on_single_source_path */);

    connect_data_flow_graph(
        {make_df_edge(root_node, leaf_node_1),
         make_df_edge(root_node, leaf_node_2),
         make_df_edge(root_node, leaf_node_3)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node_1.get(), leaf_node_2.get(), leaf_node_3.get()};
    EXPECT_TRUE(data_flow_internal::are_all_leaf_nodes_on_single_source_path(leaf_nodes));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_AreAllLeafNodesOnSingleSourcePath, AtLeastOneLeafIsnt)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 = make_df_node_mock(true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_3 = make_df_node_mock(false /* is_on_single_source_path */);

    connect_data_flow_graph(
        {make_df_edge(root_node, leaf_node_1),
         make_df_edge(root_node, leaf_node_2),
         make_df_edge(root_node, leaf_node_3)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node_1.get(), leaf_node_2.get(), leaf_node_3.get()};
    EXPECT_FALSE(data_flow_internal::are_all_leaf_nodes_on_single_source_path(leaf_nodes));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::number_of_iterations_satisfies_divisibility_constraints
**********************************************************************************************************************/

// No need to create a test fixture here since the test function doesn't require any context to be invoked int.
TEST(Pipegen2_TransferLimitsCalculatorInternal, NumberOfIterationsSatisfiesDivisbilityConstraints)
{
    // 1. Epoch tiles is not divisible by the number of iterations => doesn't satisfy.
    EXPECT_FALSE(data_flow_internal::number_of_iterations_satisfies_divisibility_constraints(
        5 /* num_iterations */, 12 /* num_epoch_tiles */, 3 /* tile_clear_granularity */));

    // 2. Epoch tiles and num iterations ratio is not divisible by tile clear granularity => doesn't satisfy.
    EXPECT_FALSE(data_flow_internal::number_of_iterations_satisfies_divisibility_constraints(
        5 /* num_iterations */, 15 /* num_epoch_tiles */, 2 /* tile_clear_granularity */));

    // 3. Both above conditions are met => does satisfy.
    EXPECT_TRUE(data_flow_internal::number_of_iterations_satisfies_divisibility_constraints(
        5 /* num_iterations */, 30 /* num_epoch_tiles */, 3 /* tile_clear_granularity */));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::get_single_source_path_clearing_granularity
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_GetSingleSourcePathClearingGranularity : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int scatter_gather_num_tiles, unsigned int consume_granularity, bool is_dram_or_pcie_output)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_scatter_gather_num_tiles()).WillByDefault(Return(scatter_gather_num_tiles));
        ON_CALL(*df_node_mock, get_consume_granularity()).WillByDefault(Return(consume_granularity));
        ON_CALL(*df_node_mock, is_dram_or_pcie_output()).WillByDefault(Return(is_dram_or_pcie_output));

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(unsigned int scatter_gather_num_tiles)
    {
        return make_df_node_mock(
            scatter_gather_num_tiles, 1 /* consume_granularity */, false /* is_dram_or_pcie_output */);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int consume_granularity, bool is_dram_or_pcie_output)
    {
        return make_df_node_mock(1 /* scatter_gather_num_tiles */, consume_granularity, is_dram_or_pcie_output);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_GetSingleSourcePathClearingGranularity, PathEndsWithDramOutput)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_root_df_node_mock(15 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(30 /* consume_granularity */, true /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node.get()};
    unsigned int clearing_granularity =
        data_flow_internal::get_single_source_path_clearing_granularity(root_node.get(), leaf_nodes);

    EXPECT_EQ(clearing_granularity, root_node->get_scatter_gather_num_tiles());
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_GetSingleSourcePathClearingGranularity, PathDoesntEndWithDramOutput)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_root_df_node_mock(15 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(30 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node.get()};
    unsigned int clearing_granularity =
        data_flow_internal::get_single_source_path_clearing_granularity(root_node.get(), leaf_nodes);

    EXPECT_EQ(clearing_granularity, leaf_node->get_consume_granularity());
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_GetSingleSourcePathClearingGranularity, MultipleLeavesLcmGranularity)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_root_df_node_mock(10 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 =
        make_leaf_df_node_mock(30 /* consume_granularity */, false /* is_dram_or_pcie_output */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 =
        make_leaf_df_node_mock(20 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node_1), make_df_edge(root_node, leaf_node_2)});

    std::vector<DataFlowNode*> leaf_nodes{leaf_node_1.get(), leaf_node_2.get()};
    unsigned int clearing_granularity =
        data_flow_internal::get_single_source_path_clearing_granularity(root_node.get(), leaf_nodes);

    EXPECT_EQ(clearing_granularity, 60);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_num_iterations_for_single_source_path
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int epoch_tiles,
        unsigned int scatter_gather_num_tiles,
        unsigned int consume_granularity,
        bool is_dram_or_pcie_output)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_num_epoch_tiles()).WillByDefault(Return(epoch_tiles));
        ON_CALL(*df_node_mock, get_scatter_gather_num_tiles()).WillByDefault(Return(scatter_gather_num_tiles));
        ON_CALL(*df_node_mock, get_consume_granularity()).WillByDefault(Return(consume_granularity));
        ON_CALL(*df_node_mock, is_dram_or_pcie_output()).WillByDefault(Return(is_dram_or_pcie_output));

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(
        unsigned int epoch_tiles, unsigned int scatter_gather_num_tiles)
    {
        return make_df_node_mock(
            epoch_tiles, scatter_gather_num_tiles, 1 /* consume_granularity */, false /* is_dram_or_pcie_output */);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int consume_granularity, bool is_dram_or_pcie_output)
    {
        return make_df_node_mock(
            1 /* epoch_tiles */, 1 /* scatter_gather_num_tiles */, consume_granularity, is_dram_or_pcie_output);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath, AllPhasesFitOneIteration)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(100 /* epoch_tiles */, 10 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(30 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int num_iterations = data_flow_internal::calculate_num_iterations_for_single_source_path(
        root_node.get(), {leaf_node.get()}, 20 /* max_num_tiles_per_phase */, 6 /* max_num_phases_per_iteration */);

    EXPECT_EQ(num_iterations, 1);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath,
    CanTransferTilesInIdealNumberOfIterations)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(144 /* epoch_tiles */, 1 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(2 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int num_iterations = data_flow_internal::calculate_num_iterations_for_single_source_path(
        root_node.get(), {leaf_node.get()}, 16 /* max_num_tiles_per_phase */, 3 /* max_num_phases_per_iteration */);

    // Ideal number of iterations for this test case is 3 and we are able to transfer all epoch tiles in 3 iterations.
    EXPECT_EQ(num_iterations, 3);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath,
    MustTransferTilesInLessThanIdealNumberOfIterations)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(65 /* epoch_tiles */, 1 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(13 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int num_iterations = data_flow_internal::calculate_num_iterations_for_single_source_path(
        root_node.get(), {leaf_node.get()}, 3 /* max_num_tiles_per_phase */, 3 /* max_num_phases_per_iteration */);

    // Ideal number of iterations for this test case is 7 and we are able to transfer all epoch tiles in 5 iterations.
    EXPECT_EQ(num_iterations, 5);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath,
    MustTransferTilesInMoreThanIdealNumberOfIterations)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(21 /* epoch_tiles */, 1 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(3 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int num_iterations = data_flow_internal::calculate_num_iterations_for_single_source_path(
        root_node.get(), {leaf_node.get()}, 2 /* max_num_tiles_per_phase */, 2 /* max_num_phases_per_iteration */);

    // Ideal number of iterations for this test case is 5 and we are able to transfer all epoch tiles in 7 iterations.
    EXPECT_EQ(num_iterations, 7);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForSingleSourcePath, MustTransferEpochTilesOneByOne)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(13 /* epoch_tiles */, 1 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(3 /* consume_granularity */, false /* is_dram_or_pcie_output */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int num_iterations = data_flow_internal::calculate_num_iterations_for_single_source_path(
        root_node.get(), {leaf_node.get()}, 3 /* max_num_tiles_per_phase */, 3 /* max_num_phases_per_iteration */);

    // Ideal number of iterations for this test case is 1 and we are able to transfer all epoch tiles in 13 iterations.
    EXPECT_EQ(num_iterations, 13);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_num_iterations_for_multi_source_path
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForMultiSourcePath : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int repeat_factor,
        unsigned int serialization_factor,
        unsigned int epoch_tiles = 1,
        unsigned int scatter_gather_num_tiles = 1)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_num_epoch_tiles()).WillByDefault(Return(epoch_tiles));
        ON_CALL(*df_node_mock, get_scatter_gather_num_tiles()).WillByDefault(Return(scatter_gather_num_tiles));
        ON_CALL(*df_node_mock, get_repeat_factor()).WillByDefault(Return(repeat_factor));
        ON_CALL(*df_node_mock, get_serialization_factor()).WillByDefault(Return(serialization_factor));

        m_data_flow_nodes.push_back(df_node_mock.get());

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(
        unsigned int epoch_tiles, unsigned int scatter_gather_num_tiles)
    {
        return make_df_node_mock(
            1 /* repeat_factor */, 1 /* serialization_factor */, epoch_tiles, scatter_gather_num_tiles);
    }

    std::vector<DataFlowNode*> m_data_flow_nodes;
    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterationsForMultiSourcePath, MultipleNodesOnForkedPath)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(120 /* epoch_tiles */, 10 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> fork_node =
        make_df_node_mock(1 /* repeat_factor */, 1 /* serialization_factor */);
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_1 =
        make_df_node_mock(3 /* repeat_factor */, 2 /* serialization_factor */);
    std::unique_ptr<DataFlowNodeMock> serial_fork_node_2 =
        make_df_node_mock(5 /* repeat_factor */, 6 /* serialization_factor */);

    connect_data_flow_graph(
        {make_df_edge(root_node, fork_node),
         make_df_edge(fork_node, serial_fork_node_1),
         make_df_edge(fork_node, serial_fork_node_2)});

    data_flow_internal::calculate_num_iterations_for_multi_source_path({root_node.get()});

    std::unordered_map<NodeId, unsigned int> node_id_to_expected_num_iterations{
        {root_node->get_id(), 12u},
        {fork_node->get_id(), 12u},
        {serial_fork_node_1->get_id(), 18},
        {serial_fork_node_2->get_id(), 10}};
    verify_data_flow_nodes_num_iterations(m_data_flow_nodes, node_id_to_expected_num_iterations);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_num_iterations
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterations : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int repeat_factor,
        unsigned int serialization_factor,
        unsigned int epoch_tiles,
        unsigned int scatter_gather_num_tiles,
        bool can_do_epoch_in_single_iteration,
        bool is_on_single_source_path)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, get_num_epoch_tiles()).WillByDefault(Return(epoch_tiles));
        ON_CALL(*df_node_mock, get_scatter_gather_num_tiles()).WillByDefault(Return(scatter_gather_num_tiles));
        ON_CALL(*df_node_mock, get_repeat_factor()).WillByDefault(Return(repeat_factor));
        ON_CALL(*df_node_mock, get_serialization_factor()).WillByDefault(Return(serialization_factor));

        // We ensure that this node can do epoch in single iteration by configuring it to be untilized dram output
        // node. Combinations with underlying intermediate node are tested in more detail in tests for function
        // data_flow_internal::can_all_leaf_nodes_do_epoch_in_single_iteration.
        ON_CALL(*df_node_mock, is_untilized_dram_output()).WillByDefault(Return(can_do_epoch_in_single_iteration));

        // If this leaf node is on single source path, configure it to be dram output node so that the clearing size of
        // that single source path will be scatter gather num tiles of the root node.
        ON_CALL(*df_node_mock, is_dram_or_pcie_output()).WillByDefault(Return(is_on_single_source_path));
        df_node_mock->set_is_on_single_source_path(is_on_single_source_path);

        m_data_flow_nodes.push_back(df_node_mock.get());

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_root_df_node_mock(
        unsigned int epoch_tiles, unsigned int scatter_gather_num_tiles)
    {
        std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
            1 /* repeat_factor */,
            1 /* serialization_factor */,
            epoch_tiles,
            scatter_gather_num_tiles,
            false /* can_do_epoch_in_single_iteration */,
            true /* is_on_single_source_path */);

        m_root_nodes.push_back(root_node.get());

        return root_node;
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int repeat_factor,
        unsigned int serialization_factor,
        bool can_do_epoch_in_single_iteration,
        bool is_on_single_source_path)
    {
        std::unique_ptr<DataFlowNodeMock> leaf_node = make_df_node_mock(
            repeat_factor,
            serialization_factor,
            1 /* epoch_tiles */,
            1 /* scatter_gather_num_tiles */,
            can_do_epoch_in_single_iteration,
            is_on_single_source_path);

        m_leaf_nodes.push_back(leaf_node.get());

        return leaf_node;
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        bool can_do_epoch_in_single_iteration, bool is_on_single_source_path)
    {
        return make_leaf_df_node_mock(
            1 /* repeat_factor */,
            1 /* serialization_factor */,
            can_do_epoch_in_single_iteration,
            is_on_single_source_path);
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int repeat_factor, unsigned int serialization_factor)
    {
        return make_leaf_df_node_mock(
            repeat_factor,
            serialization_factor,
            false /* can_do_epoch_in_single_iteration */,
            false /* is_on_single_source_path */);
    }

    std::vector<DataFlowNode*> m_data_flow_nodes;
    std::vector<DataFlowNode*> m_leaf_nodes;
    std::vector<DataFlowNode*> m_root_nodes;
    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterations, AllLeafNodesCanDoEpochInSingleIteration)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(100 /* epoch_tiles */, 5 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 =
        make_leaf_df_node_mock(true /* can_do_epoch_in_single_teration */, false /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 =
        make_leaf_df_node_mock(true /* can_do_epoch_in_single_teration */, false /* is_on_single_source_path */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node_1), make_df_edge(root_node, leaf_node_2)});

    data_flow_internal::calculate_num_iterations(
        m_leaf_nodes, m_root_nodes, 10 /* max_num_tiles_per_phase */, 10 /* max_num_phases_per_iteration */);

    verify_data_flow_nodes_num_iterations(m_data_flow_nodes, 1);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterations, AllLeafNodesOnSingleSourcePath)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(100 /* epoch_tiles */, 5 /* scatter_gather_num_tiles */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 =
        make_leaf_df_node_mock(true /* can_do_epoch_in_single_teration */, true /* is_on_single_source_path */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 =
        make_leaf_df_node_mock(false /* can_do_epoch_in_single_teration */, true /* is_on_single_source_path */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node_1), make_df_edge(root_node, leaf_node_2)});

    data_flow_internal::calculate_num_iterations(
        m_leaf_nodes, m_root_nodes, 10 /* max_num_tiles_per_phase */, 2 /* max_num_phases_per_iteration */);

    verify_data_flow_nodes_num_iterations(m_data_flow_nodes, 5);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateNumIterations, MultiSourcePath)
{
    std::unique_ptr<DataFlowNodeMock> root_node =
        make_root_df_node_mock(100 /* epoch_tiles */, 5 /* scatter_gather_num_tiles */);

    // Need to pass Xu as the arguments since otherwise it is ambigous which function definition compiler should use
    // since both bool and unsigned int versions require conversion from int (if not U suffixs is used).
    std::unique_ptr<DataFlowNodeMock> leaf_node_1 =
        make_leaf_df_node_mock(3u /* repeat_factor */, 2u /* serialization_factor */);
    std::unique_ptr<DataFlowNodeMock> leaf_node_2 =
        make_leaf_df_node_mock(1u /* repeat_factor */, 5u /* serialization_factor */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node_1), make_df_edge(root_node, leaf_node_2)});

    data_flow_internal::calculate_num_iterations(
        m_leaf_nodes, m_root_nodes, 100 /* max_num_tiles_per_phase */, 15 /* max_num_phases_per_iteration */);

    std::unordered_map<NodeId, unsigned int> node_id_to_expected_num_iterations{
        {root_node->get_id(), 20u}, {leaf_node_1->get_id(), 30u}, {leaf_node_2->get_id(), 4}};
    verify_data_flow_nodes_num_iterations(m_data_flow_nodes, node_id_to_expected_num_iterations);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::is_end_of_single_source_edge
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_IsEndOfSingleSourceEdge : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(bool is_scatter)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));

        return df_node_mock;
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_IsEndOfSingleSourceEdge, RootNodeReturnsFalse)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(false /* is_scatter */);
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_df_node_mock(false /* is_scatter */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    EXPECT_FALSE(data_flow_internal::is_end_of_single_source_edge(root_node.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_IsEndOfSingleSourceEdge, MultipleInputsReturnsFalse)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(false /* is_scatter */);
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_df_node_mock(false /* is_scatter */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node, {1, 2} /* offsets */)});

    EXPECT_FALSE(data_flow_internal::is_end_of_single_source_edge(leaf_node.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_IsEndOfSingleSourceEdge, ParrentIsScatterReturnsFalse)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(true /* is_scatter */);
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_df_node_mock(false /* is_scatter */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    EXPECT_FALSE(data_flow_internal::is_end_of_single_source_edge(root_node.get()));
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_IsEndOfSingleSourceEdge, ReturnsTrue)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(false /* is_scatter */);
    std::unique_ptr<DataFlowNodeMock> leaf_node = make_df_node_mock(false /* is_scatter */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    EXPECT_TRUE(data_flow_internal::is_end_of_single_source_edge(leaf_node.get()));
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_max_tiles_per_phase_for_node
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int size_tiles,
        bool is_dram_or_pcie_input,
        unsigned int subtree_common_divisor,
        unsigned int tiles_to_send,
        bool is_scatter)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));
        ON_CALL(*df_node_mock, get_size_tiles()).WillByDefault(Return(size_tiles));
        ON_CALL(*df_node_mock, is_dram_or_pcie_input()).WillByDefault(Return(is_dram_or_pcie_input));

        df_node_mock->set_subtree_common_divisor(subtree_common_divisor);
        df_node_mock->set_tiles_to_send(tiles_to_send);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int subtree_common_divisor, unsigned int tiles_to_send)
    {
        return make_df_node_mock(
            1 /* size_tiles */,
            false /* is_dram_or_pcie_input */,
            subtree_common_divisor,
            tiles_to_send,
            false /* is_scatter */);
    }

    NodeId m_node_id;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode, EndOfSingleSourceEdge)
{
    // Only relevant parameter is `is_scatter` which ensures that the leaf node is the end of single source path.
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        1 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        1 /* subtree_common_divisor */,
        1 /* tiles_to_send */,
        false /* is_scatter */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(1 /* subtree_common_divisor */, 1 /* tiles_to_send */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    unsigned int parent_max_num_tiles_per_phase = 42;
    root_node->set_max_num_tiles_per_phase(parent_max_num_tiles_per_phase);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        leaf_node.get(), 1 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    EXPECT_EQ(leaf_node->get_max_num_tiles_per_phase(), parent_max_num_tiles_per_phase);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode,
    NonDramRootNodeInitializesCommonDivisorToSizeTiles)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        18 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        1 /* subtree_common_divisor */,
        9 /* tiles_to_send */,
        false /* is_scatter */);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        root_node.get(), 3186 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 2034 /* int(2048 / 18) * 18 */);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode,
    DramRootNodeInitializesCommonDivisorToOne)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        18 /* size_tiles */,
        true /* is_dram_or_pcie_input */,
        1 /* subtree_common_divisor */,
        9 /* tiles_to_send */,
        false /* is_scatter */);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        root_node.get(), 3186 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 2048 /* int(2048 / 1) * 1 */);
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode, TilesToSendGTMaxTilesPerPhase)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        18 /* size_tiles */,
        true /* is_dram_or_pcie_input */,
        7 /* subtree_common_divisor */,
        2160 /* tiles_to_send */,
        false /* is_scatter */);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        root_node.get(), 3186 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    // Common divisor will be 7 due to subtree common divisor (size tiles does not contribute to common divisor as
    // this node is a dram root node).
    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 2044 /* int(2048 / 7) * 7 */);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode,
    RootNumTilesPerInputLEMaxTilesPerPhaseWithoutWarning)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        18 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        7 /* subtree_common_divisor */,
        9 /* tiles_to_send */,
        false /* is_scatter */);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        root_node.get(), 1926 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    // Common divisor gets initialized to 18 and will be 1926 as LCM(18, 1926).
    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 1926 /* int(2048 / 1926) * 1926 */);
}

TEST_F(
    Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhaseForNode,
    RootNumTilesPerInputLEMaxTilesPerPhaseWithWarning)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        27 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        7 /* subtree_common_divisor */,
        9 /* tiles_to_send */,
        false /* is_scatter */);

    data_flow_internal::calculate_max_tiles_per_phase_for_node(
        root_node.get(), 2322 /* root_num_tiles_per_input */, 2048 /* max_num_tiles_per_phase */);

    // Common divisor gets initialized to 27 and will stay at that value because 2322, LCM(27, 2322), is larger then
    // max num tiles per phase.
    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 2025 /* int(2048 / 27) * 27 */);
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::calculate_max_tiles_per_phase
**********************************************************************************************************************/

class Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhase : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::unique_ptr<DataFlowNodeMock> make_df_node_mock(
        unsigned int size_tiles,
        bool is_dram_or_pcie_input,
        unsigned int subtree_common_divisor,
        unsigned int tiles_to_send,
        bool is_scatter,
        unsigned int num_tiles_per_input)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_scatter()).WillByDefault(Return(is_scatter));
        ON_CALL(*df_node_mock, get_size_tiles()).WillByDefault(Return(size_tiles));
        ON_CALL(*df_node_mock, is_dram_or_pcie_input()).WillByDefault(Return(is_dram_or_pcie_input));
        ON_CALL(*df_node_mock, get_num_tiles_per_input()).WillByDefault(Return(num_tiles_per_input));

        df_node_mock->set_subtree_common_divisor(subtree_common_divisor);
        df_node_mock->set_tiles_to_send(tiles_to_send);

        return df_node_mock;
    }

    std::unique_ptr<DataFlowNodeMock> make_leaf_df_node_mock(
        unsigned int subtree_common_divisor, unsigned int tiles_to_send)
    {
        return make_df_node_mock(
            1 /* size_tiles */,
            false /* is_dram_or_pcie_input */,
            subtree_common_divisor,
            tiles_to_send,
            false /* is_scatter */,
            1 /* num_tiles_per_input */);
    }

    NodeId m_node_id;
    std::unordered_set<DataFlowNode*> m_visited_nodes;
};

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhase, AssertsWhenInvokedWithNonRootNode)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        1 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        1 /* subtree_common_divisor */,
        1 /* tiles_to_send */,
        false /* is_scatter */,
        120 /* num_tiles_per_input */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(1 /* subtree_common_divisor */, 1 /* tiles_to_send */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    verify_log_assert(
        [&]()
        {
            data_flow_internal::calculate_max_tiles_per_phase(
                leaf_node.get(), m_visited_nodes, 2048 /* max_num_tiles_per_phase */);
        },
        "^Expecting root node argument to start with max_tiles_per_phase calculation but got node [0-9]+.*");
}

TEST_F(Pipegen2_TransferLimitsCalculatorInternal_CalculateMaxTilesPerPhase, MaxTilesPerPhaseCalculatedForAllNodesOnPath)
{
    std::unique_ptr<DataFlowNodeMock> root_node = make_df_node_mock(
        27 /* size_tiles */,
        false /* is_dram_or_pcie_input */,
        7 /* subtree_common_divisor */,
        9 /* tiles_to_send */,
        true /* is_scatter */,
        2322 /* num_tiles_per_input */);
    std::unique_ptr<DataFlowNodeMock> leaf_node =
        make_leaf_df_node_mock(7 /* subtree_common_divisor */, 2088 /* tiles_to_send */);

    connect_data_flow_graph({make_df_edge(root_node, leaf_node)});

    data_flow_internal::calculate_max_tiles_per_phase(
        root_node.get(), m_visited_nodes, 2048 /* max_num_tiles_per_phase */);

    EXPECT_EQ(root_node->get_max_num_tiles_per_phase(), 2025);
    EXPECT_EQ(leaf_node->get_max_num_tiles_per_phase(), 2044);
}
