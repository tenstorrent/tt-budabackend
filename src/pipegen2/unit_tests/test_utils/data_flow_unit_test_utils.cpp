// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_unit_test_utils.h"

using ::testing::Contains;
using ::testing::Key;
using ::testing::UnorderedElementsAreArray;

namespace pipegen2
{
namespace unit_test_utils
{

void connect_data_flow_graph(const std::vector<DFGraphEdgeInfo>& edges)
{
    for (const DFGraphEdgeInfo& df_edge_info : edges)
    {
        df_edge_info.source_node->add_dest(df_edge_info.dest_node);
        df_edge_info.dest_node->add_source(df_edge_info.source_node);

        for (unsigned int offset : df_edge_info.offsets)
        {
            df_edge_info.dest_node->add_input(df_edge_info.source_node, offset);
        }
    }
}

DFGraphEdgeInfo make_df_edge(
    const std::unique_ptr<DataFlowNodeMock>& src_df_node, const std::unique_ptr<DataFlowNodeMock>& dest_df_node)
{
    return make_df_edge(src_df_node, dest_df_node, {0u});
}

DFGraphEdgeInfo make_df_edge(DataFlowNode* src_df_node, DataFlowNode* dest_df_node)
{
    return make_df_edge(src_df_node, dest_df_node, {0u});
}

DFGraphEdgeInfo make_df_edge(
    const std::unique_ptr<DataFlowNodeMock>& src_df_node,
    const std::unique_ptr<DataFlowNodeMock>& dest_df_node,
    const std::vector<unsigned int>& offsets)
{
    return make_df_edge(src_df_node.get(), dest_df_node.get(), offsets);
}

DFGraphEdgeInfo make_df_edge(
    DataFlowNode* src_df_node, DataFlowNode* dest_df_node, const std::vector<unsigned int>& offsets)
{
    return {.source_node = src_df_node, .dest_node = dest_df_node, .offsets = offsets};
}

DataFlowNodeInput make_df_input(const std::unique_ptr<DataFlowNodeMock>& src_node, unsigned int offset)
{
    return DataFlowNodeInput(src_node.get(), offset);
}

void configure_df_inputs(
    const std::unique_ptr<DataFlowNodeMock>& df_node, const std::vector<DataFlowNodeInput>& df_inputs)
{
    for (const DataFlowNodeInput& df_input : df_inputs)
    {
        df_node->add_input(df_input.get_node(), df_input.get_offset());
    }
}

void verify_phases(const std::vector<PhaseInfo>& obtained_phases, const std::vector<PhaseInfo>& expected_phases)
{
    EXPECT_EQ(obtained_phases.size(), expected_phases.size());

    for (unsigned int i = 0; i < expected_phases.size(); ++i)
    {
        const PhaseInfo& obtained_phase = obtained_phases[i];
        const PhaseInfo& expected_phase = expected_phases[i];

        EXPECT_EQ(obtained_phase.phase_offset, expected_phase.phase_offset);
        EXPECT_EQ(obtained_phase.data_offset, expected_phase.data_offset);
        EXPECT_EQ(obtained_phase.num_msgs, expected_phase.num_msgs);
    }
}

void verify_data_flow_inputs(DataFlowNode* df_node, const std::vector<DataFlowNodeInput>& expected_df_inputs)
{
    const std::vector<DataFlowNodeInput>& observed_df_inputs = df_node->get_all_inputs();
    EXPECT_EQ(observed_df_inputs.size(), expected_df_inputs.size());

    for (std::size_t i = 0; i < observed_df_inputs.size(); ++i)
    {
        EXPECT_EQ(observed_df_inputs[i].get_node(), expected_df_inputs[i].get_node());
        EXPECT_EQ(observed_df_inputs[i].get_offset(), expected_df_inputs[i].get_offset());
    }
}

void verify_sources_and_destinations(
    DataFlowNode* df_node,
    const std::unordered_set<DataFlowNode*>& expected_sources,
    const std::unordered_set<DataFlowNode*>& expected_destinations)
{
    EXPECT_THAT(df_node->get_sources(), UnorderedElementsAreArray(expected_sources));
    EXPECT_THAT(df_node->get_destinations(), UnorderedElementsAreArray(expected_destinations));
}

void verify_data_flow_graph_node_ids(
    DataFlowGraph* data_flow_graph, const std::unordered_set<NodeId>& expected_node_ids)
{
    EXPECT_EQ(data_flow_graph->get_nodes().size(), expected_node_ids.size());

    std::unordered_set<NodeId> observed_node_ids;
    for (const std::unique_ptr<DataFlowNode>& df_node : data_flow_graph->get_nodes())
    {
        observed_node_ids.insert(df_node->get_id());
    }

    EXPECT_THAT(observed_node_ids, UnorderedElementsAreArray(expected_node_ids));
}

void verify_data_flow_node_input_range(
    const DataFlowNodeInputRange& input_range, const std::vector<DataFlowNodeInput>& expected_inputs)
{
    EXPECT_EQ(input_range.size(), expected_inputs.size());
    auto expected_it = expected_inputs.begin();
    for (auto observed_it = input_range.begin(); observed_it != input_range.end(); ++observed_it, ++expected_it)
    {
        EXPECT_EQ(observed_it->get_node(), expected_it->get_node());
        EXPECT_EQ(observed_it->get_offset(), expected_it->get_offset());
    }
}

void verify_empty_subgraph_leaf_group(
    const std::unordered_map<unsigned int, LeafGroupOrder>& subgraph_leaves, unsigned int subgraph_id)
{
    EXPECT_THAT(subgraph_leaves, Contains(Key(subgraph_id)));

    const LeafGroupOrder& subgraph_leaf_groups = subgraph_leaves.at(subgraph_id);

    for (const LeafGroup& leaf_group : subgraph_leaf_groups.get_leaf_groups())
    {
        EXPECT_TRUE(leaf_group.empty());
    }
}

void verify_data_flow_nodes_num_iterations(
    const std::vector<DataFlowNode*>& data_flow_nodes, unsigned int expected_num_iterations)
{
    for (const DataFlowNode* df_node : data_flow_nodes)
    {
        EXPECT_EQ(df_node->get_num_iterations(), expected_num_iterations);
    }
}

void verify_data_flow_nodes_num_iterations(
    const std::vector<DataFlowNode*>& data_flow_nodes,
    const std::unordered_map<NodeId, unsigned int>& node_id_to_expected_num_iterations)
{
    for (const DataFlowNode* df_node : data_flow_nodes)
    {
        NodeId node_id = df_node->get_id();
        EXPECT_THAT(node_id_to_expected_num_iterations, Contains(Key(node_id)));

        EXPECT_EQ(df_node->get_num_iterations(), node_id_to_expected_num_iterations.at(node_id));
    }
}

void check_phase_alignment(
    const std::vector<PhaseInfo>& sender_phases,
    const std::vector<PhaseInfo>& receiver_phases,
    const unsigned int receiver_max_num_tiles_per_phase)
{
    size_t sender_phase_index = 0;
    for (const PhaseInfo& receiver_phase : receiver_phases)
    {
        unsigned int sender_phase_sum = 0;
        while (sender_phase_index < sender_phases.size() &&
               sender_phase_sum + sender_phases[sender_phase_index].num_msgs <= receiver_phase.num_msgs)
        {
            sender_phase_sum += sender_phases[sender_phase_index].num_msgs;
            EXPECT_LE(sender_phase_sum, receiver_max_num_tiles_per_phase);
            ++sender_phase_index;
        }
        EXPECT_EQ(sender_phase_sum, receiver_phase.num_msgs);
        sender_phase_sum = 0;
    }
}

void verify_transfer_limits(
    DataFlowNode* df_node,
    bool expected_is_on_single_source_path,
    unsigned int expected_num_iterations,
    unsigned int expected_tiles_to_send,
    unsigned int expected_number_of_paths_through_df_node,
    unsigned int expected_subtree_common_divisor,
    unsigned int expected_max_num_tiles_per_phase)
{
    EXPECT_EQ(df_node->is_on_single_source_path(), expected_is_on_single_source_path);
    EXPECT_EQ(df_node->get_num_iterations(), expected_num_iterations);
    EXPECT_EQ(df_node->get_tiles_to_send(), expected_tiles_to_send);
    EXPECT_EQ(df_node->get_number_of_unique_df_paths(), expected_number_of_paths_through_df_node);
    EXPECT_EQ(df_node->get_subtree_common_divisor(), expected_subtree_common_divisor);
    EXPECT_EQ(df_node->get_max_num_tiles_per_phase(), expected_max_num_tiles_per_phase);
}

}  // namespace unit_test_utils
}  // namespace pipegen2
