// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "utils/logger.hpp"
#include "epoch_q.h"
#include "netlist/netlist_info_types.hpp"
#include "netlist_program.hpp"
#include "runtime/runtime_types.hpp"

using namespace std;

namespace tt {

std::tuple<uint16_t, uint32_t, uint32_t> get_varinst_cmd_opcode_and_operands(const tt_instruction_info &instrn, const unordered_map<string, int> &vars);

struct tt_varinst_queue_update_cmd_info {

    using Field = tt_queue_header_field;

    std::string var_name;
    uint16_t opcode; // VarinstCmdOpcode
    uint32_t operand_0;
    uint32_t operand_1;
    tt_queue_varinst_update_field_mask update_field_mask;
    std::unordered_set<std::string> queue_names;
    tt_queue_settings_sync_type sync_type;
    bool valid_cmd = true; // optimizations may invalidate.

    inline bool are_local_global_rdptr_siblings(const tt_varinst_queue_update_cmd_info &cmd){

        bool matching = (valid_cmd == 1) && // Avoid cmds already merged.
                        (cmd.valid_cmd == 1) &&
                        (update_field_mask.has_field(Field::LocalRdptr) == cmd.update_field_mask.has_field(Field::GlobalRdptr)) &&
                        (update_field_mask.has_field(Field::GlobalRdptr) == cmd.update_field_mask.has_field(Field::LocalRdptr)) &&
                        (queue_names == cmd.queue_names) &&
                        (opcode == cmd.opcode) &&
                        (operand_0 == cmd.operand_0) &&
                        (operand_1 == cmd.operand_1);

        return matching;
    }

    inline void merge_local_globl_rdptr_siblings(tt_varinst_queue_update_cmd_info &sibling_cmd){
        update_field_mask.set_field(Field::LocalRdptr);   // For sending current command as merged
        update_field_mask.set_field(Field::GlobalRdptr);
        sibling_cmd.valid_cmd = false;  // To mark sibling as merged, so it's not tried to be merged later.
    }


    inline bool are_varinst_cmds_commutative(const tt_varinst_queue_update_cmd_info &cmd){

        bool commutative = (queue_names == cmd.queue_names);

        switch (opcode){
            case epoch_queue::VarinstCmdIncWrap:
                commutative &= (opcode == cmd.opcode); // Both IncWrap
                commutative &= (operand_1 == cmd.operand_1); // Same Wrapval
                break;
            case epoch_queue::VarinstCmdAdd:
            case epoch_queue::VarinstCmdMul:
            case epoch_queue::VarinstCmdSet:
                commutative &= true; // Just overwrites previous command.
                break;
            case epoch_queue::VarinstCmdInc:
                commutative &= (opcode == cmd.opcode); // Both Inc
                break;
            default:
                log_assert(false, "Invalid opcode {} for varinst on device", cmd.opcode);
                break;
        }

        log_trace(tt::LogLoader, "commutative: {} for this: {} cmd: {}", commutative, *this, cmd);
        return commutative;
    }

