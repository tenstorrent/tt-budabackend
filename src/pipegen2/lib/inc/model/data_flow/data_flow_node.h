// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>

#include "model/data_flow/data_flow_info.h"
#include "model/data_flow/data_flow_node_input.h"
#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"

namespace pipegen2
{
    class DramInputNode;
    class DramOutputNode;
    class PCIeStreamingNode;
    class UnpackerOutputNode;

    // DataFlowNode represents a group of one or two nodes in a rational graph.
    // TODO comment on reasoning behind this
    class DataFlowNode
    {
    public:
        DataFlowNode(const RGBaseNode* rg_base_node);

        // Virtual destructor, necessary for base class.
        virtual ~DataFlowNode() = default;

        const RGBaseNode* get_rg_node() const { return m_rg_node; }

        const RGBasePipe* get_rg_pipe() const { return m_rg_pipe; }

        // Returns the ID of the underlying RG node.
        virtual NodeId get_id() const;

        // Returns data flow type of the underlying RG pipe.
        virtual DataFlowType get_dataflow_type() const;

        // Returns fanout of the pipe associated with this data flow node.
        virtual unsigned int get_serialization_factor() const;

        // Returns from how many different input source groups does this node receive data from.
        virtual unsigned int get_input_group_count() const
        {
            return is_union() ? m_sources.size() : 1;
        }

        // Returns the total number of inputs per one input group.
        unsigned int get_num_inputs_per_input_group() const
        {
            return get_total_num_inputs() / get_input_group_count();
        }

        // Returns periodic repeat of the pipe associated with this data flow node.
        virtual unsigned int get_repeat_factor() const;

        // Returns if the node has processed all of its serialized inputs.
        bool has_processed_all_inputs() const;

        // Change source of the inputs to process next by increasing input group serialization index.
        void move_to_next_input_group() { ++m_current_input_group_index; }

        int get_inputs_phase_offset() const { return m_inputs_phase_offset; }

        void set_inputs_phase_offset(int val) { m_inputs_phase_offset = val; }

        const std::vector<DataFlowNodeInput>& get_all_inputs() const { return m_inputs; }

        // Returns the total number of inputs in all of the serial inputs iterations.
        std::size_t get_total_num_inputs() const { return get_all_inputs().size(); }

        // Returns a list of the current inputs to process based on input serialization index.
        DataFlowNodeInputRange get_current_inputs_to_process() const;

        // Returns the first node in the input lists, asserts if the node is a root node.
        DataFlowNode* get_first_input_node() const;

        void add_dest(DataFlowNode* dest_df_node) { m_destinations.push_back(dest_df_node); }

        void add_source(DataFlowNode* source_df_node) { m_sources.push_back(source_df_node); }

        const std::vector<DataFlowNode*>& get_destinations() const { return m_destinations; }

        const std::vector<DataFlowNode*>& get_sources() const { return m_sources; }

        bool is_root_node() const { return m_sources.empty(); }

        bool is_leaf_node() const { return m_destinations.empty(); }

        void set_is_on_single_source_path(bool val) { m_is_on_single_source_path = val; }

        bool is_on_single_source_path() const { return m_is_on_single_source_path; }

        bool is_union() const;

        virtual bool is_scatter() const;

        virtual bool is_dram_or_pcie_input() const;

        // Returns true if the underlying RG pipe of this data flow node is of type DramParallelFork.
        virtual bool is_dram_parallel_fork() const;

        virtual bool is_dram_or_pcie_output() const;

        virtual bool is_padding() const;

        // Returns if the node is an isolated node, i.e. it is not connected to any other node in the graph. These may
        // be byproduct of encapsulation of a node into a virtual node.
        bool is_isolated_node() const;

        // Returns true if underlying RG node corresponds to untilized dram output node.
        virtual bool is_untilized_dram_output() const;

        // Returns whether underlying buffer corresponds to an intermediate buffer.
        virtual bool is_intermediate() const;

        // Returns whether we can transfer whole epoch of data in a single iteration. This is true if underlying RG node
        // is either: untilized dram output node or intermediate node.
        bool can_do_epoch_in_single_iteration() const;

