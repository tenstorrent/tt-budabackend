// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <stack>
#include <unordered_set>
#include <vector>

namespace pipegen2
{

class DataFlowNode;

namespace data_flow_internal
{

// Starts from a source node and sets single source path marker on such paths.
bool find_single_source_paths(DataFlowNode* source_node, bool is_upstream_single_source);

// Calculates num iterations field for all data flow nodes which can be reached from any root node.
void calculate_num_iterations(const std::vector<DataFlowNode*>& leaf_nodes,
                              const std::vector<DataFlowNode*>& root_nodes,
                              unsigned int max_num_tiles_per_phase,
                              unsigned int max_num_phases_per_iteration);

// Returns whether the given node is the end of a single source edge.
bool is_end_of_single_source_edge(DataFlowNode* node);

// Calculate how many iterations will happen during the epoch in the data flow graph given with its destinations
// and the root node of the single source path.
unsigned int calculate_num_iterations_for_single_source_path(const DataFlowNode* root_node,
                                                             const std::vector<DataFlowNode*>& leaf_nodes,
                                                             unsigned int max_num_tiles_per_phase,
                                                             unsigned int max_num_phases_per_iteration);

// Returns if all leaf nodes in data flow graph can do epoch in a single iterations.
bool can_all_leaf_nodes_do_epoch_in_single_iteration(const std::vector<DataFlowNode*>& leaf_nodes);

// Returns if all leaf nodes in data flow graph are on single source paths.
bool are_all_leaf_nodes_on_single_source_path(const std::vector<DataFlowNode*>& leaf_nodes);

// Propagates constant number of iterations to all nodes in data flow graph, starting from the root nodes.
void set_num_iterations_for_graph(const std::vector<DataFlowNode*>& root_nodes, const int num_iterations);

// Calculates number of iterations on a multi-source (scatter) path from root nodes to the leaf nodes.
void calculate_num_iterations_for_multi_source_path(const std::vector<DataFlowNode*>& leaf_nodes,
                                                    const std::vector<DataFlowNode*>& root_nodes);

// Checks if number of iterations satisfies these divisibility constraints:
//   1) Number of tiles per epoch is divisible by number of iterations.
//   2) Number of tiles per iteration is divisible by unpacker tile clear granularity.
bool number_of_iterations_satisfies_divisibility_constraints(unsigned int num_iterations,
                                                             unsigned int num_epoch_tiles,
                                                             unsigned int tile_clear_granularity);

// Returns clearing granularity for a single source path from the root to multiple leaf nodes.
unsigned int get_single_source_path_clearing_granularity(const DataFlowNode* root_node,
                                                         const std::vector<DataFlowNode*>& leaf_nodes);

// Returns clearing granularity for a single source path from the root to the leaf node.
unsigned int get_single_source_path_clearing_granularity(const DataFlowNode* root_node, const DataFlowNode* leaf_node);

// Calculates and sets how many tiles are going to be send through each data flow node in the tree with the
// given node being the leaf node.
void calculate_tiles_to_send(DataFlowNode* df_node, std::unordered_set<DataFlowNode*>& visited_nodes);

// Calculates the number of unique data flow paths going through the given node - i.e. the number of times
// this node needs to be visited when processing its subtree.
void calculate_number_of_paths_through_node(DataFlowNode* df_node,
                                            std::unordered_set<DataFlowNode*>& visited_nodes);

// Calculates common divisor for the subtree starting the given node. Common divisor is the least number of
// tiles that maximum number of tiles per phase must be divisible by.
void calculate_subtree_common_divisor(DataFlowNode* df_node, std::unordered_set<DataFlowNode*>& visited_nodes);

// Calculates how many tiles at most each data flow node may send in one phase. This is done by taking into
// consideration how many tiles a data flow node must send in an iteration, and what is the common divisor of
// the subtree of the given node. This function is called for every root node and it sets the maximum number of
// tiles per phase for all the nodes reachable from the given root node.
void calculate_max_tiles_per_phase(DataFlowNode* df_node,
                                   std::unordered_set<DataFlowNode*>& visited_nodes,
                                   unsigned int max_num_tiles_per_phase);

// Traverses data flow graph starting from the given root nodes in topological order and invokes visit
// function on each node. Guarantees that no node will be visited multiple times.
void visit_nodes_in_topological_order(const std::vector<DataFlowNode*>& root_nodes,
                                      const std::function<void(DataFlowNode*)>& visit_node_func,
                                      std::unordered_set<DataFlowNode*>& visited_nodes);

// Helper function for calculating topological ordering of a graph. Adds univisted neighbours of a given node
// to the topological ordering stack.
void topological_order_helper_dfs(DataFlowNode* node,
                                  std::unordered_set<DataFlowNode*>& visited_nodes,
                                  std::stack<DataFlowNode*>& topological_ordering);

} // namespace data_flow_internal
} // namespace pipegen2
