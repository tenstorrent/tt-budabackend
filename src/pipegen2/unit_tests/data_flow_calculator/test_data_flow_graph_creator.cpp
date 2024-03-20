// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/data_flow_graph_creator.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mocks/model/data_flow_graph/df_node_mocks.h"
#include "mocks/model/rational_graph/rg_nodes_mocks.h"
#include "mocks/model/rational_graph/rg_pipes_mocks.h"
#include "model/data_flow/data_flow_graph.h"
#include "model/rational_graph/rational_graph.h"
#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "test_utils/data_flow_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

using ::testing::Contains;
using ::testing::Key;
using ::testing::NiceMock;
using ::testing::Return;

/**********************************************************************************************************************
    Tests for function: DataFlowGraphCreator::create_data_flow_graphs
**********************************************************************************************************************/

class UT_DataFlowGraphCreator : public DataFlowGraphCreator
{
public:
    // Mock implementation of splitting the data flow graph into connected components which is by default done by
    // another component - ConnectedDataFlowGraphFinder. Mock implementation is to move all data flow nodes to a single
    // data flow graph.
    std::vector<std::unique_ptr<DataFlowGraph>> group_connected_data_flow_nodes(
        std::vector<std::unique_ptr<DataFlowNode>>&& data_flow_nodes) override
    {
        std::unique_ptr<DataFlowGraph> data_flow_graph = std::make_unique<DataFlowGraph>();
        for (std::unique_ptr<DataFlowNode>& data_flow_node : data_flow_nodes)
        {
            data_flow_graph->add_node(std::move(data_flow_node));
        }

        std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs;
        connected_data_flow_graphs.push_back(std::move(data_flow_graph));

        return connected_data_flow_graphs;
    }
};

class Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs : public testing::Test
{
protected:
    void SetUp() override
    {
        m_rg_node_id = 100;
    }

    std::pair<RGBaseNode*, NodeId> make_rg_node_mock()
    {
        m_rg_nodes.push_back(std::make_unique<NiceMock<RGNodeStructuralMock>>(m_rg_node_id++));
        RGBaseNode* rg_node = m_rg_nodes.back().get();

        return std::make_pair(rg_node, rg_node->get_id());
    }

    RGBasePipe* make_rg_pipe_mock(
        const std::vector<PipeInput>& pipe_inputs,
        const std::vector<const RGBaseNode*>& pipe_outputs)
    {
        m_rg_pipes.push_back(std::make_unique<NiceMock<RGPipeStructuralMock>>());
        RGBasePipe* rg_pipe = m_rg_pipes.back().get();

        for (const PipeInput& pipe_input : pipe_inputs)
        {
            rg_pipe->add_input(pipe_input);
        }
        for (const RGBaseNode* pipe_output : pipe_outputs)
        {
            rg_pipe->add_output_node(pipe_output);
        }

        return rg_pipe;
    }

    std::unique_ptr<RationalGraph> get_rational_graph()
    {
        return std::make_unique<RationalGraph>(
            std::move(m_rg_nodes), std::move(m_rg_pipes), false /* is_doing_pcie_transfer */);
    }

    // Take note that this creates a rational graph from memeber lists of nodes and pipes, the lists are moved into the
    // newly created rational graph object which invalidates any exists RGBaseNode and RGBasePipe pointers.
    std::vector<std::unique_ptr<DataFlowGraph>> create_data_flow_graphs()
    {
        std::unique_ptr<RationalGraph> rational_graph = get_rational_graph();

        std::vector<std::unique_ptr<DataFlowGraph>> connected_data_flow_graphs =
            m_data_flow_graph_creator.create_data_flow_graphs(rational_graph.get());

        for (const std::unique_ptr<DataFlowGraph>& df_graph : connected_data_flow_graphs)
        {
            populate_node_id_to_df_node_mapping(df_graph.get());
        }

        return connected_data_flow_graphs;
    }

    void populate_node_id_to_df_node_mapping(const DataFlowGraph* data_flow_graph)
    {
        for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_graph->get_nodes())
        {
            m_rg_node_id_to_df_node[df_node->get_id()] = df_node.get();
        }
    }

    DataFlowNode* get_df_node(const NodeId node_id)
    {
        EXPECT_THAT(m_rg_node_id_to_df_node, Contains(Key(node_id)));

        return m_rg_node_id_to_df_node.at(node_id);
    }

    UT_DataFlowGraphCreator m_data_flow_graph_creator;
    NodeId m_rg_node_id;
    std::unordered_map<NodeId, DataFlowNode*> m_rg_node_id_to_df_node;

