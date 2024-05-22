// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "data_flow_calculator/connected_data_flow_graph_finder.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

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

/**********************************************************************************************************************
    Tests for function: ConnectedDataFlowGraphFinder::group_connected_data_flow_nodes
**********************************************************************************************************************/

class Pipegen2_ConnectedDataFlowGraphFinder_GroupConnectedDataFlowNodes : public testing::Test
{
protected:
    void SetUp() override { m_node_id = 100; }

    std::pair<DataFlowNode*, NodeId> make_df_node_mock()
    {
        m_data_flow_nodes.emplace_back(std::make_unique<NiceMock<DataFlowNodeMock>>());
        DataFlowNode* df_node = m_data_flow_nodes.back().get();

        // Need to cast to concrete mock class in order for ON_CALL macro to work.
        DataFlowNodeMock* df_node_mock = dynamic_cast<DataFlowNodeMock*>(df_node);
        ON_CALL(*df_node_mock, get_id()).WillByDefault(Return(m_node_id++));

        return std::make_pair(df_node, df_node->get_id());
    }

    std::vector<std::unique_ptr<DataFlowGraph>> group_connected_data_flow_nodes()
    {
        ConnectedDataFlowGraphFinder connected_data_flow_graph_finder;
        return connected_data_flow_graph_finder.group_connected_data_flow_nodes(std::move(m_data_flow_nodes));
    }

    std::vector<std::unique_ptr<DataFlowNode>> m_data_flow_nodes;
    NodeId m_node_id;
};

TEST_F(Pipegen2_ConnectedDataFlowGraphFinder_GroupConnectedDataFlowNodes, SingleConnectedComponent)
{
    auto [src_df_node_1, src_node_id_1] = make_df_node_mock();
    auto [src_df_node_2, src_node_id_2] = make_df_node_mock();
    auto [gather_df_node, gather_node_id] = make_df_node_mock();
    auto [dest_df_node_1, dest_node_id_1] = make_df_node_mock();
    auto [dest_df_node_2, dest_node_id_2] = make_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(src_df_node_1, gather_df_node),
         make_df_edge(src_df_node_2, gather_df_node),
         make_df_edge(gather_df_node, dest_df_node_1),
         make_df_edge(gather_df_node, dest_df_node_2)});

    std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs = group_connected_data_flow_nodes();
    EXPECT_EQ(connected_data_flow_graphs.size(), 1);

    DataFlowGraph* data_flow_graph = connected_data_flow_graphs[0].get();
    verify_data_flow_graph_node_ids(
        data_flow_graph, {src_node_id_1, src_node_id_2, gather_node_id, dest_node_id_1, dest_node_id_2});
}

TEST_F(Pipegen2_ConnectedDataFlowGraphFinder_GroupConnectedDataFlowNodes, MultipleConnectedComponents)
{
    auto [g1_src_df_node_1, g1_src_node_id_1] = make_df_node_mock();
    auto [g0_src_df_node, g0_src_node_id] = make_df_node_mock();
    auto [g1_src_df_node_2, g1_src_node_id_2] = make_df_node_mock();
    auto [g0_dest_df_node_1, g0_dest_node_id_1] = make_df_node_mock();
    auto [g1_gather_df_node, g1_gather_node_id] = make_df_node_mock();
    auto [g0_dest_df_node_2, g0_dest_node_id_2] = make_df_node_mock();
    auto [g1_dest_df_node, g1_dest_node_id] = make_df_node_mock();

    connect_data_flow_graph(
        {make_df_edge(g1_src_df_node_1, g1_gather_df_node),
         make_df_edge(g1_src_df_node_2, g1_gather_df_node),
         make_df_edge(g1_gather_df_node, g1_dest_df_node)});

    connect_data_flow_graph({
        make_df_edge(g0_src_df_node, g0_dest_df_node_1),
        make_df_edge(g0_src_df_node, g0_dest_df_node_2),
    });

    std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs = group_connected_data_flow_nodes();
    EXPECT_EQ(connected_data_flow_graphs.size(), 2);

    std::sort(
        connected_data_flow_graphs.begin(),
        connected_data_flow_graphs.end(),
        [](const std::unique_ptr<DataFlowGraph>& left, const std::unique_ptr<DataFlowGraph>& right)
        { return left->get_nodes().size() < right->get_nodes().size(); });

    verify_data_flow_graph_node_ids(
        connected_data_flow_graphs[0].get(), {g0_src_node_id, g0_dest_node_id_1, g0_dest_node_id_2});

    verify_data_flow_graph_node_ids(
        connected_data_flow_graphs[1].get(), {g1_src_node_id_1, g1_src_node_id_2, g1_gather_node_id, g1_dest_node_id});
}