// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <list>
#include <memory>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model/rational_graph/rational_graph.h"
#include "model/data_flow/data_flow_graph.h"
#include "model/data_flow/data_flow_info.h"
#include "model/data_flow/data_flow_node.h"

namespace pipegen2
{
    class LeafGroup;
    class RGBaseNode;
    class RGBasePipe;
    class SubgraphLeafGroups;

    class DataFlowCalculator
    {
    public:
        // Constructor. Initializes data flow structure corresponding to the given rational graph.
        DataFlowCalculator(const RationalGraph* rg, unsigned int max_num_tiles_per_phase);

        // Returns data flow information for the given rational graph.
        DataFlowInfo get_data_flow_info();

    private:
        // TODO(radenko): general TODO item for this class:
        // Make a DF graph class with methods such as
        //  - build graph
        //  - calulate transfers size
        //  - calculate transfer limit
        //  - calculate transfers
        // These methods can have many implementations depending on the type of chip and underlying data flow HW.
        // Idea is to completely abstract the problem DFCalculator is solving.

        // Maximum number of phases we can have in one iteration on direct path.
        static constexpr uint32_t c_max_num_phases_per_iteration = 15;

        // Returns data flow information for the given data flow graph.
        DataFlowInfo get_data_flow_info(const DataFlowGraph* df_graph);

        // Finds all direct paths starting from the given root nodes.
        void find_direct_paths(const std::vector<DataFlowNode*> root_nodes);

        // Calculates num iterations field for all data flow nodes which can be reached from any root node.
        void calculate_num_iterations(const std::vector<DataFlowNode*>& leaf_nodes,
                                      const std::vector<DataFlowNode*>& root_nodes);

        // Calculates tiles to send for every data flow node whose sink is present in the list of leaf nodes.
        void calculate_tiles_to_send(const std::vector<DataFlowNode*>& leaf_nodes);

        // Calculates number of unique data flow paths going through every data flow node whose sink is present in the
        // list of leaf nodes.
        void calculate_number_of_paths_through_node(const std::vector<DataFlowNode*>& leaf_nodes);

        // Calculates common df node subtree divisor, starting from the given root nodes and then visiting all the nodes
        // which can be reached.
        void calculate_subtree_common_divisor(const std::vector<DataFlowNode*>& root_nodes);

        // Calculates max tiles per phase for every data flow node which can be reached from the given list of root
        // nodes.
        void calculate_max_tiles_per_phase(const std::vector<DataFlowNode*>& root_nodes);

        // Finds leaf groups in the data flow graph and returns SubgraphLeafGroups object that contains leaf groups.
        std::unique_ptr<SubgraphLeafGroups> find_leaf_groups(const std::vector<DataFlowNode*>& root_nodes);

        // Calculates phases for all data flow nodes. Phases are calculated by visiting leaf nodes, in the order
        // provided by the subgraph leaf groups, and going backwards toward the root nodes.
        void calculate_phases(const SubgraphLeafGroups* subgraph_leaf_groups);

        // Copies phases calculated for the first node in the leaf group to other node in the leaf group. Nodes in the
        // same leaf group will always have the exact same phases, so there is no need to compute them separately.
        void copy_calculated_phases_per_leaf_group(const SubgraphLeafGroups* subgraph_leaf_groups);

        // Copies phases calculated for the first node in the leaf group to other node in the leaf group. Every node in
        // the leaf group gets a copy of the first leaf node's receiving phases for all input groups, and all source
        // nodes of the given leaf group are updated to have sending phases towards all leaf nodes in the group.
        void copy_calculated_phases_per_leaf_group(const LeafGroup& leaf_group);

        // Creates a data flow info object by extracting required information from all nodes in the data flow graph.
        DataFlowInfo create_data_flow_info(const DataFlowGraph* data_flow_graph);

        // Starts from a source node and sets direct path marker on such paths.
        bool find_direct_paths(DataFlowNode* source_node, bool is_upstream_direct);

        // Returns whether the given node is the end of a direct edge.
        bool is_end_of_direct_edge(DataFlowNode* node);

        // Calculate how many iterations will happen during the epoch in the data flow graph given with its destination.
        uint32_t calculate_num_iterations(DataFlowNode* leaf_node);