        virtual unsigned int get_num_epoch_tiles() const { return m_rg_node->get_num_epoch_tiles(); }

        virtual unsigned int get_consume_granularity() const;

        virtual unsigned int get_scatter_gather_num_tiles() const;

        virtual std::optional<unsigned int> get_root_to_leaf_path_index() const;

        virtual unsigned int get_num_root_to_leaf_paths_in_subgraph() const;

        // Returns the size in tiles of the underlying RG node.
        virtual unsigned int get_size_tiles() const;

        // Returns number of tiles per input of the underlying RG node.
        virtual unsigned int get_num_tiles_per_input() const;

        unsigned int get_tiles_to_send() const { return m_tiles_to_send; }

        unsigned int get_max_num_tiles_per_phase() const { return m_max_num_tiles_per_phase; }

        unsigned int get_num_iterations() const { return m_num_iterations; }

        unsigned int get_subtree_common_divisor() const { return m_subtree_common_divisor; }

        void set_tiles_to_send(unsigned int val) { m_tiles_to_send = val; }

        void set_max_num_tiles_per_phase(unsigned int val) { m_max_num_tiles_per_phase = val; }

        void set_num_iterations(unsigned int val) { m_num_iterations = val; }

        void set_subtree_common_divisor(unsigned int val) { m_subtree_common_divisor = val; }

        void set_leaf_subgraph_id(int val) { m_leaf_subgraph_id = val; }

        int get_leaf_subgraph_id() const { return m_leaf_subgraph_id; }

        bool has_assigned_leaf_subgraph_id() const { return m_leaf_subgraph_id != -1; }

        // Returns whether it is possible to expand last phase toward given destination. In case it is possible, it will
        // update the sending phases accordingly.
        bool try_update_last_sending_phase(DataFlowNode* destination);

        // Returns number of assigned sending phases towards given destination.
        unsigned int get_num_sending_phases(DataFlowNode* destination);

        // Adds a new phase on the sending side of the node towards given destination.
        void add_sending_phase(DataFlowNode* destination,
                               unsigned int phase_offset,
                               unsigned int data_offset,
                               unsigned int num_msgs);

        // Returns whether it is possible to expand the last receiving phase in the current input group. In case there
        // is no receiving phase in the current input group, returns true because we can always add a new phase.
        bool can_expand_last_receiving_phase(unsigned int receiving_chunk_size) const;

        // Adds a new phase on the receiving side of the node.
        void add_receiving_phase(unsigned int phase_offset, unsigned int num_msgs);

        // Updates the last receiving phase if possible, otherwise it adds a new phase.
        void update_receiving_phases(unsigned int phase_offset, unsigned int receiving_chunk_size);

        // Returns a list of receiving phases for all input groups.
        std::vector<PhaseInfo> get_receiving_phases_for_all_input_groups() const;

        const std::map<unsigned int, std::vector<PhaseInfo>>& get_receiving_phases_per_input_group() const
        {
            return m_receiving_phases_per_input_group;
        }

        void set_receiving_phases_per_input_group(
            std::map<unsigned int, std::vector<PhaseInfo>>&& receiving_phases_per_input_group)
        {
            m_receiving_phases_per_input_group = std::move(receiving_phases_per_input_group);
        }

        void set_sending_phases_to_dest(DataFlowNode* dest, std::vector<PhaseInfo>&& sending_phases)
        {
            m_sending_phases[dest] = std::move(sending_phases);
        }

        const std::vector<PhaseInfo>& get_current_input_group_receiving_phases() const
        {
            return m_receiving_phases_per_input_group.at(get_current_input_group_index());
        }

        const std::vector<PhaseInfo>& get_sending_phases(DataFlowNode* destination) const;

        void set_number_of_unique_df_paths(unsigned int number_of_unique_df_paths)
        {
            m_number_of_unique_df_paths = number_of_unique_df_paths;
        }

        unsigned int get_number_of_unique_df_paths() const { return m_number_of_unique_df_paths; }

        unsigned int get_current_input_group_index() const
        {
            return m_current_input_group_index % get_input_group_count();
        }

