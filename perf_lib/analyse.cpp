// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "analyse.hpp"
#include "third_party/json/json.hpp"

namespace postprocess {

    using perf::PerfComparisonConfig;

    bool is_check_enabled_for_instruction(const InstructionInfo &instr, const vector<PerfComparisonConfig>& comparison_config, PerfComparisonConfig &target_config) {
        for (const PerfComparisonConfig &config : comparison_config) {
            if (config.graph_name == instr.graph_name && config.program_name == instr.program_name) {
                target_config = config;
                return true;
            }
        }
        return false;
    }

    bool is_host_event_check(const PerfComparisonConfig &comparison_config) {

        return comparison_config.backend_samples_per_second.en;
    }

    template<typename T>
    bool check_with_expected_perf(float observed_value, T expected_value, float rtol, T override_target_value){
        T target_value = expected_value;
        if (expected_value == 0) {
            target_value = override_target_value;
        }
        return float(abs(observed_value - target_value)) < abs(rtol * target_value);
    }

    template<typename T>
    bool check_with_perf_min_bound(float observed_value, T min_bound, T override_target_value) {
        T target_value = min_bound;
        if (min_bound == 0) {
            target_value = override_target_value;
        }
        return observed_value >= target_value;
    }

    template<typename T>
    bool check_perf_value(float observed_value, const perf::PerfCheckValue<T> &expected_perf) {
        if(expected_perf.using_expected_value) return check_with_expected_perf(observed_value, expected_perf.target_value, expected_perf.rtol, expected_perf.override_target_value);
        else if(expected_perf.using_min_bound) return check_with_perf_min_bound(observed_value, expected_perf.target_value, expected_perf.override_target_value);
        else log_assert(false, "Must use either 'min_bound' or 'expected' target value during performance checking.");
        return false;
    }
    template<typename T>
    string get_perf_comparison_message(float observed, const perf::PerfCheckValue<T> &expected, bool lower_is_better){
        T target_value = expected.target_value;
        if (expected.en && expected.target_value == 0) {
            target_value = expected.override_target_value;
        }
        string message = "";
        if(expected.using_min_bound){
            message = "Min-Bound " + to_string(target_value) + " Observed " + to_string(observed);
        }
        else if(expected.using_expected_value) {
            message = "Expected " + to_string(target_value) + " Observed " + to_string(observed);
        } else {
            log_assert(false, "Must use either 'min_bound' or 'expected' target value during performance checking.");
            return "";
        }
        if (lower_is_better) {
            message += " (lower values are better)";
        } else {
            message += " (higher values are better)";
        }
        return message;
    }
    vector<string> find_core_label_from_op_name(const json &runtime_table, const string &target_op_name) {
        vector<string> all_core_labels;
        for (auto &core_info: runtime_table.items()) {
            string core_label = core_info.key();
            auto op_name_pos = core_label.find(target_op_name);
            // If string exists and is the last characters of the label
            if ((op_name_pos != string::npos) && (op_name_pos + target_op_name.size() == core_label.size())) {
                all_core_labels.push_back(core_label);
            }
        }
        return all_core_labels;
    }

    string find_core_label_from_core_id(const json &runtime_table, const tt_xy_pair& target_core_id) {
        for (auto &core_info: runtime_table.items()) {
            string core_label = core_info.key();
            string target_core_id_label = to_string(target_core_id.x) + "-" + to_string(target_core_id.y);
            if (core_label.find(target_core_id_label) != string::npos) {
                return core_label;
            }
        }
        return "";
    }

    string get_input_label_from_input_idx(const json &runtime_table, const string &core_label, int input_idx) {
        string input_label = "input-" + to_string(input_idx);
        if (runtime_table.at(core_label).contains(input_label)) {
            return input_label;
        } else {
            log_error("Skipping perf check for input {} core {}. Perf results was not recorded for this input.", input_idx, core_label);
            return "";
        }
    }

    template<typename T>
    T get_perf_value_from_json(const json &json_val) {
        T value = 0;
        if (json_val.type() != json::value_t::string) {
            value = json_val.get<T>();           
        }
        return value;
    }