private:
    std::vector<std::unique_ptr<RGBaseNode>> m_rg_nodes;
    std::vector<std::unique_ptr<RGBasePipe>> m_rg_pipes;
};

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, SingleConnectionWithMultiplePipeInputs)
{
    auto [src_rg_node, src_node_id] = make_rg_node_mock();
    auto [dest_rg_node, dest_node_id] = make_rg_node_mock();

    make_rg_pipe_mock(
        {PipeInput(src_rg_node, 0), PipeInput(src_rg_node, 1), PipeInput(src_rg_node, 2), PipeInput(src_rg_node, 3)},
        {dest_rg_node});

    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 2);

    DataFlowNode* src_df_node = get_df_node(src_node_id);
    DataFlowNode* dest_df_node = get_df_node(dest_node_id);

    verify_sources_and_destinations(src_df_node, {}, {dest_df_node});
    verify_sources_and_destinations(dest_df_node, {src_df_node}, {});

    verify_data_flow_inputs(dest_df_node, {
        DataFlowNodeInput(src_df_node, 0),
        DataFlowNodeInput(src_df_node, 1),
        DataFlowNodeInput(src_df_node, 2),
        DataFlowNodeInput(src_df_node, 3)
    });
}

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, SingleConnectionWithMultiplePipeInputsAndOutputs)
{
    auto [src_rg_node, src_node_id] = make_rg_node_mock();
    auto [dest_rg_node_1, dest_node_id_1] = make_rg_node_mock();
    auto [dest_rg_node_2, dest_node_id_2] = make_rg_node_mock();
    auto [dest_rg_node_3, dest_node_id_3] = make_rg_node_mock();
    auto [dest_rg_node_4, dest_node_id_4] = make_rg_node_mock();

    make_rg_pipe_mock(
        {PipeInput(src_rg_node, 0), PipeInput(src_rg_node, 1)},
        {dest_rg_node_1, dest_rg_node_2, dest_rg_node_3, dest_rg_node_4});

    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 5);

    DataFlowNode* src_df_node = get_df_node(src_node_id);
    DataFlowNode* dest_df_node_1 = get_df_node(dest_node_id_1);
    DataFlowNode* dest_df_node_2 = get_df_node(dest_node_id_2);
    DataFlowNode* dest_df_node_3 = get_df_node(dest_node_id_3);
    DataFlowNode* dest_df_node_4 = get_df_node(dest_node_id_4);

    verify_sources_and_destinations(src_df_node, {}, {dest_df_node_1, dest_df_node_2, dest_df_node_3, dest_df_node_4});
    verify_sources_and_destinations(dest_df_node_1, {src_df_node}, {});
    verify_sources_and_destinations(dest_df_node_2, {src_df_node}, {});
    verify_sources_and_destinations(dest_df_node_3, {src_df_node}, {});
    verify_sources_and_destinations(dest_df_node_4, {src_df_node}, {});

    const std::vector<DataFlowNodeInput> expected_df_inputs = {
        DataFlowNodeInput(src_df_node, 0), DataFlowNodeInput(src_df_node, 1)};
    verify_data_flow_inputs(dest_df_node_1, expected_df_inputs);
    verify_data_flow_inputs(dest_df_node_2, expected_df_inputs);
    verify_data_flow_inputs(dest_df_node_3, expected_df_inputs);
    verify_data_flow_inputs(dest_df_node_4, expected_df_inputs);
}

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, GatherNode)
{
    auto [src_rg_node_1, src_node_id_1] = make_rg_node_mock();
    auto [src_rg_node_2, src_node_id_2] = make_rg_node_mock();
    auto [src_rg_node_3, src_node_id_3] = make_rg_node_mock();
    auto [dest_rg_node, dest_node_id] = make_rg_node_mock();

    make_rg_pipe_mock(
        {PipeInput(src_rg_node_1, 0), PipeInput(src_rg_node_2, 55),
         PipeInput(src_rg_node_1, 1), PipeInput(src_rg_node_3, 42)},
        {dest_rg_node});

    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 4);

    DataFlowNode* src_df_node_1 = get_df_node(src_node_id_1);
    DataFlowNode* src_df_node_2 = get_df_node(src_node_id_2);
    DataFlowNode* src_df_node_3 = get_df_node(src_node_id_3);
    DataFlowNode* dest_df_node = get_df_node(dest_node_id);

    verify_sources_and_destinations(src_df_node_1, {}, {dest_df_node});
    verify_sources_and_destinations(src_df_node_2, {}, {dest_df_node});
    verify_sources_and_destinations(src_df_node_3, {}, {dest_df_node});
    verify_sources_and_destinations(dest_df_node, {src_df_node_1, src_df_node_2, src_df_node_3}, {});

    verify_data_flow_inputs(dest_df_node, {
        DataFlowNodeInput(src_df_node_1, 0),
        DataFlowNodeInput(src_df_node_2, 55),
        DataFlowNodeInput(src_df_node_1, 1),
        DataFlowNodeInput(src_df_node_3, 42)
    });
}

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, ForkNode)
{
    auto [src_rg_node, src_node_id] = make_rg_node_mock();
    auto [fork_rg_node_1, fork_node_id_1] = make_rg_node_mock();
    auto [fork_rg_node_2, fork_node_id_2] = make_rg_node_mock();
    auto [dest_rg_node_1, dest_node_id_1] = make_rg_node_mock();
    auto [dest_rg_node_2, dest_node_id_2] = make_rg_node_mock();

    make_rg_pipe_mock(
        {PipeInput(src_rg_node, 4), PipeInput(src_rg_node, 2), PipeInput(src_rg_node, 6)}, {fork_rg_node_1});
    make_rg_pipe_mock({PipeInput(src_rg_node, 0), PipeInput(src_rg_node, 0)}, {fork_rg_node_2});
    make_rg_pipe_mock({PipeInput(fork_rg_node_1, 0)}, {dest_rg_node_1});
    make_rg_pipe_mock({PipeInput(fork_rg_node_2, 0)}, {dest_rg_node_2});

    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 5);

    DataFlowNode* src_df_node = get_df_node(src_node_id);
    DataFlowNode* fork_df_node_1 = get_df_node(fork_node_id_1);
    DataFlowNode* fork_df_node_2 = get_df_node(fork_node_id_2);
    DataFlowNode* dest_df_node_1 = get_df_node(dest_node_id_1);
    DataFlowNode* dest_df_node_2 = get_df_node(dest_node_id_2);

    verify_sources_and_destinations(src_df_node, {}, {fork_df_node_1, fork_df_node_2});
    verify_sources_and_destinations(fork_df_node_1, {src_df_node}, {dest_df_node_1});
    verify_sources_and_destinations(fork_df_node_2, {src_df_node}, {dest_df_node_2});
    verify_sources_and_destinations(dest_df_node_1, {fork_df_node_1}, {});
    verify_sources_and_destinations(dest_df_node_2, {fork_df_node_2}, {});

    verify_data_flow_inputs(
        fork_df_node_1,
        {DataFlowNodeInput(src_df_node, 4), DataFlowNodeInput(src_df_node, 2), DataFlowNodeInput(src_df_node, 6)});
    verify_data_flow_inputs(
        fork_df_node_2, {DataFlowNodeInput(src_df_node, 0), DataFlowNodeInput(src_df_node, 0)});
    verify_data_flow_inputs(dest_df_node_1, {DataFlowNodeInput(fork_df_node_1, 0)});
    verify_data_flow_inputs(dest_df_node_2, {DataFlowNodeInput(fork_df_node_2, 0)});
}

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, DisconnectedRationalGraphs)
{
    auto [src_rg_node_1, src_node_id_1] = make_rg_node_mock();
    auto [src_rg_node_2, src_node_id_2] = make_rg_node_mock();
    auto [dest_rg_node_1, dest_node_id_1] = make_rg_node_mock();
    auto [dest_rg_ndoe_2, dest_node_id_2] = make_rg_node_mock();

    make_rg_pipe_mock({PipeInput(src_rg_node_1)}, {dest_rg_node_1});
    make_rg_pipe_mock({PipeInput(src_rg_node_2)}, {dest_rg_ndoe_2});

    // Note: number of data flow graphs will be 1 because the the function which groups connected data flow nodes is
    // mocked to always return a single data flow graph.
    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 4);

    DataFlowNode* src_df_node_1 = get_df_node(src_node_id_1);
    DataFlowNode* src_df_node_2 = get_df_node(src_node_id_2);
    DataFlowNode* dest_df_node_1 = get_df_node(dest_node_id_1);
    DataFlowNode* dest_df_node_2 = get_df_node(dest_node_id_2);

    verify_sources_and_destinations(src_df_node_1, {}, {dest_df_node_1});
    verify_sources_and_destinations(dest_df_node_1, {src_df_node_1}, {});
    verify_data_flow_inputs(dest_df_node_1, {DataFlowNodeInput(src_df_node_1, 0)});

    verify_sources_and_destinations(src_df_node_2, {}, {dest_df_node_2});
    verify_sources_and_destinations(dest_df_node_2, {src_df_node_2}, {});
    verify_data_flow_inputs(dest_df_node_2, {DataFlowNodeInput(src_df_node_2, 0)});
}

