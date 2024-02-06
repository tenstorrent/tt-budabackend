// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/data_flow/data_flow_node.h"

#include "model/rational_graph/nodes/base_intermed_node.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/dram_output_node.h"
#include "model/rational_graph/nodes/pcie_streaming_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/pipes/fork/dram_parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"
#include "model/rational_graph/pipes/join/union_pipe.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    DataFlowNode::DataFlowNode(const RGBaseNode* rg_base_node) :
        DataFlowNode()
    {
        log_assert(rg_base_node, "Expecting a valid RGBaseNode to be passed to DataFlowNode constructor");

        m_rg_node = rg_base_node;
        m_rg_pipe = rg_base_node->get_input_pipe();
    }

    DataFlowNode::DataFlowNode() :
            m_rg_node(nullptr),
            m_rg_pipe(nullptr),
            m_is_on_single_source_path(true),
            m_current_input_group_index(0),
            m_inputs_phase_offset(-1),
            m_subtree_common_divisor(1),
            m_leaf_subgraph_id(-1)
    {
    }

    NodeId DataFlowNode::get_id() const
    {
        return m_rg_node->get_id();
    }

    DataFlowType DataFlowNode::get_dataflow_type() const
    {
        return m_rg_pipe ? m_rg_pipe->get_dataflow_type() : DataFlowType::Parallel;
    }

    uint32_t DataFlowNode::get_serialization_factor() const
    {
        const SerialForkPipe* serial_fork_pipe = dynamic_cast<const SerialForkPipe*>(m_rg_pipe);
        return serial_fork_pipe ? serial_fork_pipe->get_num_serial_non_padding_outputs() : 1;
    }

    bool DataFlowNode::is_intermediate() const
    {
        return dynamic_cast<const BaseIntermedNode*>(m_rg_node) != nullptr;
    }

    bool DataFlowNode::can_do_epoch_in_single_iteration() const
    {
        const DramOutputNode* output_node = try_get_dram_output_node();
        return (output_node != nullptr && output_node->is_untilized_output()) || is_intermediate();
    }

    unsigned int DataFlowNode::get_consume_granularity() const
    {
        const UnpackerOutputNode* output_node = try_get_unpacker_output_node();
        return output_node == nullptr ? 1 : output_node->get_transfer_granularity();
    }

    unsigned int DataFlowNode::get_scatter_gather_num_tiles() const
    {
        const BaseInputNode* input_node = dynamic_cast<const BaseInputNode*>(m_rg_node);
        return input_node == nullptr ? 1 : input_node->get_scatter_gather_num_tiles();
    }

    std::optional<unsigned int> DataFlowNode::get_root_to_leaf_path_index() const
    {
        const SerialForkNode* serial_fork_node = dynamic_cast<const SerialForkNode*>(m_rg_node);
        return serial_fork_node ? std::make_optional(serial_fork_node->get_scatter_index()) : std::nullopt;
    }

    unsigned int DataFlowNode::get_num_root_to_leaf_paths_in_subgraph() const
    {
        const SerialForkPipe* serial_fork_pipe = dynamic_cast<const SerialForkPipe*>(m_rg_pipe);
        return serial_fork_pipe ? serial_fork_pipe->get_num_serial_outputs() : 1;
    }

    bool DataFlowNode::has_processed_all_inputs() const
    {
        if (is_root_node())
        {
            return true;
        }

        return m_current_input_group_index >= m_number_of_unique_df_paths;
    }

    DataFlowNodeInputRange DataFlowNode::get_current_inputs_to_process() const
    {
        log_assert(m_current_input_group_index < m_number_of_unique_df_paths,
                   "Current input group index out of range for node {}", get_id());

        const std::vector<DataFlowNodeInput>& df_inputs = get_all_inputs();
        const uint32_t num_inputs_per_group = get_num_inputs_per_input_group();

        return DataFlowNodeInputRange(df_inputs,
                                      get_current_input_group_index() * num_inputs_per_group,
                                      num_inputs_per_group);
    }

    DataFlowNode* DataFlowNode::get_first_input_node() const
    {
        log_assert(!is_root_node(), "Root node {} does not have any inputs", get_id());

        return get_all_inputs().at(0).get_node();
    }

    bool DataFlowNode::is_union() const { return dynamic_cast<const UnionPipe*>(m_rg_pipe) != nullptr; }

    bool DataFlowNode::is_scatter() const
    {
        const BaseInputNode* input_node = dynamic_cast<const BaseInputNode*>(m_rg_node);
        return input_node == nullptr ? false : input_node->is_scatter();
    }

    bool DataFlowNode::is_dram_or_pcie_input() const
    {
        return try_get_dram_input_node() != nullptr || try_get_pcie_streaming_node() != nullptr;
    }

    bool DataFlowNode::is_dram_parallel_fork() const
    {
        return dynamic_cast<const DramParallelForkPipe*>(m_rg_pipe) != nullptr;
    }

    bool DataFlowNode::is_padding() const
    {
        const DramInputNode* dram_input_node = try_get_dram_input_node();
        return dram_input_node && dram_input_node->is_padding();
    }

    bool DataFlowNode::try_update_last_sending_phase(DataFlowNode* destination)
    {
        log_assert(destination,
                   "Expecting valid destination as argument to DataFlowNode::try_update_last_sending_phase");
        log_assert(m_sending_phases.count(destination),
                   "Did not find sending phases from node {} to destination node {}", get_id(), destination->get_id());

        PhaseInfo& last_phase = m_sending_phases[destination].back();
        if (last_phase.num_msgs + m_tiles_to_send <= m_max_num_tiles_per_phase)
        {
            m_sending_phases[destination].back().num_msgs += m_tiles_to_send;
            return true;
        }
        return false;
    }

    void DataFlowNode::add_sending_phase(DataFlowNode* destination,
                                         const uint32_t phase_offset,
                                         const uint32_t data_offset,
                                         const uint32_t num_msgs)
    {
        m_sending_phases[destination].emplace_back(phase_offset, data_offset, num_msgs);
    }

    unsigned int DataFlowNode::get_num_sending_phases(DataFlowNode* destination)
    {
        const auto it = m_sending_phases.find(destination);
        if (it == m_sending_phases.end())
        {
            return 0;
        }
        return it->second.size();
    }

    bool DataFlowNode::can_expand_last_receiving_phase(const std::vector<PhaseInfo>& receiving_phases,
                                                       unsigned int receiving_chunk_size) const
    {
        return receiving_phases.back().num_msgs + receiving_chunk_size <= m_max_num_tiles_per_phase;
    }

    bool DataFlowNode::can_expand_last_receiving_phase(const unsigned int receiving_chunk_size) const
    {
        auto it = m_receiving_phases_per_input_group.find(m_current_input_group_index);
        if (it == m_receiving_phases_per_input_group.end())
        {
            return true;
        }

        return can_expand_last_receiving_phase(it->second, receiving_chunk_size);
    }

    void DataFlowNode::expand_last_receiving_phase(std::vector<PhaseInfo>& receiving_phases,
                                                   const unsigned int receiving_chunk_size)
    {
        receiving_phases.back().num_msgs += receiving_chunk_size;
    }

    void DataFlowNode::add_receiving_phase(unsigned int phase_offset, unsigned int num_msgs)
    {
        get_receiving_phases_for_current_input_group().emplace_back(phase_offset, num_msgs);
    }

    void DataFlowNode::update_receiving_phases(const unsigned int phase_offset,
                                               const unsigned int receiving_chunk_size)
    {
        std::vector<PhaseInfo>& current_input_group_receiving_phases = get_receiving_phases_for_current_input_group();

        log_assert(!current_input_group_receiving_phases.empty(),
                   "Receiving phases of node {} should not be empty when updating", get_id());

        if (can_expand_last_receiving_phase(current_input_group_receiving_phases, receiving_chunk_size))
        {
            expand_last_receiving_phase(current_input_group_receiving_phases, receiving_chunk_size);
        }
        else
        {
            add_receiving_phase(phase_offset, receiving_chunk_size);
        }
    }

    std::vector<PhaseInfo>& DataFlowNode::get_receiving_phases_for_current_input_group()
    {
        return m_receiving_phases_per_input_group[m_current_input_group_index];
    }

    std::vector<PhaseInfo> DataFlowNode::get_receiving_phases_for_all_input_groups() const
    {
        std::vector<PhaseInfo> all_receiving_phases;
        for (auto& [input_group_idx, input_group_receiving_phases] : m_receiving_phases_per_input_group)
        {
            all_receiving_phases.insert(all_receiving_phases.end(),
                                        input_group_receiving_phases.begin(),
                                        input_group_receiving_phases.end());
        }

        return all_receiving_phases;
    }

    const std::vector<PhaseInfo>& DataFlowNode::get_sending_phases(DataFlowNode* destination) const
    {
        const auto it = m_sending_phases.find(destination);
        log_assert(
            it != m_sending_phases.end(),
            "Sending phases from node {} to destination node {} are not yet calculated",
            get_id(), destination->get_id());

        return it->second;
    }

    const UnpackerOutputNode* DataFlowNode::try_get_unpacker_output_node() const
    {
        return dynamic_cast<const UnpackerOutputNode*>(m_rg_node);
    }

    const DramInputNode* DataFlowNode::try_get_dram_input_node() const
    {
        return dynamic_cast<const DramInputNode*>(m_rg_node);
    }

    const DramOutputNode* DataFlowNode::try_get_dram_output_node() const
    {
        return dynamic_cast<const DramOutputNode*>(m_rg_node);
    }

    const PCIeStreamingNode* DataFlowNode::try_get_pcie_streaming_node() const
    {
        return dynamic_cast<const PCIeStreamingNode*>(m_rg_node);
    }

    bool DataFlowNode::should_accumulate_sending_phases() const
    {
        return !is_dram_or_pcie_input() && !is_dram_parallel_fork();
    }
}