    bool extract_observed_values_and_compare(const json &runtime_table, const PerfComparisonConfig &comparison_config, const string &core_label, const string &input_label) {

        bool all_passed = true;

        float math_utilization = get_perf_value_from_json<float>(runtime_table.at(core_label).at(input_label).at("math-utilization-first-unpack-to-last-pack"));
        uint64_t execution_cycles = get_perf_value_from_json<uint64_t>(runtime_table.at(core_label).at(input_label).at("first-unpack-to-last-pack"));

        float average_math_utilization = get_perf_value_from_json<float>(runtime_table.at(core_label).at("average-math-utilization"));
        uint64_t total_runtime = get_perf_value_from_json<uint64_t>(runtime_table.at(core_label).at("total-runtime"));

        float input0_bw = get_perf_value_from_json<float>(runtime_table.at(core_label).at("trisc-bw-operand-input-0"));
        float output_bw = get_perf_value_from_json<float>(runtime_table.at(core_label).at("trisc-bw-operand-output-0"));
        string log_message;
        if (!comparison_config.math_utilization.en &&
            !comparison_config.execution_cycles.en &&
            !comparison_config.average_math_utlization.en &&
            !comparison_config.total_runtime.en &&
            !comparison_config.input0_bw.en &&
            !comparison_config.output_bw.en) {
                log_error("No performance metric was specified for the current perf-comparison-config");
                all_passed = false;
        }
        if (comparison_config.math_utilization.en) {
            if (check_perf_value(math_utilization, comparison_config.math_utilization)) {
                log_message =  "Math-utilization check was successful for program {}, graph {}, core_label {}, input_label {}. " + get_perf_comparison_message(math_utilization, comparison_config.math_utilization, false);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label, input_label);
            } else {
                log_message = "Math-utilization check failed for program {}, graph {}, core_label {}, input_label {}. " + get_perf_comparison_message(math_utilization, comparison_config.math_utilization, false);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label, input_label);
            }
        }
        
        if (comparison_config.execution_cycles.en) {
            if (check_perf_value(execution_cycles, comparison_config.execution_cycles)) {

                log_message = "Execution-cycles check was successful for program {}, graph {}, core_label {}, input_label {}. " + get_perf_comparison_message(execution_cycles, comparison_config.execution_cycles, true);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label, input_label);
            } else {
                log_message = "Execution-cycles check failed for program {}, graph {}, core_label {}, input_label {}. " + get_perf_comparison_message(execution_cycles, comparison_config.execution_cycles, true);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label, input_label);
            }
        }
        if (comparison_config.average_math_utlization.en) {
            if (check_perf_value(average_math_utilization, comparison_config.average_math_utlization)) {
                log_message = "Average-math-utilization check was successful for program {}, graph {}, core_label {}. " + get_perf_comparison_message(average_math_utilization, comparison_config.average_math_utlization, false);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            } else {
                log_message = "Average-math-utilization check failed for program {}, graph {}, core_label {}. " + get_perf_comparison_message(average_math_utilization, comparison_config.average_math_utlization, false);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            }
        }
        if (comparison_config.total_runtime.en) {
            if (check_perf_value(total_runtime, comparison_config.total_runtime)) {
                log_info(tt::LogPerfCheck, "Total-runtime check was successful for program {}, graph {}, core_label {}. Expected {} Observed {}",
                        comparison_config.program_name, comparison_config.graph_name, core_label, comparison_config.total_runtime.target_value, total_runtime, true);
            } else {
                all_passed = false;
                log_error("Total-runtime check failed for program {}, graph {}, core_label {}. Expected {} Observed {}",
                        comparison_config.program_name, comparison_config.graph_name, core_label, comparison_config.total_runtime.target_value, total_runtime, true);
            }
        }
        if (comparison_config.input0_bw.en) {
            if (check_perf_value(input0_bw, comparison_config.input0_bw)) {
                log_message = "Input0-bw check was successful for program {}, graph {}, core_label {}. " + get_perf_comparison_message(input0_bw, comparison_config.input0_bw, false);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            } else {
                log_message = "Input0-bw check failed for program {}, graph {}, core_label {}. " + get_perf_comparison_message(input0_bw, comparison_config.input0_bw, false);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            }
        }
        if (comparison_config.output_bw.en) {
            if (check_perf_value(output_bw, comparison_config.output_bw)) {
                log_message = "Output-bw check was successful for program {}, graph {}, core_label {}. " + get_perf_comparison_message(output_bw, comparison_config.output_bw, false);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            } else {
                log_message = "Output-bw check failed for program {}, graph {}, core_label {}. " + get_perf_comparison_message(output_bw, comparison_config.output_bw, false);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, core_label);
            }
        }
        return all_passed;
    }

    bool check_instruction_perf(const InstructionInfo &instr, const PerfComparisonConfig &comparison_config) {
        
        log_assert(instr.output_dir_path != "", "Output directory of each instruction must have been poplated by postprocessor");
        bool all_passed = true;
        ifstream ifs_runtime_json(instr.output_dir_path + "/runtime_table.json");
        json runtime_table = json::parse(ifs_runtime_json);
        string log_message;

        log_info(tt::LogPerfCheck, "Starting performance check for program {} with id {}, and graph {} with id {}",
                instr.program_name, instr.program_id, instr.graph_name, instr.instr_id_local);
        for (const tt_xy_pair &target_core: comparison_config.target_cores) {
            string core_label = find_core_label_from_core_id(runtime_table, target_core);
            if (core_label == "") {
                log_error("Skipping perf check for core core-id {} since entry was not found in perf runtime report", target_core.str());
                all_passed = false;
                continue;
            }
            for (int target_input: comparison_config.target_inputs) {
                string input_label = get_input_label_from_input_idx(runtime_table, core_label, target_input);
                if (input_label == "") {
                    all_passed = false;
                    continue;
                }
                all_passed &= extract_observed_values_and_compare(runtime_table, comparison_config, core_label, input_label);
            }
        }
        for (const string &target_op_name: comparison_config.target_ops) {
            vector<string> all_core_labels = find_core_label_from_op_name(runtime_table, target_op_name);
            for (string core_label: all_core_labels) {
                if (core_label == "") {
                    log_error("Skipping perf check for core op-name {} since entry was not found in perf runtime report", target_op_name);
                    all_passed = false;
                    continue;
                }
                for (int target_input: comparison_config.target_inputs) {
                    string input_label = get_input_label_from_input_idx(runtime_table, core_label, target_input);
                    if (input_label == "") {
                        all_passed = false;
                        continue;
                    }
                    all_passed &= extract_observed_values_and_compare(runtime_table, comparison_config, core_label, input_label);
                }
            }
        }
        if (comparison_config.num_cycles_per_input.en) {
            if(check_perf_value(instr.num_cycles_per_input, comparison_config.num_cycles_per_input)) {
                log_message = "Throughput check for number of cycles per input (tensor) was successful for program {}, graph {}. " + get_perf_comparison_message(instr.num_cycles_per_input, comparison_config.num_cycles_per_input, true);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name);
            } else {
                log_message = "Throughput check for number of cycles per input (tensor) failed for program {}, graph {}. " + get_perf_comparison_message(instr.num_cycles_per_input, comparison_config.num_cycles_per_input, true);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name);
            }
        }
        if (comparison_config.num_inputs_per_second.en) {
            if (check_perf_value(instr.num_inputs_per_second, comparison_config.num_inputs_per_second)) {
                log_message = "Throughput check for number of inputs (tensors) per second was successful for program {}, graph {}. " + get_perf_comparison_message(instr.num_inputs_per_second, comparison_config.num_inputs_per_second, false);
                log_info(tt::LogPerfCheck, log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name);
            } else {
                log_message = "Throughput check for number of inputs (tensors) per second failed for program {}, graph {}. " + get_perf_comparison_message(instr.num_inputs_per_second, comparison_config.num_inputs_per_second, false);
                all_passed = false;
                log_error(log_message.c_str(),
                        comparison_config.program_name, comparison_config.graph_name, comparison_config.num_inputs_per_second.target_value, instr.num_inputs_per_second);
            }
        }
        return all_passed;
    }
    
    bool check_performance(const vector<InstructionInfo>& all_instructions, const perf::PerfDesc &perf_desc) {
        
        log_info(tt::LogPerfCheck, "Starting device performance check...");
        bool all_passed = true;
        if (perf_desc.skip_perf_check) {
            log_warning(tt::LogPerfCheck, "Skipping device performance check since --skip-perf-check is set");
            return true;
        }
        for (const InstructionInfo &instr: all_instructions) {
            PerfComparisonConfig target_config;
            if (!is_check_enabled_for_instruction(instr, perf_desc.comparison_configs, target_config)) {
                continue;
            }
            all_passed &= check_instruction_perf(instr, target_config);
        }
        if (all_passed) {
            log_info(tt::LogPerfCheck, "Finished performance check for all instructions successfully");
        } else {
            log_error("Performance check failed for one or more instructions. Check for errors in test log for more detail");
        }
        return all_passed;
    }

    bool check_performance_host_events(const string &output_dir, const PerfState &perf_state) {
        log_info(tt::LogPerfCheck, "Starting performance check for host events");
        const int thread_id = perf::get_thread_id();
        if (perf_state.get_perf_desc().skip_perf_check) {
            log_warning(tt::LogPerfCheck, "Skipping device performance check since --skip-perf-check is set");
            return true;
        }
        bool all_passed = true;
        for (const PerfComparisonConfig &comparison_config: perf_state.get_perf_desc().comparison_configs) {
            if (is_host_event_check(comparison_config)) {
                if (comparison_config.backend_samples_per_second.en) {
                    string perf_report_path = postprocess::get_perf_out_directory(output_dir, perf_state.get_perf_desc().override_perf_output_dir, false) + "/perf_info_all_epochs.yaml";
                    log_assert(fs::exists(perf_report_path), "If host performance check is enabled, the info summary report must have been generated under {}", perf_report_path);
                    YAML::Node perf_report_yaml = YAML::LoadFile(perf_report_path);
                    // float samples_per_sec = host_report.at("global-events").at("samples-per-second");
                    float samples_per_sec = perf_report_yaml["global-events"]["samples-per-second-excluding-last-epoch-of-each-program"].as<float>();
                    if (check_perf_value(samples_per_sec, comparison_config.backend_samples_per_second)) {
                        string log_msg = "Backend samples/s check was successful " + get_perf_comparison_message(samples_per_sec, comparison_config.backend_samples_per_second, false);
                        log_info(tt::LogPerfCheck, log_msg.c_str());
                    } else {
                        string log_msg = "Backend samples/s check failed " + get_perf_comparison_message(samples_per_sec, comparison_config.backend_samples_per_second, false);
                        log_error(log_msg.c_str());
                        all_passed = false;
                    }
                }
            }
        }
        if (all_passed) {
            log_info(tt::LogPerfCheck, "Finished host performance check successfully");
        } else {
            log_error("Host performance check failed with one or more failures. Check the errors in the test log for more information");
        }
        return all_passed;
    }
}
