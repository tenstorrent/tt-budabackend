// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/data_flow_calculator_internal.h"

#include <memory>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "model/data_flow/data_flow_graph.h"

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::AnyOf;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_root_nodes
**********************************************************************************************************************/

class Pipegen2_DataFlowCalculatorInternal_FindRootNodes : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
        m_data_flow_graph = std::make_unique<DataFlowGraph>();
    }

    DataFlowNodeMock* create_df_node_mock(bool is_padding)
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));
        ON_CALL(*df_node_mock, is_padding()).WillByDefault(Return(is_padding));

        DataFlowNodeMock* df_node_mock_ptr = df_node_mock.get();
        m_data_flow_graph->add_node(std::move(df_node_mock));

        return df_node_mock_ptr;
    }

    DataFlowNodeMock* create_non_padding_df_node() { return create_df_node_mock(false /* is_padding */); }

    DataFlowNodeMock* create_padding_df_node() { return create_df_node_mock(true /* is_padding */); }

    NodeId m_node_id;
    std::unique_ptr<DataFlowGraph> m_data_flow_graph;
};

TEST_F(Pipegen2_DataFlowCalculatorInternal_FindRootNodes, NoPaddingRootNodes)
{
    DataFlowNodeMock* root1 = create_non_padding_df_node();
    DataFlowNodeMock* root2 = create_non_padding_df_node();
    DataFlowNodeMock* root3 = create_non_padding_df_node();
    DataFlowNodeMock* leaf = create_non_padding_df_node();

    connect_data_flow_graph({make_df_edge(root1, leaf), make_df_edge(root2, leaf), make_df_edge(root3, leaf)});

    std::vector<DataFlowNode*> root_nodes = data_flow_internal::find_root_nodes(m_data_flow_graph.get());

    EXPECT_THAT(root_nodes, UnorderedElementsAre(root1, root2, root3));
}

TEST_F(Pipegen2_DataFlowCalculatorInternal_FindRootNodes, WithPaddingRootNodes)
{
    DataFlowNodeMock* root1 = create_non_padding_df_node();
    DataFlowNodeMock* root2_padding = create_padding_df_node();
    DataFlowNodeMock* root3 = create_non_padding_df_node();
    DataFlowNodeMock* root4_padding = create_padding_df_node();
    DataFlowNodeMock* root5 = create_non_padding_df_node();
    DataFlowNodeMock* leaf = create_non_padding_df_node();

    connect_data_flow_graph(
        {make_df_edge(root1, leaf),
         make_df_edge(root2_padding, leaf),
         make_df_edge(root3, leaf),
         make_df_edge(root4_padding, leaf),
         make_df_edge(root5, leaf)});

    std::vector<DataFlowNode*> root_nodes = data_flow_internal::find_root_nodes(m_data_flow_graph.get());

    EXPECT_EQ(root_nodes.size(), 5);

    // First 3 nodes are non-padding roots.
    std::vector<DataFlowNode*> non_padding_roots =
        std::vector<DataFlowNode*>(root_nodes.begin(), root_nodes.begin() + 3);
    EXPECT_THAT(non_padding_roots, UnorderedElementsAre(root1, root3, root5));

    // Last 2 nodes are padding roots.
    std::vector<DataFlowNode*> padding_roots = std::vector<DataFlowNode*>(root_nodes.begin() + 3, root_nodes.end());
    EXPECT_THAT(padding_roots, UnorderedElementsAre(root2_padding, root4_padding));
}

TEST_F(Pipegen2_DataFlowCalculatorInternal_FindRootNodes, IsolatedNode)
{
    DataFlowNodeMock* isolated_node = create_non_padding_df_node();

    verify_log_assert(
        [&]() { data_flow_internal::find_root_nodes(m_data_flow_graph.get()); },
        "^Expecting non-isolated nodes in graph but found node " + std::to_string(isolated_node->get_id()) + ".*");
}

/**********************************************************************************************************************
    Tests for function: data_flow_internal::find_leaf_nodes
**********************************************************************************************************************/

class Pipegen2_DataFlowCalculatorInternal_FindLeafNodes : public testing::Test
{
protected:
    void SetUp() override
    {
        m_node_id = 100;
        m_data_flow_graph = std::make_unique<DataFlowGraph>();
    }

    DataFlowNodeMock* create_df_node_mock()
    {
        std::unique_ptr<DataFlowNodeMock> df_node_mock = std::make_unique<NiceMock<DataFlowNodeMock>>();
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        DataFlowNodeMock* df_node_mock_ptr = df_node_mock.get();
        m_data_flow_graph->add_node(std::move(df_node_mock));

        return df_node_mock_ptr;
    }

    NodeId m_node_id;
    std::unique_ptr<DataFlowGraph> m_data_flow_graph;
};

TEST_F(Pipegen2_DataFlowCalculatorInternal_FindLeafNodes, HasMultipleLeafNodes)
{
    DataFlowNodeMock* root = create_df_node_mock();
    DataFlowNodeMock* leaf1 = create_df_node_mock();
    DataFlowNodeMock* leaf2 = create_df_node_mock();
    DataFlowNodeMock* leaf3 = create_df_node_mock();

    connect_data_flow_graph({make_df_edge(root, leaf1), make_df_edge(root, leaf2), make_df_edge(root, leaf3)});

    std::vector<DataFlowNode*> leaf_nodes = data_flow_internal::find_leaf_nodes(m_data_flow_graph.get());

    EXPECT_THAT(leaf_nodes, UnorderedElementsAre(leaf1, leaf2, leaf3));
}

TEST_F(Pipegen2_DataFlowCalculatorInternal_FindLeafNodes, IsolatedNode)
{
    DataFlowNodeMock* isolated_node = create_df_node_mock();

    verify_log_assert(
        [&]() { data_flow_internal::find_leaf_nodes(m_data_flow_graph.get()); },
        "^Expecting non-isolated nodes in graph but found node " + std::to_string(isolated_node->get_id()) + ".*");
}