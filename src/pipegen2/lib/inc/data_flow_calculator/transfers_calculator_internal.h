// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace pipegen2
{

class DataFlowNode;

namespace data_flow_internal
{
    
// Calculates phases needed to transfer data from a data flow node to its destination. Returns how many phases
// are needed to transfer data to the node. `data_offset` is the offset in data buffer from where the data flow
// node will start sending data. `dest` is the destination data flow node.
int calculate_phases(DataFlowNode* df_node,
                     unsigned int data_offset,
                     DataFlowNode* dest,
                     unsigned int start_phase_offset);

// Helper function for calculate_phases. Calculates phases needed to transfer data from a root data flow node to
// its destination. Returns how many phases are needed to transfer data.
int calculate_phases_for_root(DataFlowNode* df_node,
                              unsigned int data_offset,
                              DataFlowNode* dest,
                              unsigned int start_phase_offset);

// Helper function for calculate_phases. Calculates phases needed to receive data from all inputs of a data flow
// node. Returns how many phases are needed to transfer data to the node.
int calculate_receiving_phases(DataFlowNode* df_node,
                               unsigned int start_phase_offset);

// Helper function to determine if the sender node can accumulate messages to the same phase.
bool can_sender_accumulate(DataFlowNode* df_node,
                           DataFlowNode* prev_sender, 
                           unsigned int prev_data_offset, 
                           DataFlowNode* current_sender, 
                           unsigned int current_data_offset);

// Helper function for calculate_phases. Calculates phases needed to send data to its destination.
void calculate_sending_phases(DataFlowNode* df_node,
                              unsigned int data_offset,
                              DataFlowNode* dest,
                              unsigned int start_phase_offset);

} // namespace data_flow_internal
} // namespace pipegen2
