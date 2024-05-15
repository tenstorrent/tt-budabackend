// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "epoch_utils.hpp"
#include "epoch_q.h"

namespace tt {

// Get proper operands for this cmd and check they are legal for varinst on device.
std::tuple<uint16_t, uint32_t, uint32_t> get_varinst_cmd_opcode_and_operands(const tt_instruction_info &instrn, const unordered_map<string, int> &vars){

    uint16_t opcode = varinst_instr_to_cmd_opcode(instrn.varinst_opcode);
    std::string operand_0_str, operand_1_str;
    std::string var_name = get<0>(instrn.vars[0]);

    switch (opcode){
        case epoch_queue::VarinstCmdIncWrap:
        case epoch_queue::VarinstCmdAdd:
        case epoch_queue::VarinstCmdMul:
            operand_0_str = std::get<0>(instrn.vars[1]);
            operand_1_str = std::get<0>(instrn.vars[2]);
            break;
        case epoch_queue::VarinstCmdInc:
        case epoch_queue::VarinstCmdSet:
            operand_0_str = std::get<0>(instrn.vars[1]);
            operand_1_str = std::get<0>(instrn.vars[1]); // Unused.
            break;
        default:
            log_assert(false, "Invalid opcode {} for varinst on device", opcode);
            break;
    }

    uint32_t operand_0          = get_variable_from_map(operand_0_str, vars);
    uint32_t operand_1          = get_variable_from_map(operand_1_str, vars);

    // If non-immediate or non-const variables are used as operands, cannot convert them to on-device varinst. Checking only for vars bound to queues.
    log_assert(is_constant(operand_0_str) || is_immediate(operand_0_str) || is_param(operand_0_str),
        "var {} bound to queue, used as varinst dest, but operand0 {} must be const/immed/param for prog looping on device. instrn: {}",
         var_name, operand_0_str, instrn);
    log_assert(is_constant(operand_1_str) || is_immediate(operand_1_str) || is_param(operand_1_str),
        "var {} bound to queue, used as varinst dest, but operand1 {} must be const/immed/param for prog looping on device. instrn: {}",
         var_name, operand_1_str, instrn);

    // If updating "zero" single-bit setting, include neighboring fields in 16-bit container via RMW to avoid overwriting 
    // since EpochCmdVarinst doesn't support single bit-update mode.
    // FIXME - WIP - Add it here...

    return std::make_tuple(opcode, operand_0, operand_1);
}

} // namespace
