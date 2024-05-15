// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "perf_base.hpp"

namespace perf {

string InstructionInfo::construct_intermed_dump_key() const {
    string intermed_dump_key = "device_perf_dump_program_" + to_string(program_id) + "_" + program_name + "_graph_" +
                                graph_name + "_locid_" + to_string(instr_id_local) + "_globid_" + to_string(instr_id_global) +
                                "_device_" + to_string(device_id);
    
    return intermed_dump_key;
}

void InstructionInfo::set_intermed_dump_key(const string& perf_out_dir) {
    intermed_dump_key = perf_out_dir + construct_intermed_dump_key();
}
const string InstructionInfo::get_output_dir_name() const {
    std::stringstream ss;
    ss << std::setw(4) << std::setfill('0') << to_string(instr_id_local);
    string padded_glob_id = ss.str();
    string instruction_dir =  padded_glob_id + "_" + graph_name;
    return instruction_dir;
}
string InstructionInfo::get_program_dir_name() {
    std::stringstream ss;
    ss << std::setw(4) << std::setfill('0') << to_string(program_id);
    string padded_program_id = ss.str();
    string program_dir = padded_program_id + "_" + program_name;
    return program_dir;
}
void InstructionInfo::set_output_dir_path(const string& perf_out_dir) {
    string instruction_dir = get_output_dir_name();
    string program_dir = get_program_dir_name();
    output_dir_path = perf_out_dir + "/" + program_dir + "/" + instruction_dir + "/";
}
const string InstructionInfo::get_epoch_label() const {
    string output_dir_name = get_output_dir_name();
    return "program_id_" + to_string(program_id) + "_name_" + program_name + "_" + output_dir_name;
}

string PerfTriscDecoupleModetoString(const PerfTriscDecoupleMode &decouple_mode) {
    switch(decouple_mode) {
        case PerfTriscDecoupleMode::None:     return "None";
        case PerfTriscDecoupleMode::UnpMath: return "UnpMath";
        case PerfTriscDecoupleMode::MathPack: return "MathPack";
        default: {
            log_fatal("unrecognized enum type for PerfTriscDecoupleMode");
            return "";
        }
    }
}

PerfTriscDecoupleMode StringtoPerfTriscDecoupleMode(const string &decouple_mode) {
    if (decouple_mode == "UnpMath") {
        return PerfTriscDecoupleMode::UnpMath;
    } else if (decouple_mode == "MathPack") {
        return PerfTriscDecoupleMode::MathPack;
    } else if (decouple_mode == "None") {
        return PerfTriscDecoupleMode::None;
    } else {
        log_fatal("Invalid trisc decouple mode {}", decouple_mode);
        return PerfTriscDecoupleMode::None;
    }
}

}

std::ostream &operator<<(std::ostream &os, const perf::device_perf_event &perf_event) {
    os << "Event id: " << perf_event.id << "\n";
    os << "Event description: " << perf_event.description << "\n";
    os << "First val: " << perf_event.first_val << "\n";
    os << "Second val: " << perf_event.second_val << "\n";
    for (int i = 0; i < perf_event.extra_val.size(); i++) {
        os << "Extra val " << i << ": " << perf_event.extra_val[i] << "\n";
    }
    return os;
}