TEST_F(Pipegen2_DataFlowGraphCreator_CreateDataFlowGraphs, CompoundRationalGraph)
{
    auto [src_rg_node_1, src_node_id_1] = make_rg_node_mock();
    auto [src_rg_node_2, src_node_id_2] = make_rg_node_mock();
    auto [gather_rg_node_1, gather_node_id_1] = make_rg_node_mock();
    auto [gather_rg_node_2, gather_node_id_2] = make_rg_node_mock();
    auto [union_rg_node, union_node_id] = make_rg_node_mock();
    auto [dest_rg_node_1, dest_node_id_1] = make_rg_node_mock();
    auto [dest_rg_node_2, dest_node_id_2] = make_rg_node_mock();
    auto [dest_rg_node_3, dest_node_id_3] = make_rg_node_mock();

    make_rg_pipe_mock({PipeInput(src_rg_node_1, 16), PipeInput(src_rg_node_2, 2)}, {gather_rg_node_1});
    make_rg_pipe_mock({PipeInput(src_rg_node_2, 4), PipeInput(src_rg_node_1, 8)}, {gather_rg_node_2});
    make_rg_pipe_mock({PipeInput(gather_rg_node_1)}, {union_rg_node});
    make_rg_pipe_mock({PipeInput(gather_rg_node_2)}, {union_rg_node});
    make_rg_pipe_mock({PipeInput(union_rg_node)}, {dest_rg_node_1, dest_rg_node_2, dest_rg_node_3});

    std::vector<std::unique_ptr<DataFlowGraph>> df_graphs = create_data_flow_graphs();
    EXPECT_EQ(df_graphs.size(), 1);

    DataFlowGraph* df_graph = df_graphs[0].get();
    EXPECT_EQ(df_graph->get_nodes().size(), 8);

    DataFlowNode* src_df_node_1 = get_df_node(src_node_id_1);
    DataFlowNode* src_df_node_2 = get_df_node(src_node_id_2);
    DataFlowNode* gather_df_node_1 = get_df_node(gather_node_id_1);
    DataFlowNode* gather_df_node_2 = get_df_node(gather_node_id_2);
    DataFlowNode* union_df_node = get_df_node(union_node_id);
    DataFlowNode* dest_df_node_1 = get_df_node(dest_node_id_1);
    DataFlowNode* dest_df_node_2 = get_df_node(dest_node_id_2);
    DataFlowNode* dest_df_node_3 = get_df_node(dest_node_id_3);

    verify_sources_and_destinations(src_df_node_1, {}, {gather_df_node_1, gather_df_node_2});
    verify_sources_and_destinations(src_df_node_2, {}, {gather_df_node_1, gather_df_node_2});
    verify_sources_and_destinations(gather_df_node_1, {src_df_node_1, src_df_node_2}, {union_df_node});
    verify_sources_and_destinations(gather_df_node_2, {src_df_node_1, src_df_node_2}, {union_df_node});
    verify_sources_and_destinations(
        union_df_node, {gather_df_node_1, gather_df_node_2}, {dest_df_node_1, dest_df_node_2, dest_df_node_3});
    verify_sources_and_destinations(dest_df_node_1, {union_df_node}, {});
    verify_sources_and_destinations(dest_df_node_2, {union_df_node}, {});
    verify_sources_and_destinations(dest_df_node_3, {union_df_node}, {});

    verify_data_flow_inputs(
        gather_df_node_1, {DataFlowNodeInput(src_df_node_1, 16), DataFlowNodeInput(src_df_node_2, 2)});
    verify_data_flow_inputs(
        gather_df_node_2, {DataFlowNodeInput(src_df_node_2, 4), DataFlowNodeInput(src_df_node_1, 8)});
    verify_data_flow_inputs(
        union_df_node, {DataFlowNodeInput(gather_df_node_1, 0), DataFlowNodeInput(gather_df_node_2, 0)});
    verify_data_flow_inputs(dest_df_node_1, {DataFlowNodeInput(union_df_node, 0)});
    verify_data_flow_inputs(dest_df_node_2, {DataFlowNodeInput(union_df_node, 0)});
    verify_data_flow_inputs(dest_df_node_3, {DataFlowNodeInput(union_df_node, 0)});
}