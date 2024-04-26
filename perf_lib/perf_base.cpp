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

void PerfComparisonConfig::print() const {
    log_info(tt::LogPerfCheck, "Performance comparison config:");
    log_info(tt::LogPerfCheck, "    program-name = {}, graph-name = {}", program_name, graph_name);
    log_info(tt::LogPerfCheck, "    math-utilization: en = {}, target_value = {}, rtol = {}, override = {}", math_utilization.en, math_utilization.target_value, math_utilization.rtol, math_utilization.override_target_value);
    log_info(tt::LogPerfCheck, "    execution-cycles: en = {}, target_value = {}, rtol = {}, override = {}", execution_cycles.en, execution_cycles.target_value, execution_cycles.rtol, execution_cycles.override_target_value);
    log_info(tt::LogPerfCheck, "    num-cycles-per-input: en = {}, target_value = {}, rtol = {}, override = {}", num_cycles_per_input.en, num_cycles_per_input.target_value, num_cycles_per_input.rtol, num_cycles_per_input.override_target_value);
    log_info(tt::LogPerfCheck, "    num-inputs-per-second: en = {}, target_value = {}, rtol = {}, override = {}", num_inputs_per_second.en, num_inputs_per_second.target_value, num_inputs_per_second.rtol, num_inputs_per_second.override_target_value);
    log_info(tt::LogPerfCheck, "    average-math-utilization: en = {}, target_value = {}, rtol = {}, override = {}", average_math_utlization.en, average_math_utlization.target_value, average_math_utlization.rtol, average_math_utlization.override_target_value);
    log_info(tt::LogPerfCheck, "    total-runtime: en = {}, target_value = {}, rtol = {}, override = {}", total_runtime.en, total_runtime.target_value, total_runtime.rtol, total_runtime.override_target_value);
    log_info(tt::LogPerfCheck, "    input0-bw: en = {}, target_value = {}, rtol = {}, override = {}", input0_bw.en, input0_bw.target_value, input0_bw.rtol, input0_bw.override_target_value);
    log_info(tt::LogPerfCheck, "    output-bw: en = {}, target_value = {}, rtol = {}, override = {}", output_bw.en, output_bw.target_value, output_bw.rtol, output_bw.override_target_value);
    log_info(tt::LogPerfCheck, "    backend-samples-per-second: en = {}, target_value = {}, rtol = {}, override = {}", backend_samples_per_second.en, backend_samples_per_second.target_value, backend_samples_per_second.rtol, backend_samples_per_second.override_target_value);
    string target_core_str;
    for (const auto& target_core: target_cores) {
        target_core_str += target_core.str() + ", ";
    }
    log_info(tt::LogPerfCheck, "    target-cores: {}", target_core_str);
    string target_ops_str;
    for (const auto& target_op: target_ops) {
        target_ops_str += target_op + ", ";
    }
    log_info(tt::LogPerfCheck, "    target-ops: {}", target_ops_str);
    string target_inputs_str;
    for (const auto& target_input: target_inputs) {
        target_inputs_str += to_string(target_input) + ", ";
    }
    log_info(tt::LogPerfCheck, "    target-inputs: {}", target_inputs_str);
}

bool PerfComparisonConfig::has_non_zero_expected_value() const {
    bool has_non_zero_value = false;
    has_non_zero_value |= (math_utilization.target_value != 0 or math_utilization.override_target_value != 0);
    has_non_zero_value |= (execution_cycles.target_value != 0 or execution_cycles.override_target_value != 0);
    has_non_zero_value |= (num_cycles_per_input.target_value != 0 or num_cycles_per_input.override_target_value != 0);
    has_non_zero_value |= (num_inputs_per_second.target_value != 0 or num_inputs_per_second.override_target_value != 0);
    has_non_zero_value |= (average_math_utlization.target_value != 0 or average_math_utlization.override_target_value != 0);
    has_non_zero_value |= (total_runtime.target_value != 0 or total_runtime.override_target_value != 0);
    has_non_zero_value |= (input0_bw.target_value != 0 or input0_bw.override_target_value != 0);
    has_non_zero_value |= (output_bw.target_value != 0 or output_bw.override_target_value != 0);
    has_non_zero_value |= (backend_samples_per_second.target_value != 0 or backend_samples_per_second.override_target_value != 0);
    return has_non_zero_value;
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
