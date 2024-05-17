// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/transfers_calculator_internal.h"

#include "model/data_flow/data_flow_node.h"
#include "utils/logger.hpp"

namespace pipegen2
{
namespace data_flow_internal
{

int calculate_phases(
    DataFlowNode* df_node, const unsigned int data_offset, DataFlowNode* dest, const unsigned int start_phase_offset)
{
    if (df_node->is_root_node())
    {
        return calculate_phases_for_root(df_node, data_offset, dest, start_phase_offset);
    }

    if (!df_node->has_processed_all_inputs())
    {
        // Save the calculated offset so we don't go through its inputs again.
        df_node->set_inputs_phase_offset(calculate_receiving_phases(df_node, start_phase_offset));
    }

    if (dest)
    {
        calculate_sending_phases(df_node, data_offset, dest, start_phase_offset);
    }

    if (!df_node->has_processed_all_inputs())
    {
        df_node->move_to_next_input_group();
    }

    return df_node->get_inputs_phase_offset();
}

int calculate_phases_for_root(
    DataFlowNode* df_node, unsigned int data_offset, DataFlowNode* dest, unsigned int start_phase_offset)
{
    log_assert(dest, "Expecting destination node to be set for root node {}", df_node->get_id());
    int tiles_remaining_to_send = df_node->get_tiles_to_send();
    unsigned int num_msgs_in_cur_phase = tiles_remaining_to_send > df_node->get_max_num_tiles_per_phase()
                                             ? df_node->get_max_num_tiles_per_phase()
                                             : tiles_remaining_to_send;
    unsigned int sending_phase_offset = start_phase_offset;
    while (tiles_remaining_to_send)
    {
        df_node->add_sending_phase(dest, sending_phase_offset, data_offset, num_msgs_in_cur_phase);
        tiles_remaining_to_send -= num_msgs_in_cur_phase;
        ++sending_phase_offset;
        num_msgs_in_cur_phase = tiles_remaining_to_send > df_node->get_max_num_tiles_per_phase()
                                    ? df_node->get_max_num_tiles_per_phase()
                                    : tiles_remaining_to_send;
    }
    return sending_phase_offset - start_phase_offset;
}

int calculate_receiving_phases(DataFlowNode* df_node, unsigned int start_phase_offset)
{
    // Phase offset starts from the given phase offset and it is increased for every input into this node.
    // The amount of increment is determined recursively for every input.
    unsigned int inputs_phase_offset = start_phase_offset;

    DataFlowNode* prev_sender = nullptr;
    unsigned int prev_data_offset = 0;

    const DataFlowNodeInputRange df_inputs_to_process = df_node->get_current_inputs_to_process();
    for (auto it = df_inputs_to_process.begin(); it != df_inputs_to_process.end(); ++it)
    {
        const DataFlowNodeInput& input = *it;
        DataFlowNode* current_sender = input.get_node();
        const unsigned int current_data_offset = input.get_offset();

        // Save the index of the last phase we have assigned to this node from the current sender.
        unsigned int prev_sender_phase_idx = current_sender->get_num_sending_phases(df_node);

        if (!can_sender_accumulate(df_node, prev_sender, prev_data_offset, current_sender, current_data_offset))
        {
            inputs_phase_offset += calculate_phases(current_sender, current_data_offset, df_node, inputs_phase_offset);
        }

        if (prev_sender != current_sender)
        {
            // If source of data changes, new phases have to be assigned to this node.
            const std::vector<PhaseInfo>& sender_phases = current_sender->get_sending_phases(df_node);

            // Assign new phases to this node starting after the last phase we have assigned corresponding to
            // the current sender.
            for (unsigned int i = prev_sender_phase_idx; i < sender_phases.size(); ++i)
            {
                df_node->add_receiving_phase(sender_phases[i].phase_offset, sender_phases[i].num_msgs);
            }
        }
        else
        {
            // Try updating the number of messages received in the last phase from the current sender. If not
            // possible, add a new phase at the offset of the last phase from the current sender.
            df_node->update_receiving_phases(inputs_phase_offset - 1, current_sender->get_tiles_to_send());
        }

        prev_sender = current_sender;
        prev_data_offset = current_data_offset;
    }

    return inputs_phase_offset - start_phase_offset;
}

bool can_sender_accumulate(
    DataFlowNode* dest,
    DataFlowNode* prev_sender,
    unsigned int prev_data_offset,
    DataFlowNode* current_sender,
    unsigned int current_data_offset)
{
    // Whether we can accumulate messages to the same phase on the sender side. It is important to call
    // try_update_last_sending_phase() after we check if we can accumulate messages on the receiver side.
    // TODO: refactor try_update_last_sending_phase() into 2 functions: can_accumulate() and update().
    return (prev_sender == current_sender) && current_sender->should_accumulate_sending_phases() &&
           ((prev_data_offset + prev_sender->get_tiles_to_send()) == current_data_offset) &&
           dest->can_expand_last_receiving_phase(current_sender->get_tiles_to_send()) &&
           current_sender->try_update_last_sending_phase(dest);
}

void calculate_sending_phases(
    DataFlowNode* df_node, unsigned int data_offset, DataFlowNode* dest, unsigned int start_phase_offset)
{
    unsigned int acc_num_msgs = 0;
    unsigned int sending_phase_offset = start_phase_offset;

    const std::vector<PhaseInfo>& receiving_phases = df_node->get_current_input_group_receiving_phases();
    for (const PhaseInfo& phase_info : receiving_phases)
    {
        if (acc_num_msgs + phase_info.num_msgs > df_node->get_max_num_tiles_per_phase())
        {
            df_node->add_sending_phase(dest, sending_phase_offset, data_offset, acc_num_msgs);
            acc_num_msgs = phase_info.num_msgs;
            sending_phase_offset = phase_info.phase_offset;
        }
        else
        {
            acc_num_msgs += phase_info.num_msgs;
        }
    }
    if (acc_num_msgs > 0)
    {
        df_node->add_sending_phase(dest, sending_phase_offset, data_offset, acc_num_msgs);
    }
}

}  // namespace data_flow_internal
}  // namespace pipegen2