        void add_input(DataFlowNode* source, unsigned int offset)
        {
            m_inputs.emplace_back(source, offset);
        }

        // Returns if this DF node should accumulate its sending phases. Sending phases from dram input nodes are not
        // accumulated, but they get merged afterwards outside of data flow calculator.
        // TODO: Remove this after dram read data flow refactor.
        bool should_accumulate_sending_phases() const;

    protected:
        DataFlowNode();

    private:
        // Returns a list of receiving phases for the current input group.
        std::vector<PhaseInfo>& get_receiving_phases_for_current_input_group();

        // Returns whether it is possible to expand the last receiving phase in the given receiving phases vector.
        bool can_expand_last_receiving_phase(const std::vector<PhaseInfo>& receiving_phases,
                                             unsigned int receiving_chunk_size) const;

        // Expands the last receiving phase for the current input group by given receiving chunk size.
        void expand_last_receiving_phase(std::vector<PhaseInfo>& receiving_phases, unsigned int receiving_chunk_size);

        // Tries to cast underlying buffer to an unpacker output node. Returns nullptr if it fails.
        const UnpackerOutputNode* try_get_unpacker_output_node() const;

        // Tries to cast underlying buffer to a dram input node. Returns nullptr if it fails.
        const DramInputNode* try_get_dram_input_node() const;

        // Tries to cast underlying buffer to a dram output node. Returns nullptr if it fails.
        const DramOutputNode* try_get_dram_output_node() const;

        // Tries to cast underlying buffer to a PCIe streaming node. Returns nullptr if it fails.
        const PCIeStreamingNode* try_get_pcie_streaming_node() const;

        // How many phases are needed for all inputs to transfer data to this data flow node.
        int m_inputs_phase_offset;

        // Which pipe source to process in the current iteration when calculating phases.
        unsigned int m_current_input_group_index;

        // List of destinations of this data flow node.
        std::vector<DataFlowNode*> m_destinations;

        // List of sources of this data flow node.
        std::vector<DataFlowNode*> m_sources;

        // List of phase information on the receiving side of this data flow node, grouped by the input group index and
        // sorted by receiving group index in ascending order.
        std::map<unsigned int, std::vector<PhaseInfo>> m_receiving_phases_per_input_group;

        // List of phase information for each destination on the sending side of this data flow node.
        std::unordered_map<DataFlowNode*, std::vector<PhaseInfo>> m_sending_phases;

        // How many tiles to send in one iteration from this data flow node.
        unsigned int m_tiles_to_send;

        // How many tiles at most this data flow node can send in a single phase.
        // TODO: calculate max_num_tiles_per_phase for each destination independently. Potentially, we can have
        // different max_num_tiles_per_phase for source and dest and we could accumulate on source side more than we are
        // able to receive on dest side. Idea is propagate max_num_tiles_per_phase from dest back to source when dest
        // has multiple inputs because that causes differences in max_num_tiles_per_phase. This is a hypothetical
        // scenario, but it is possible.
        unsigned int m_max_num_tiles_per_phase;

        // How many iterations of data transfers are needed to transfer whole epoch of data from this data flow node.
        unsigned int m_num_iterations;

        // Common divisor of all data flow nodes consume granularities in the subtree of this node.
        unsigned int m_subtree_common_divisor;

        // Whether this node is on single source path or not. Note that node can be on multiple single source paths if
        // it is forked in such way.
        bool m_is_on_single_source_path;

        // Rational Graph node this node corresponds to.
        const RGBaseNode* m_rg_node;

        // Rational Graph pipe this node corresponds to. If the node is a root, the pipe is nullptr.
        const RGBasePipe* m_rg_pipe;

        // Number of unique data flow paths going through this node - represents the number of times the node needs
        // to be visited in order for its subtree to be processed completely.
        unsigned int m_number_of_unique_df_paths;

        // Id of a subgraph to which this leaf belongs this. This field is only relevant for the leaf data flow nodes.
        int m_leaf_subgraph_id;

        // List of all the inputs to this data flow node.
        std::vector<DataFlowNodeInput> m_inputs;
    };
}