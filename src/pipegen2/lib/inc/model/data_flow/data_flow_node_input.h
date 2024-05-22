// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

namespace pipegen2
{
class DataFlowNode;

// Data flow abstraction of pipe input.
class DataFlowNodeInput
{
public:
    DataFlowNodeInput(DataFlowNode* df_node, unsigned int offset) : m_df_node(df_node), m_offset(offset) {}

    DataFlowNode* get_node() const { return m_df_node; }

    unsigned int get_offset() const { return m_offset; }

private:
    // Input data flow node (DF node which corresponds to the RG node representing the input buffer).
    DataFlowNode* m_df_node;

    // Offset given in number of tiles within input's data buffer.
    unsigned int m_offset;
};

// Class representing a range of inputs to data flow node.
class DataFlowNodeInputRange
{
public:
    DataFlowNodeInputRange(
        const std::vector<DataFlowNodeInput>& df_inputs, unsigned int start_index, unsigned int range_size);

    // Since the input range holds a reference to objects which are owned by another object, prevent passing an rvalue
    // reference to the constructor to avoid keeping a reference to invalid objects.
    DataFlowNodeInputRange(
        std::vector<DataFlowNodeInput>&& df_inputs_rvalue, unsigned int start_index, unsigned int range_size) = delete;

    // Returns a const iterator pointing to the begining of the input range (inclusive).
    std::vector<DataFlowNodeInput>::const_iterator begin() const;

    // Returns a const iterator pointing to the end of the input range (exclusive).
    std::vector<DataFlowNodeInput>::const_iterator end() const;

    // Returns the first element of the range.
    const DataFlowNodeInput& first() const;

    // Returns the size of the input range.
    const std::size_t size() const { return m_range_size; }

private:
    // Referenece to all the data flow node inputs.
    const std::vector<DataFlowNodeInput>& m_df_inputs;

    // Index of the first element of the range.
    unsigned int m_start_index;

    // Size of the input range.
    unsigned int m_range_size;
};
}  // namespace pipegen2
