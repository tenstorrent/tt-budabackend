// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <utility>
#include <vector>

// clang-format off
#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_node.h"
#include "model/data_flow/data_flow_node_input.h"
#include "model/data_flow/subgraph_leaf_groups.h"

#include "mocks/model/data_flow_graph/df_node_mocks.h"
// clang-format on

namespace pipegen2
{
namespace unit_test_utils
{

// Helpers structure which holds information about a single edge in data flow graph.
struct DFGraphEdgeInfo
{
    DataFlowNode* source_node;
    DataFlowNode* dest_node;
    std::vector<unsigned int> offsets;
};

// Helper function which connects all source dest pairs in the given lists of data flow graph edges.
void connect_data_flow_graph(const std::vector<DFGraphEdgeInfo>& edges);

// Helper function which creates a data flow graph edge info from the given source and dest mock data flow node unique
// pointers.
DFGraphEdgeInfo make_df_edge(
    const std::unique_ptr<DataFlowNodeMock>& src_df_node, const std::unique_ptr<DataFlowNodeMock>& dest_df_node);

// Helper function which creates a data flow graph edge info from the given source and dest data flow node raw
// pointers.
DFGraphEdgeInfo make_df_edge(DataFlowNode* src_df_node, DataFlowNode* dest_df_node);

// Helper function which creates a data flow graph edge info from the given source and dest mock data flow node unique
// pointers and the given offsets.
DFGraphEdgeInfo make_df_edge(
    const std::unique_ptr<DataFlowNodeMock>& src_df_node,
    const std::unique_ptr<DataFlowNodeMock>& dest_df_node,
    const std::vector<unsigned int>& offsets);

// Helper function which creates a data flow graph edge info from the given source and dest data flow node raw
// pointers and the given offsets.
DFGraphEdgeInfo make_df_edge(
    DataFlowNode* src_df_node, DataFlowNode* dest_df_node, const std::vector<unsigned int>& offsets);

// Helper creator function for data flow node input objects.
DataFlowNodeInput make_df_input(const std::unique_ptr<DataFlowNodeMock>& src_node, unsigned int offset);

// Helper function which configures inputs of the given data flow node.
void configure_df_inputs(
    const std::unique_ptr<DataFlowNodeMock>& df_node, const std::vector<DataFlowNodeInput>& df_inputs);

// Helper functions which verifies that the obtained phases match the expected phases.
void verify_phases(const std::vector<PhaseInfo>& obtained_phases, const std::vector<PhaseInfo>& expected_phases);

// Helper function which verifies that the obtained data flow inputes of a given data flow node match the expected ones.
void verify_data_flow_inputs(DataFlowNode* df_node, const std::vector<DataFlowNodeInput>& expected_df_inputs);

// Helper function which verifies that the obtained sources and destinations of a given data flow node match the
// expected ones.
void verify_sources_and_destinations(
    DataFlowNode* df_node,
    const std::unordered_set<DataFlowNode*>& expected_sources,
    const std::unordered_set<DataFlowNode*>& expected_destinations);

// Helper function which verifies that the obtained node ids from the given data flow graph match the expected list of
// node ids.
void verify_data_flow_graph_node_ids(
    DataFlowGraph* data_flow_graph, const std::unordered_set<NodeId>& expected_node_ids);

// Helper function which verifies that the data flow inputs in the given data flow node input range match the expected
// list of inputs.
void verify_data_flow_node_input_range(
    const DataFlowNodeInputRange& input_range, const std::vector<DataFlowNodeInput>& expected_inputs);

// Verifies that all leaf groups within a given subgraph are empty.
void verify_empty_subgraph_leaf_group(
    const std::unordered_map<unsigned int, LeafGroupOrder>& subgraph_leaves, unsigned int subgraph_id);

// Helper function which verifies that all nodes in a given list of nodes has the expected number of iterations.
void verify_data_flow_nodes_num_iterations(
    const std::vector<DataFlowNode*>& data_flow_nodes, unsigned int expected_num_iterations);

// Helper function which verifies that every data flow node within a given list has the expected number of iterations,
// based on the node ID -> expected number of iterations mapping.
void verify_data_flow_nodes_num_iterations(
    const std::vector<DataFlowNode*>& data_flow_nodes,
    const std::unordered_map<NodeId, unsigned int>& node_id_to_expected_num_iterations);

// Checks that every receiver phase corresponds to contiguous subset of sender phases that sum up to the same number of
// messages.
void check_phase_alignment(
    const std::vector<PhaseInfo>& sender_phases,
    const std::vector<PhaseInfo>& receiver_phases,
    const unsigned int receiver_max_num_tiles_per_phase);

// Verifies that all transfer limits have been computed correctly for a given data flow node.
void verify_transfer_limits(
    DataFlowNode* df_node,
    bool expected_is_on_single_source_path,
    unsigned int expected_num_iterations,
    unsigned int expected_tiles_to_send,
    unsigned int expected_number_of_paths_through_df_node,
    unsigned int expected_subtree_common_divisor,
    unsigned int expected_max_num_tiles_per_phase);

}  // namespace unit_test_utils
}  // namespace pipegen2