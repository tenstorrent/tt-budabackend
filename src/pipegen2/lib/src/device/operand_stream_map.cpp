// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/operand_stream_map.h"

#include "noc/noc_overlay_parameters.h" // Necessary for stream_io_map.h
#include "stream_io_map.h"

#include "utils/logger.hpp"

namespace pipegen2
{

bool OperandStreamMap::is_input_operand(int operand_id)
{
    return (operand_id >= OPERAND_INPUT_START_INDEX && operand_id < OPERAND_OUTPUT_START_INDEX) ||
           is_intermediate_operand(operand_id);
}

bool OperandStreamMap::is_output_operand(int operand_id)
{
    return (operand_id >= OPERAND_OUTPUT_START_INDEX && operand_id < OPERAND_INTERMEDIATES_START_INDEX) ||
           is_intermediate_operand(operand_id);
}

bool OperandStreamMap::is_intermediate_operand(int operand_id)
{
    return operand_id >= OPERAND_INTERMEDIATES_START_INDEX && operand_id < OPERAND_RELAY_START_INDEX;
}

bool OperandStreamMap::is_relay_operand(int operand_id)
{
    return operand_id >= OPERAND_RELAY_START_INDEX && operand_id < MAX_NUM_OPERANDS;
}

StreamId OperandStreamMap::get_operand_stream_id(int operand_id)
{
    return static_cast<StreamId>(::get_operand_stream_id(operand_id));
}

unsigned int OperandStreamMap::get_output_index(int operand_id)
{
    log_assert(is_output_operand(operand_id),
               "Operand ID must be a valid output operand id in order to map it to an output index");

    return static_cast<unsigned int>(::operand_to_output_index(operand_id));
}

} // namespace pipegen2