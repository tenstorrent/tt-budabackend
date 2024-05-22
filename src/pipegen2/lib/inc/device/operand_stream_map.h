// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/typedefs.h"

namespace pipegen2
{

class OperandStreamMap
{
public:
    // Checks if operand with a given ID is an input operand.
    static bool is_input_operand(int operand_id);

    // Checks if operand with a given ID is an output operand.
    static bool is_output_operand(int operand_id);

    // Checks if operand with a given ID is an intermediate operand.
    static bool is_intermediate_operand(int operand_id);

    // Checks if operand with a given ID is a relay operand.
    static bool is_relay_operand(int operand_id);

    // Converts operand ID to a stream ID.
    static StreamId get_operand_stream_id(int operand_id);

    // Returns output index based on operand id.
    static unsigned int get_output_index(int operand_id);
};

}  // namespace pipegen2