    inline tt_varinst_queue_update_cmd_info merge_commutative_varinst_cmds(const tt_varinst_queue_update_cmd_info &prev_cmd){

        tt_varinst_queue_update_cmd_info merged_cmd = *this;

        switch (opcode){
            case epoch_queue::VarinstCmdIncWrap:
                merged_cmd.operand_0 += prev_cmd.operand_0;
                break;
            case epoch_queue::VarinstCmdAdd:
            case epoch_queue::VarinstCmdMul:
            case epoch_queue::VarinstCmdSet:
                break;
            case epoch_queue::VarinstCmdInc:
                merged_cmd.operand_0 += prev_cmd.operand_0;
                break;
            default:
                log_assert(false, "Invalid opcode {} for varinst on device", opcode);
                break;
        }

        log_trace(tt::LogLoader, "prev_cmd: {} + curr_cmd: {} ==> Merged: {}", prev_cmd, *this, merged_cmd);
        return merged_cmd;

    }

};


inline void remove_invalid_commands(std::vector<tt_varinst_queue_update_cmd_info> &cmd_info_list){
    cmd_info_list.erase(std::remove_if(cmd_info_list.begin(), cmd_info_list.end(), [](const tt_varinst_queue_update_cmd_info& info) {
        return !info.valid_cmd;
    }), cmd_info_list.end());
}

// Given opcode and operands from varinst-on-device command, update a variable, num_iteration times in case of operations
// that do not take pure constants as operands.
inline void update_var_for_varinst_cmd_opcode(int &num_iterations, uint16_t &dest, uint16_t opcode, uint32_t operand_0, uint32_t operand_1){

    switch (opcode){
        case epoch_queue::VarinstCmdIncWrap:
            for (int iter = 0; iter < num_iterations; iter++) {
                dest = (dest + operand_0) % operand_1;
            }
            break;
        case epoch_queue::VarinstCmdAdd:
            dest = operand_0 + operand_1;
            break;
        case epoch_queue::VarinstCmdMul:
            dest = operand_0 * operand_1;
            break;
        case epoch_queue::VarinstCmdInc:
            for (int iter = 0; iter < num_iterations; iter++) {
                dest = dest + operand_0;
            }
            break;
        case epoch_queue::VarinstCmdSet:
            dest = operand_0;
            break;
        default:
            log_assert(false, "Invalid opcode {} for varinst on device", opcode);
            break;
    }
}

inline int varinst_instr_to_cmd_opcode(VAR_INSTRUCTION_OPCODE opcode_instr){

    int opcode = epoch_queue::VarinstCmdInvalid;

    switch (opcode_instr){
        case VAR_INSTRUCTION_OPCODE::Invalid:
            opcode = epoch_queue::VarinstCmdInvalid;
            break;
        case VAR_INSTRUCTION_OPCODE::IncWrap:
            opcode = epoch_queue::VarinstCmdIncWrap;
            break;
        case VAR_INSTRUCTION_OPCODE::Inc:
            opcode = epoch_queue::VarinstCmdInc;
            break;
        case VAR_INSTRUCTION_OPCODE::Mul:
            opcode = epoch_queue::VarinstCmdMul;
            break;
        case VAR_INSTRUCTION_OPCODE::Add:
            opcode = epoch_queue::VarinstCmdAdd;
            break;
        case VAR_INSTRUCTION_OPCODE::Set:
            opcode = epoch_queue::VarinstCmdSet;
            break;
        default:
            log_error("varinst instr opcode {} not supported.", opcode_instr);
            break;
    }

    return opcode;
}

inline epoch_queue::VarinstCmdInfo get_varinst_cmd_for_io_queue_update(
    const tt_varinst_queue_update_cmd_info &info,
    uint64_t addr,
    tt_xy_pair buffer_dram_core,
    uint16_t num_buffers,
    uint8_t reader_index,
    uint8_t num_readers,
    uint16_t update_mask) {
    static const int field_size_bytes = 2; // Only support 16b fields, to match get_16b_header_position_for_field()
    return {
        .dram_addr_lower = static_cast<uint32_t>(addr & 0xffffffff),
        .dram_addr_upper = static_cast<uint16_t>((addr & (uint64_t)0x0000ffff00000000) >> 32),
        .dram_core_x = static_cast<uint16_t>(buffer_dram_core.x & 0x3F),
        .dram_core_y = static_cast<uint16_t>(buffer_dram_core.y & 0x3F),
        .cmd_type = epoch_queue::EpochCmdVarinst,
        .num_buffers = num_buffers,
        .reader_index = reader_index,
        .num_readers = num_readers,
        .update_mask = update_mask,
        .opcode = info.opcode,
        .field_size_bytes = field_size_bytes,
        .operand_0 = info.operand_0,
        .operand_1 = info.operand_1,
        .sync_loc_enable = false,
        .sync_loc_dram_core_x = 0,
        .sync_loc_dram_core_y = 0,
        .sync_loc_index = 0,
    };
}

// Matches the 16b container positions in tt_queue_header - must update if things change.
inline uint8_t get_16b_header_position_for_field(const tt_queue_header_field &field) {
    int position = 0;

    switch (field) {
        case tt_queue_header_field::GlobalRdptr:
            position = 0;
            break;
        case tt_queue_header_field::GlobalWrptr:
            position = 2;
            break;
        case tt_queue_header_field::LocalRdptr:
            position = 4;
            break;
        case tt_queue_header_field::ZeroSetting:
            position = 7;
            break;
        default:
            log_fatal("Unsupported field type for varinst-on-device");
    }
    return position;
}

inline uint16_t get_varinst_cmd_update_mask(const tt_varinst_queue_update_cmd_info &info) {

    using Field = tt_queue_header_field;

    // Currently not expected to see merged rdptr + wrptr updates.
    log_assert(!((info.update_field_mask.has_field(Field::GlobalRdptr) || info.update_field_mask.has_field(Field::LocalRdptr))
    && info.update_field_mask.has_field(Field::GlobalWrptr)),
    "Cannot combine local/global rdptr update and global_wr_ptr update in EpochCmdVarinst currently.");

    uint16_t update_mask =  (info.update_field_mask.has_field(Field::GlobalRdptr) << get_16b_header_position_for_field(Field::GlobalRdptr)) |
                            (info.update_field_mask.has_field(Field::GlobalWrptr) << get_16b_header_position_for_field(Field::GlobalWrptr)) |
                            (info.update_field_mask.has_field(Field::LocalRdptr)  << get_16b_header_position_for_field(Field::LocalRdptr))  |
                            (info.update_field_mask.has_field(Field::ZeroSetting) << get_16b_header_position_for_field(Field::ZeroSetting));

    return update_mask;

}

// To support alias queues for dual-view read/write feature.
inline std::string get_base_queue_name(const tt_queue_info &queue_info) {
    return queue_info.alias != "" ? queue_info.alias : queue_info.name;
}

inline ostream& operator<<(ostream& os, const tt_varinst_queue_update_cmd_info& t) {

  os << "tt_varinst_queue_update_cmd_info{ ";
  os << ".valid_cmd = " << t.valid_cmd << ", ";
  os << ".var_name = " << t.var_name << ", ";
  os << ".opcode = " << t.opcode << ", ";
  os << ".operand_0 = " << t.operand_0 << ", ";
  os << ".operand_1 = " << t.operand_1 << ", ";
  os << ".update_field_mask = " << (int) t.update_field_mask.value << ", ";
  os << ".sync_type = " << (int) t.sync_type << ", ";
  os << ".num_queues = " << t.queue_names.size() << ", ";
  os << ".queue_names = {";
  for (auto &queue_name: t.queue_names){
    os << queue_name << ",";
  }
  os << "}";
  os << "}";
  return os;
}

} // namespace