        // Checks if number of iterations satisfies these divisibility constraints:
        //   1) Number of tiles per epoch is divisible by number of iterations.
        //   2) Number of tiles per iteration is divisible by unpacker tile clear granularity.
        bool number_of_iterations_satisfies_divisibility_constraints(
            uint32_t num_iterations, uint32_t num_epoch_tiles, uint32_t tile_clear_granularity);

        // Returns clearing granularity for a direct path from the root to the leaf node.
        uint32_t get_direct_path_clearing_granularity(const DataFlowNode* root_node, const DataFlowNode* leaf_node);

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
        void calculate_max_tiles_per_phase(DataFlowNode* df_node, std::unordered_set<DataFlowNode*>& visited_nodes);

        // Calculates phases needed to transfer data from a data flow node to its destination. Returns how many phases
        // are needed to transfer data to the node. `data_offset` is the offset in data buffer from where the data flow
        // node will start sending data. `dest` is the destination data flow node.
        int calculate_phases(DataFlowNode* df_node,
                             uint32_t data_offset,
                             DataFlowNode* dest,
                             uint32_t start_phase_offset);

        // Helper function for calculate_phases. Calculates phases needed to transfer data from a root data flow node to
        // its destination. Returns how many phases are needed to transfer data.
        int calculate_phases_for_root(DataFlowNode* df_node,
                                      uint32_t data_offset,
                                      DataFlowNode* dest,
                                      uint32_t start_phase_offset);

        // Helper function for calculate_phases. Calculates phases needed to receive data from all inputs of a data flow
        // node. Returns how many phases are needed to transfer data to the node.
        int calculate_receiving_phases(DataFlowNode* df_node,
                                       uint32_t data_offset,
                                       uint32_t start_phase_offset);

        // Helper function for calculate_phases. Calculates phases needed to send data to its destination.
        void calculate_sending_phases(DataFlowNode* df_node,
                                      uint32_t data_offset,
                                      DataFlowNode* dest,
                                      uint32_t start_phase_offset);

        // Returns DataFlowNode corresponding to the given RationalGraphNode.
        DataFlowNode* get_df_node(const RGBaseNode* rg_base_node) { return m_rg_node_2_df_node.at(rg_base_node).get(); }

        // Returns list of DataFlowNodes corresponding to the given RationalGraphPipe.
        const std::vector<DataFlowNode*>& get_df_nodes(const RGBasePipe* rg_base_pipe)
        {
            return m_rg_pipe_2_df_nodes.at(rg_base_pipe);
        }

        // Traverses data flow graph starting from the given root nodes in topological order and invokes visit
        // function on each node. Guarantees that no node will be visited multiple times.
        void visit_nodes_in_topological_order(std::vector<DataFlowNode*> root_nodes,
                                              const std::function<void(DataFlowNode*)>& visit_node_func,
                                              std::unordered_set<DataFlowNode*>& visited_nodes);

        // Helper function for calculating topological ordering of a graph. Adds univisted neighbours of a given node
        // to the topological ordering stack.
        void topological_order_helper_dfs(DataFlowNode* node,
                                          std::unordered_set<DataFlowNode*>& visited_nodes,
                                          std::stack<DataFlowNode*>& topological_ordering);

        // Traverses data flow nodes to find connected subgraphs.
        void split_into_connected_components();

        // Traverses data flow graph starting from the giver root node in depth-first-search fashion and stores
        // its connected subgraph into a DataFlowGraph.
        std::unique_ptr<DataFlowGraph> find_connected_subgraph(DataFlowNode* root_node,
                                                               std::unordered_set<DataFlowNode*>& visited_nodes);

    private:
        const RationalGraph *const m_rational_graph;

        // Maximum number of tiles we can transfer in one phase.
        // TODO: add a UT to test DF with different max num tiles per phase.
        const unsigned int m_max_num_tiles_per_phase;

        // TODO add comment about data flow structure that is created out of RG.
        std::unordered_map<const RGBaseNode*, std::unique_ptr<DataFlowNode>> m_rg_node_2_df_node;
        std::unordered_map<const RGBasePipe*, std::vector<DataFlowNode*>> m_rg_pipe_2_df_nodes;
        std::vector<std::unique_ptr<DataFlowGraph>> m_connected_components;
    };
}