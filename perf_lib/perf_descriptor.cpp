// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "perf_descriptor.hpp"
#include "model/utils.hpp"
#include "yaml-cpp/yaml.h"
#include "dram_address_map.h"
#include "postprocess.hpp"

perf::tt_backend_perf backend_profiler;
std::mutex backend_profiler_mutex;
namespace perf {

bool is_number(const string& input) {
    for (char c: input) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}
// The input cmdline option for perf mode will have the following format:
// <OP_NAME>:<FIRST_DECOUPLE>-<SECOND_DECOUPLE>,...
// Example: eltwise_binary:UnpMath-MathPack
// This function parses the input arg and extracts the op name and decouplings.
std::unordered_map<std::string, std::unordered_set<perf::PerfTriscDecoupleMode>> set_trisc_perf_decouplings(const std::string& perf_op_mode_desc) {
    std::unordered_map<std::string, std::unordered_set<perf::PerfTriscDecoupleMode>> op_to_decouplings;
    if (perf_op_mode_desc.empty()) {
        return op_to_decouplings;
    }
    std::vector<std::string> each_decoupling_op;
    tt::args::split_string_into_vector(each_decoupling_op, perf_op_mode_desc, ",");
    for (const auto& perf_op_mode_desc_single_op : each_decoupling_op) {
        size_t colon_pos = perf_op_mode_desc_single_op.find(':');
        log_assert(colon_pos != std::string::npos, "Could not find ':' in decoupling descriptor");
        std::string op_name = perf_op_mode_desc_single_op.substr(0, colon_pos);
        size_t decouplings_start = colon_pos + 1;
        std::string all_decouplings = perf_op_mode_desc_single_op.substr(
            decouplings_start, perf_op_mode_desc_single_op.length() - decouplings_start);
        std::vector<std::string> each_decoupling_str;
        unordered_set<perf::PerfTriscDecoupleMode> each_decoupling;
        tt::args::split_string_into_vector(each_decoupling_str, all_decouplings, "-");
        for (const auto &decoupling : each_decoupling_str) {
            each_decoupling.insert(perf::StringtoPerfTriscDecoupleMode(decoupling));
        }
        if (op_to_decouplings.find(op_name) == op_to_decouplings.end()) {
            op_to_decouplings.insert({op_name, each_decoupling});
        }
        else {
            op_to_decouplings.at(op_name).merge(each_decoupling);
        }
    }

    std::stringstream ss;
    for (auto &[op_name, decouplings] : op_to_decouplings) {
        // If perf::PerfTriscDecoupleMode::None is set for an op, ignore all other decouplings
        if (decouplings.find(perf::PerfTriscDecoupleMode::None) != decouplings.end()) {
            decouplings.clear();
            decouplings.insert(perf::PerfTriscDecoupleMode::None);
        }
        ss << op_name << ": ";
        for (const auto& decoupling : decouplings) {
            ss << perf::PerfTriscDecoupleModetoString(decoupling) << " ";
        }
    }

    log_info(tt::LogRuntime, "Performance decouplings: {}", ss.str());

    return op_to_decouplings;
}

// The format of the descriptor file must be as follows:
// Each <DECOUPLES> must have the same format as described in the comments above set_overlay_perf_decouplings function.
// <OP_NAME_0>:<DECOUPLINGS>
// <OP_NAME_1>:<DECOUPLINGS>
// ...
string get_overlay_decouple_desc_from_file(const string &overlay_decoupling_desc_path) {
    log_assert(fs::exists(overlay_decoupling_desc_path), "Overlay-decouple descriptor file path does not exist under {}", overlay_decoupling_desc_path);
    std::ifstream desc_file(overlay_decoupling_desc_path);
    string overlay_decouple_config;
    string line;
    while(std::getline(desc_file, line)) {
        auto delim_pos = line.find(':');
        log_assert(delim_pos != std::string::npos,
                "Line in overlay decouple descriptor does not contain ':'. Each line must have the following format <OP_NAME>:<DECOUPLINGS>. Line: {}", line);
        overlay_decouple_config += line + ",";
    }
    log_assert(!overlay_decouple_config.empty(), "No overlay decoupling configs were provided inside the descriptor file under {}", overlay_decoupling_desc_path);
    overlay_decouple_config.erase(overlay_decouple_config.size() - 1);
    return overlay_decouple_config;
}

// The input cmdline option for perf mode will have the following format:
// <OP_NAME>:<FIRST_DECOUPLE>-<SECOND_DECOUPLE>,...
// Example: eltwise_binary:Input-Output
// To decouple all inputs: Input
// To decouple all outputs: Output
// To decouple specifix input indices: Input5
// This function parses the input arg and extracts the op name and decouplings.
std::unordered_map<std::string, PerfOverlayDecoupleMode> set_overlay_perf_decouplings(std::string perf_overlay_op_mode_desc) {
    std::unordered_map<std::string, PerfOverlayDecoupleMode> decouplings;
    if (perf_overlay_op_mode_desc.empty()) {
        return decouplings;
    }

    std::vector<std::string> each_decoupling_op;
    tt::args::split_string_into_vector(each_decoupling_op, perf_overlay_op_mode_desc, ",");
    for (auto single_op_config : each_decoupling_op) {
        size_t colon_pos = single_op_config.find(':');
        log_assert(colon_pos != std::string::npos, "Could not find ':' in decoupling descriptor");
        std::string op_name = single_op_config.substr(0, colon_pos);
        size_t decouplings_start = colon_pos + 1;
        std::string all_decouplings = single_op_config.substr(
            decouplings_start, single_op_config.length() - decouplings_start);
        std::vector<std::string> decouple_config_str;
        perf::PerfOverlayDecoupleMode decouple_config;
        if (decouplings.find(op_name) != decouplings.end()) {
            decouple_config = decouplings.at(op_name);
        }
        tt::args::split_string_into_vector(decouple_config_str, all_decouplings, "-");
        for(const auto& decoupling : decouple_config_str) {
            bool valid_config = false;
            if (decoupling == "Output") {
                decouple_config.output = true;
                valid_config = true;
            }
            const string input_prefix = "Input";
            if (decoupling == input_prefix) {
                decouple_config.all_inputs = true;
                valid_config = true;
            }
            if (decoupling.substr(0, input_prefix.length()) == input_prefix && decoupling.length() > input_prefix.length()) {
                const string input_idx_str = decoupling.substr(input_prefix.length(), decoupling.length() - input_prefix.length());
                log_assert(is_number(input_idx_str), "In overlay decouple config, found a config with prefix Input with invalid input idx: {}", input_idx_str);
                const int input_idx = std::stoi(input_idx_str);
                log_assert(input_idx >= 0 && input_idx < 8, "Input operand idx provided for overlay-decoupling {} does not fall within the acceptable range 0 -> 7", input_idx);
                decouple_config.input_operand_decouple.insert(input_idx);
                valid_config = true;
            }
            log_assert(valid_config, "Invalid overlay decoupling config: {}", decoupling);
        }
        if (decouplings.find(op_name) != decouplings.end()) {
            decouplings.at(op_name) = decouple_config;
        } else {
            decouplings.insert({op_name, decouple_config});
        }
    }
    log_info(tt::LogRuntime, "Performance Overlay decouplings:");
    std::stringstream ss;
    for(auto& decouple_config : decouplings) {
        ss << decouple_config.first << ": ";
        ss << decouple_config.second;
    }
    log_info(tt::LogRuntime, "{}", ss.str());
    return decouplings;
}

// The input cmdline option for inserting bubbles inside the kernels will have the following format:
// <OP_NAME>:<INPUT_DELAY_UNPACK_CYCLES>-<OUTPUT_DELAY_MATH_CYCLES>-<OUTPUT_DELAY_PACK_CYCLES>,...
// Example: eltwise_binary:0-1000
std::map<std::string, std::array<uint32_t, 3>> set_kernel_delays(std::string kernel_delay_desc, const vector<std::string>& reset_kernel_delay) {
    std::map<std::string, std::array<uint32_t, 3>> op_to_delays;
    if (kernel_delay_desc.empty()) {
        return op_to_delays;
    }
    std::vector<std::string> each_op;
    tt::args::split_string_into_vector(each_op, kernel_delay_desc, ",");
    for (const auto& kernel_delay_single_op : each_op) {
        size_t colon_pos = kernel_delay_single_op.find(':');
        log_assert(colon_pos != std::string::npos, "Could not find ':' in decoupling descriptor");
        std::string op_name = kernel_delay_single_op.substr(0, colon_pos);
        size_t delay_value_start = colon_pos + 1;
        std::string all_decouplings = kernel_delay_single_op.substr(
            delay_value_start, kernel_delay_single_op.length() - delay_value_start);
        
        std::array<uint32_t, 3> delay_values;
        std::vector<uint32_t> delay_values_vec;
        tt::args::split_string_into_vector(delay_values_vec, all_decouplings, "-");
        log_assert(delay_values_vec.size() == 3, "For each op, three delay values must have been specified in cmdline args for unp-math-pack");
        delay_values.at(0) = delay_values_vec.at(0);
        delay_values.at(1) = delay_values_vec.at(1);
        log_assert(!(delay_values.at(0) > 0 && delay_values.at(1) > 0), "Enabling both unpacker and math kernel delay is not allowed");
        delay_values.at(2) = delay_values_vec.at(2);
        op_to_delays.insert({op_name, delay_values});
    }

    for (const string &reset_op_name: reset_kernel_delay) {
        log_assert(op_to_delays.find(reset_op_name) == op_to_delays.end(), "--insert-kernel-delay and --reset-kernel-delay are both set for op {}", reset_op_name);
        op_to_delays.insert({reset_op_name, {0, 0, 0}});
    }

    log_info(tt::LogRuntime, "Kernel delays inserted:");
    std::stringstream ss;
    for (auto &op_delay : op_to_delays) {
        ss << op_delay.first << ": ";
        ss << "Unpacker: " << op_delay.second.at(0) << " Math: " << op_delay.second.at(1) << " Packer: " << op_delay.second.at(2) << endl;
    }
    log_info(tt::LogRuntime, "{}", ss.str());
    return op_to_delays;
}

// The input cmdline option for decoupling certain queues from the ops (no dram read/write to that queue):
// --decouple-dram <GRAPH_NAME>:<QUEUE_NAME0>,<GRAPH_NAME>:<QUEUE_NAME1>,<GRAPH_NAME>:AllReads
// e.g: --decouple-dram fwd_0:hidden_states
// There are two additional options for decoupling all dram reads/writes:
// e.g: <GRAPH_NAME>:AllReads or <GRAPH_NAME>:AllWrites
// To decouple all inputs and outputs of any op from dram:
// e.g: Use a single "*" (--decouple-dram "*") The double quotations are necessary
vector<PerfDramDecouple> set_dram_decouple_config(const std::string& dram_decouplings_desc) {
    vector<PerfDramDecouple> dram_decouple_config;
    if (dram_decouplings_desc.empty()) {
        return dram_decouple_config;
    }
    std::vector<std::string> each_queue;
    tt::args::split_string_into_vector(each_queue, dram_decouplings_desc, ",");
    for (const auto& dram_decouplings_single_op : each_queue) {
        PerfDramDecouple dram_decouple;
        if (dram_decouplings_single_op == "*") {
            dram_decouple.all_inputs = true;
            dram_decouple.all_outputs = true;
            dram_decouple.graph_name = "*";
            dram_decouple.queue_name = "*";
        } else {
            size_t colon_pos = dram_decouplings_single_op.find(':');
            log_assert(colon_pos != std::string::npos, "Could not find ':' in decoupling descriptor");
            dram_decouple.graph_name = dram_decouplings_single_op.substr(0, colon_pos);
            size_t queue_name_start = colon_pos + 1;
            string queue_name = dram_decouplings_single_op.substr(colon_pos+1, dram_decouplings_single_op.length());
            if (queue_name == "*") {
                dram_decouple.queue_name = "*";
                dram_decouple.all_inputs = true;
                dram_decouple.all_outputs = true;
            } else if (queue_name == "AllReads") {
                dram_decouple.queue_name = "*";
                dram_decouple.all_inputs = true;
                dram_decouple.all_outputs = false;
            } else if (queue_name == "AllWrites") {
                dram_decouple.queue_name = "*";
                dram_decouple.all_inputs = false;
                dram_decouple.all_outputs = true;
            } else {
                dram_decouple.queue_name = queue_name;
                dram_decouple.all_inputs = false;
                dram_decouple.all_outputs = false;
            }
        }
        dram_decouple_config.push_back(dram_decouple);        
    }

    log_info(tt::LogRuntime, "Dram decouple config:");
    for (auto &op_to_dram_decouple : dram_decouple_config) {
        op_to_dram_decouple.print();
    }
    return dram_decouple_config;
}

vector<int> decode_target_inputs_str(const vector<string>& target_inputs_encoded, int num_inputs) {
    vector<int> target_inputs;
    for (const string& target_input: target_inputs_encoded) {
        int delim_pos = target_input.find(':');
        if (delim_pos != std::string::npos) {
            int start_input = 0;
            int end_input = num_inputs;
            if (delim_pos != target_input.size()-1) {
                end_input = stoi(target_input.substr(delim_pos + 1, target_input.back()));
            }
            if (delim_pos != 0) {
                start_input = stoi(target_input.substr(0, delim_pos));
            }
            for (int i = start_input; i < end_input; i++) {
                target_inputs.push_back(i);
            }
        } else {
            target_inputs.push_back(stoi(target_input));
        }
    }
    return target_inputs;
}

template<typename T>
void set_comparison_config(PerfCheckValue<T> &perf_value, YAML::const_iterator it_metric, float override_perf_value) {
    if (it_metric->first.as<string>() == "expected") {
        perf_value.set_comparison_type("expected");
        perf_value.target_value = it_metric->second.as<T>();
        perf_value.en = true;
    }
    if (it_metric->first.as<string>() == "rtol") {
        perf_value.set_comparison_type("expected");
        perf_value.rtol = it_metric->second.as<float>();
    }
    if(it_metric->first.as<string>() == "min_bound") {
        perf_value.set_comparison_type("min_bound");
        perf_value.target_value = it_metric->second.as<T>();
        perf_value.en = true;
    }    
    if (perf_value.en && perf_value.target_value == 0) {
        perf_value.override_target_value = override_perf_value;
    }
}

/* Look for the perf target fetched from the CI database during make (make perf_lib/perf_targets). */
float get_ci_perf_target(const string &perf_target_yaml_path, const string &test_group, const string &test_name) {
    if (!fs::exists(perf_target_yaml_path) or !fs::is_regular_file(perf_target_yaml_path)) {
        log_debug(tt::LogPerfCheck, "Non existant perf target yaml {}. Skipping perf target override with CI database values.", perf_target_yaml_path);
        return 0;
    }

    YAML::Node ci_perf_targets_yaml = YAML::LoadFile(perf_target_yaml_path);
    string current_test_group, current_test_name;
    if (!ci_perf_targets_yaml[test_group]) {
        log_debug(tt::LogPerfCheck, "Test group {} not found in perf target yaml. Skipping perf target override with CI database values.", test_group);
        return 0;
    }
    if (!ci_perf_targets_yaml[test_group][test_name]) {
        log_debug(tt::LogPerfCheck, "Test name {} not found in perf target yaml. Skipping perf target override with CI database values.", test_name);
        return 0;
    }


    YAML::Node test_perf_target_node = ci_perf_targets_yaml[test_group][test_name];
    log_assert(test_perf_target_node["perf_target"], "Perf target information should be dumped if test group and test name exist.");
    log_assert(test_perf_target_node["perf_target"]["value"].IsDefined() && !test_perf_target_node["perf_target"]["value"].IsNull(), "Perf target value should be dumped if test group and test name exist.");

    float perf_target = test_perf_target_node["perf_target"]["value"].as<float>();
    return perf_target;
}

/* extract the test group and test name from the netlist.
all perf CI tests should have test-group, test-name fields under performance-check */
std::tuple<string, string> get_test_group_and_test_name(const YAML::Node &netlist_yaml) {
    string test_group = "";
    string test_name = "";
    for (YAML::const_iterator it = netlist_yaml.begin(); it != netlist_yaml.end(); ++it) {
        if (it->first.as<string>() != "performance-check") {
            continue;
        }
        for (YAML::const_iterator config_it = it->second.begin(); config_it != it->second.end(); ++config_it) {
            for (YAML::const_iterator it_each_config = config_it->second.begin(); it_each_config != config_it->second.end(); ++it_each_config) {
                if (it_each_config->first.as<string>() == "test-group") {
                    test_group = it_each_config->second.as<string>();
                }
                else if (it_each_config->first.as<string>() == "test-name") {
                    test_name = it_each_config->second.as<string>();
                }
            }
        }
    }
    return std::make_tuple(test_group, test_name);
}

vector<PerfComparisonConfig> set_comparison_configs(const string &netlist_file, const string &ci_perf_target_dir, float input_override_perf_value) {
    if (netlist_file == "") {
        return vector<PerfComparisonConfig>();
    }
    if (!fs::exists(netlist_file)) {
        log_fatal("Netlist file path does not exist: {}", netlist_file);
    }
    vector<PerfComparisonConfig> all_configs;
    YAML::Node netlist_yaml = YAML::LoadFile(netlist_file);

    // If the user provided an override value from the cmd line, use that
    // otherwise, look for the ci perf target fetched during make
    // if neither exist, use 0
    float override_perf_value = 0;
    if (input_override_perf_value != 0) {
        override_perf_value = input_override_perf_value;
    }
    else {
        string test_group, test_name;
        std::tie(test_group, test_name) = get_test_group_and_test_name(netlist_yaml);

        string ci_perf_target_yaml_path = ci_perf_target_dir + "/" + ci_perf_target_yaml;
        override_perf_value = get_ci_perf_target(ci_perf_target_yaml_path, test_group, test_name);
    }

    for (YAML::const_iterator it = netlist_yaml.begin(); it != netlist_yaml.end(); ++it) {
        if (it->first.as<string>() == "performance-check") {
            for (YAML::const_iterator config_it = it->second.begin(); config_it != it->second.end(); ++config_it) {
                PerfComparisonConfig perf_config;
                for (YAML::const_iterator it_each_config = config_it->second.begin(); it_each_config != config_it->second.end(); ++it_each_config) {
                    // if (it_each_config->first.as<string>() == "use-override-perf-target") {
                    //     perf_config.override_target_value = it_each_config->second.as<bool>();
                    // }
                    if (it_each_config->first.as<string>() == "graph-name") {
                        perf_config.graph_name = it_each_config->second.as<string>();
                    }
                    if (it_each_config->first.as<string>() == "program-name") {
                        perf_config.program_name = it_each_config->second.as<string>();
                    }
                    if (it_each_config->first.as<string>() == "math-utilization") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.math_utilization, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "execution-cycles") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<uint64_t>(perf_config.execution_cycles, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "cycles-per-tensor") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.num_cycles_per_input, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "tensors-per-second") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.num_inputs_per_second, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "average-math-utilization") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.average_math_utlization, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "total-runtime") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<uint64_t>(perf_config.total_runtime, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "input0-bw") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.input0_bw, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "output-bw") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.output_bw, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "backend-samples-per-second") {
                        for (YAML::const_iterator it_metric = it_each_config->second.begin(); it_metric != it_each_config->second.end(); ++it_metric) {
                            set_comparison_config<float>(perf_config.backend_samples_per_second, it_metric, override_perf_value);
                        }
                    }
                    if (it_each_config->first.as<string>() == "target-cores") {
                        vector<string> target_cores = it_each_config->second.as<vector<string>>();
                        for (const auto& core_str: target_cores) {
                            size_t delimiter_pos = core_str.find('-');
                            log_assert(delimiter_pos != std::string::npos, "Could not find '-' in decoupling descriptor"); // y-dim should exist in core coord.

                            std::string core_dim_x = core_str.substr(0, delimiter_pos);
                            size_t core_dim_y_start = delimiter_pos + 1;
                            std::string core_dim_y = core_str.substr(core_dim_y_start, core_str.length() - core_dim_y_start);
                            perf_config.target_cores.push_back(tt_xy_pair(std::stoi(core_dim_x), std::stoi(core_dim_y)));
                        }
                    }
                    if (it_each_config->first.as<string>() == "target-ops") {
                        perf_config.target_ops = it_each_config->second.as<vector<string>>();
                    }
                    if (it_each_config->first.as<string>() == "target-inputs") {
                        perf_config.target_inputs = it_each_config->second.as<vector<int>>();
                    }
                }
                all_configs.push_back(perf_config);
                perf_config.print();
            }
        }
    }
    return all_configs;
}

void check_perf_buffer_addresses_aligned() {
    static_assert(dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_BASE % NOC_ADDRESS_ALIGNMENT == 0, "DRAM_EACH_BANK_PERF_BUFFER_BASE must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 % NOC_ADDRESS_ALIGNMENT == 0, "UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0/2 % NOC_ADDRESS_ALIGNMENT == 0, "UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 % NOC_ADDRESS_ALIGNMENT == 0, "UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1/2 % NOC_ADDRESS_ALIGNMENT == 0, "UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0 % NOC_ADDRESS_ALIGNMENT == 0, "NCRISC_PERF_BUF_SIZE_LEVEL_0 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0/2 % NOC_ADDRESS_ALIGNMENT == 0, "NCRISC_PERF_BUF_SIZE_LEVEL_0/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1 % NOC_ADDRESS_ALIGNMENT == 0, "NCRISC_PERF_BUF_SIZE_LEVEL_1 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1/2 % NOC_ADDRESS_ALIGNMENT == 0, "NCRISC_PERF_BUF_SIZE_LEVEL_1/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::PERF_QUEUE_HEADER_ADDR % NOC_ADDRESS_ALIGNMENT == 0, "PERF_QUEUE_HEADER_ADDR must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::MATH_PERF_BUF_SIZE % NOC_ADDRESS_ALIGNMENT == 0, "MATH_PERF_BUF_SIZE must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::MATH_PERF_BUF_SIZE/2 % NOC_ADDRESS_ALIGNMENT == 0, "MATH_PERF_BUF_SIZE/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::BRISC_PERF_BUF_SIZE % NOC_ADDRESS_ALIGNMENT == 0, "BRISC_PERF_BUF_SIZE must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::BRISC_PERF_BUF_SIZE/2 % NOC_ADDRESS_ALIGNMENT == 0, "BRISC_PERF_BUF_SIZE/2 must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::MATH_PERF_BUF_BASE_ADDR % NOC_ADDRESS_ALIGNMENT == 0, "MATH_PERF_BUF_BASE_ADDR must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::BRISC_PERF_BUF_BASE_ADDR % NOC_ADDRESS_ALIGNMENT == 0, " BRISC_PERF_BUF_BASE_ADDR must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_BASE_ADDR % NOC_ADDRESS_ALIGNMENT == 0, "UNPACK_PACK_PERF_BUF_BASE_ADDR must be NOC_ADDRESS_ALIGNMENT bytes aligned");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 >= l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0, "Unpack/pack perf buffer size must be the largest");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 >= l1_mem::address_map::MATH_PERF_BUF_SIZE, "Unpack/pack perf buffer size must be the largest");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 >= l1_mem::address_map::BRISC_PERF_BUF_SIZE, "Unpack/pack perf buffer size must be the largest");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 >= l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0, "Unpack/pack perf buffer size must be the largest");
    static_assert(l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 >= l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1, "Unpack/pack perf buffer size must be the largest");
}

PerfDesc::PerfDesc() {
    check_perf_buffer_addresses_aligned();
}

PerfDesc::PerfDesc(vector<string> &args, string netlist_path) {
    
    const bool decouple_overlay_for_all_ops = parse_env("TT_BACKEND_PERF_ANALYZER", false);
    const bool fork_join_analyzer_en = parse_env("TT_BACKEND_FORK_JOIN_ANALYZER", false);
    const bool force_enable_perf_trace = fork_join_analyzer_en || decouple_overlay_for_all_ops;
    if (args.size() == 0 && !decouple_overlay_for_all_ops) {
        return;
    }
    check_perf_buffer_addresses_aligned();
    bool perf_dump_en = false;
    uint perf_level = 0;
    bool intermediate_dump = false;
    bool concurrent_dump = false;
    bool single_dump = false;

    string perf_op_mode_desc;
    string overlay_decoupling_desc_config;
    string comparison_config_file;
    bool debug_mailbox = false;
    string target_inputs_str;
    string target_epochs_str;
    string triplet_modes_str;
    string reset_triplet_modes_str;
    string kernel_delay_desc;
    string decouple_dram_desc;
    string reset_input_output_delay_str;
    string override_perf_targets_dir;
    
    std::tie(single_dump, args) = tt::args::has_command_option_and_remaining_args(args, "--dump-perf-events");
    std::tie(intermediate_dump, args) = tt::args::has_command_option_and_remaining_args(args, "--dump-perf-events-intermediate");
    std::tie(concurrent_dump, args) = tt::args::has_command_option_and_remaining_args(args, "--dump-perf-events-concurrent");
    std::tie(check_total_num_traces, args) = tt::args::get_command_option_uint32_and_remaining_args(args, "--check-total-num-traces", 0);
    std::tie(perf_level, args) = tt::args::get_command_option_uint32_and_remaining_args(args, "--perf-level", 0);
    std::tie(perf_op_mode_desc, args) = tt::args::get_command_option_and_remaining_args(args, "--perf-op-mode", std::string(""));
    std::tie(overlay_decoupling_desc_config, args) = tt::args::get_command_option_and_remaining_args(args, "--decouple-overlay", std::string(""));
    std::tie(debug_mailbox, args) = tt::args::has_command_option_and_remaining_args(args, "--dump-debug-mailbox");
    std::tie(decouple_dram_desc, args) = tt::args::get_command_option_and_remaining_args(args, "--decouple-dram", std::string(""));
    std::tie(target_inputs_str, args) = tt::args::get_command_option_and_remaining_args(args, "--perf-target-inputs", std::string(""));
    std::tie(target_epochs_str, args) = tt::args::get_command_option_and_remaining_args(args, "--perf-target-graphs", std::string(""));
    std::tie(suppress_warning_messages, args) = tt::args::has_command_option_and_remaining_args(args, "--perf-suppress-warnings");
    std::tie(override_perf_output_dir, args) = tt::args::get_command_option_and_remaining_args(args, "--perf-output-dir", std::string(""));
    std::tie(measure_steady_state_perf, args) = tt::args::has_command_option_and_remaining_args(args, "--measure-steady-state");
    std::tie(dump_perf_vcd, args) = tt::args::has_command_option_and_remaining_args(args, "--dump-perf-vcd");
    std::tie(triplet_modes_str, args) = tt::args::get_command_option_and_remaining_args(args, "--triplet-mode", std::string(""));
    std::tie(reset_triplet_modes_str, args) = tt::args::get_command_option_and_remaining_args(args, "--reset-triplet-mode", std::string(""));
    std::tie(cmdline_override_perf_target, args) = tt::args::get_command_option_double_and_remaining_args(args, "--perf-target", 0.0);
    std::tie(skip_perf_check, args) = tt::args::has_command_option_and_remaining_args(args, "--skip-perf-check");
    std::tie(generate_original_report, args) = tt::args::has_command_option_and_remaining_args(args, "--generate-original-report");
    std::tie(kernel_delay_desc, args) = tt::args::get_command_option_and_remaining_args(args, "--insert-kernel-delay", std::string(""));
    std::tie(reset_input_output_delay_str, args) = tt::args::get_command_option_and_remaining_args(args, "--reset-kernel-delay", std::string(""));
    std::tie(append_device_runtime_to_host_report, args) = tt::args::has_command_option_and_remaining_args(args, "--append-device-runtime-to-host");
    std::tie(reset_dram_perf_buffer, args) = tt::args::has_command_option_and_remaining_args(args, "--reset-dram-perf-buffers");
    std::tie(override_perf_targets_dir, args) = tt::args::get_command_option_and_remaining_args(args, "--perf-targets-dir", std::string(""));

    perf_dump_en = single_dump || intermediate_dump || concurrent_dump;
    if (force_enable_perf_trace) {
        if (!perf_dump_en) {
            single_dump = true;
            perf_level = 0;
            target_inputs_str = "";
            log_warning(tt::LogPerfPostProcess, "Enabling performance trace since overlay decoupling perf-analysis is enabled");
        }
    }
    if (decouple_overlay_for_all_ops) {
        overlay_decoupling_desc_config = "*:Input-Output";
        run_perf_analyzer = true;
        suppress_warning_messages = true;
    }

    log_assert((single_dump == true) + (intermediate_dump == true) + (concurrent_dump == true) <= 1, "Maximum one of the perf modes can be enabled at any time");
    if (intermediate_dump) {
        device_perf_mode = perf::PerfDumpMode::IntermediateDump;
    } else if (single_dump) {
        device_perf_mode = perf::PerfDumpMode::SingleDumpPerEpoch;
    } else if (concurrent_dump) {
        device_perf_mode = perf::PerfDumpMode::Concurrent;
    }
    perf_dump_level = perf_level;

    const string perf_output_dir_override_env = parse_env("TT_BACKEND_PERF_OUTPUT_DIR", std::string(""));

    if (!perf_output_dir_override_env.empty()) {
        log_assert(override_perf_output_dir.empty(), "The performance output directory override can only either be specified by the cmdline option or the env var");
        override_perf_output_dir = perf_output_dir_override_env;
    }

    perf_targets_dir = "./build/perf_targets";

    if (!override_perf_targets_dir.empty()) {
        perf_targets_dir = override_perf_targets_dir;
    }

    trisc_decouplings = set_trisc_perf_decouplings(perf_op_mode_desc);
    initial_trisc_decouplings = trisc_decouplings;
    
    const string overlay_decoupling_desc_path = parse_env("TT_BACKEND_OVERLAY_DECOUPLE_DESC_PATH", std::string(""));
    log_assert(overlay_decoupling_desc_config.empty() || overlay_decoupling_desc_path.empty(), "Both decouple-overlay config and decouple-overlay descriptor path are set");
    if (!overlay_decoupling_desc_path.empty()) {
        log_assert(overlay_decoupling_desc_config.empty(), "When overlay decouple descriptor path is provided to the test, the --decoule-overlay config should be empty");
        overlay_decoupling_desc_config = get_overlay_decouple_desc_from_file(overlay_decoupling_desc_path);
    }
    overlay_decouplings = set_overlay_perf_decouplings(overlay_decoupling_desc_config);

    comparison_configs = set_comparison_configs(netlist_path, perf_targets_dir, cmdline_override_perf_target);
    // If all comparison configs have 0 expected value, then force skip_perf_check to true
    bool should_force_skip_perf_check = true;
    for (const PerfComparisonConfig &config : comparison_configs) {
        if (config.has_non_zero_expected_value()) {
            should_force_skip_perf_check = false;
            break;
        }
    }
    if (should_force_skip_perf_check and !skip_perf_check) {
        log_warning(tt::LogPerfCheck, "Forcing skip perf check because no non-zero perf targets were specified or found");
        skip_perf_check = true;
    }

    dump_debug_mailbox = debug_mailbox;
    tt::args::split_string_into_vector(target_inputs, target_inputs_str, ",");
    vector<string> target_epochs_vec;
    tt::args::split_string_into_vector(target_epochs_vec, target_epochs_str, ",");
    target_epochs.insert(target_epochs_vec.begin(), target_epochs_vec.end());
    tt::args::split_string_into_vector(triplet_modes, triplet_modes_str, ",");
    tt::args::split_string_into_vector(reset_triplet_modes, reset_triplet_modes_str, ",");
    tt::args::split_string_into_vector(reset_input_output_delay, reset_input_output_delay_str, ",");
    op_name_to_kernel_delay = set_kernel_delays(kernel_delay_desc, reset_input_output_delay);
    dram_decouple_config = set_dram_decouple_config(decouple_dram_desc);
    if (device_perf_mode == perf::PerfDumpMode::Disable) {
        log_assert(overlay_decouplings.size() == 0, "Overlay decouplings can only be enabled if perf_trace is enabled as well");
        log_assert(initial_trisc_decouplings.size() == 0, "Kernel decouplings can only be enabled if perf_trace is enabled as well");
        log_assert(dram_decouple_config.size() == 0, "Dram decouplings can only be enabled if perf_trace is enabled as well");
        log_assert(triplet_modes.size() == 0, "Triplet modes can only be enabled if perf_trace is enabled as well");
        log_assert(target_inputs.size() == 0, "perf_target_inputs can be specified if perf_trace is enabled as well");
        log_assert(target_epochs.size() == 0, "perf_target_graphs can be specified if perf_trace is enabled as well");
    }
    if (overlay_decouplings.size() > 0) {
        log_assert(op_name_to_kernel_delay.size() == 0, "Overlay decoupling and kernel delay cannot be enabled at the same time because they share UNPACK_MATH_DONE semaphore");
    }
}

bool PerfDesc::is_normal_perf_mode() const {
    if (!triplet_modes.empty()) {
        return false;
    }
    for (const auto &[op_name, op_decouplings] : initial_trisc_decouplings) {
        for (const PerfTriscDecoupleMode& decouple: op_decouplings) {
            if (decouple != PerfTriscDecoupleMode::None) {
                return false;
            }
        }
    }
    return true;
}

string PerfDesc::get_decouple_mode_name() const {
    string decouple_str;
    uint op_id = 0;
    if (is_normal_perf_mode()) {
        decouple_str += "device";
    }
    else {
        for (const auto &[op_name, op_decouplings] : initial_trisc_decouplings) {
            // If the op decoupling is in mode "None" skip it in generating the name for this mode.
            if (op_decouplings.find(PerfTriscDecoupleMode::None) != op_decouplings.end()) {
                continue;
            }
            if (op_id > 0) {
                decouple_str += "\n";
            }
            decouple_str += "- decoupling_op_" + op_name;
            uint decouple_id = 0;
            for (const PerfTriscDecoupleMode& config: op_decouplings) {
                decouple_str +=  "_" + PerfTriscDecoupleModetoString(config);
                decouple_id++;
            }
            op_id++;
        }
        int triplet_idx = 0;
        for (const string &triplet: triplet_modes) {
            if (!decouple_str.empty() or triplet_idx > 0) {
                decouple_str += "\n";
            }
            decouple_str += "- triplet_" + triplet;
            triplet_idx++;
        }
    }
    return decouple_str;
}

string PerfDesc::get_overlay_decouple_string() const {
    std::stringstream ss;
    for(const auto &[op_name, overlay_decouple_config] : overlay_decouplings) {
        ss << op_name << ": ";
        ss << overlay_decouple_config << "\n";
    }
    return ss.str();
}

void print_epoch_perf_table(const vector<EpochPerfInfo> &all_epochs, const string &output_path, bool steady_state) {
    bool report_exists = fs::exists(output_path);
    std::ofstream output_file(output_path, std::ios::app);
    if (report_exists) {
        output_file << "\n";
    }
    table::PrettyTable table;
    table.horizontal_line_row = 1;
    table.vertical_line_col = 1;
    vector<string> row = {
        "program-name",
        "graph-name",
        "aiclk",
        "device-id",
        "input-count",
        "first-to-last-inputs",
        "num-cycles-per-tensor-all-inputs",
        "num-tensors-per-second-all-inputs",
        "num-cycles-all-inputs"};
    if (steady_state) {
        vector<string> new_row = {
            "middle-inputs",
            "num-cycles-per-tensor-mid-inputs",
            "num-tensors-per-second-mid-inputs"};
        row.insert(row.end(), new_row.begin(), new_row.end());
    }
    row.push_back("largest-wait-for-epoch-binary");
    table.add_row(row);
    for (EpochPerfInfo epoch_info: all_epochs) {
        bool first_and_last_inputs_recorded = epoch_info.first_and_last_inputs_recorded.first != -1 &&
                                            epoch_info.first_and_last_inputs_recorded.second != -1;
        bool steady_state_inputs_recorded = epoch_info.steady_state_first_and_last_inputs.first != -1 &&
                                            epoch_info.steady_state_first_and_last_inputs.second != -1;
        bool latency_recorded = epoch_info.first_and_last_inputs_recorded.first == 0 &&
                                (epoch_info.first_unpack_first_input != 0) &&
                                (epoch_info.last_pack_last_input != 0);
        vector<string> measurements = {
            epoch_info.program_name,
            epoch_info.graph_name,
            to_string(epoch_info.aiclk),
            to_string(epoch_info.device_id),
            to_string(epoch_info.input_count),
            first_and_last_inputs_recorded ? to_string(epoch_info.first_and_last_inputs_recorded.first) + "->" +
                to_string(epoch_info.first_and_last_inputs_recorded.second) : "N/A",
            epoch_info.num_cycles_per_input ? to_string(int(epoch_info.num_cycles_per_input)) : "N/A",
            epoch_info.num_inputs_per_second ? to_string(int(epoch_info.num_inputs_per_second)) : "N/A",
            latency_recorded ? to_string(epoch_info.last_pack_last_input - epoch_info.first_unpack_first_input): "N/A"};
        if (steady_state) {
            vector<string> new_measurement = {
                steady_state_inputs_recorded ? to_string(epoch_info.steady_state_first_and_last_inputs.first) + "->" +
                    to_string(epoch_info.steady_state_first_and_last_inputs.second) : "N/A",
                epoch_info.num_cycles_per_input_steady_state ? to_string(int(epoch_info.num_cycles_per_input_steady_state)) : "N/A",
                epoch_info.num_inputs_per_second_steady_state ? to_string(int(epoch_info.num_inputs_per_second_steady_state)) : "N/A"};
            measurements.insert(measurements.end(), new_measurement.begin(), new_measurement.end());
        }
        measurements.push_back(to_string(epoch_info.largest_wait_for_epoch_binary_cycles));
        table.add_row(measurements);
    }
    std::cout << table.generate_table_string() << std::endl;
    output_file << table.generate_table_string(table::PrettyTable::Format::CSV);
}

void tt_backend_perf::setup_host_perf_profiler(string in_test_output_dir, string in_perf_output_dir, uint in_pid, bool check_thread_exists) {
    if (en) {
        const std::lock_guard<std::mutex> lock(backend_profiler_mutex);
        test_output_dir = in_test_output_dir;
        perf_output_dir = in_perf_output_dir;
        process_id = in_pid;
        uint thread_id = get_thread_id();
        log_info(tt::LogPerfPostProcess, "Setting up backend profiler for process_id {} thread {} output directory {}", process_id, thread_id, in_test_output_dir);
        if (check_thread_exists) {
            log_assert(active_profiler_threads.find(thread_id) == active_profiler_threads.end(), "Each thread within each process must call setup_host_perf_profiler exactly once.");
        }
        active_profiler_threads.insert(thread_id);
    }
}

void tt_backend_perf::finish_host_perf_profiler(bool called_by_destructor) {
    if (en) {
        const std::lock_guard<std::mutex> lock(backend_profiler_mutex);
        uint thread_id = get_thread_id();
        log_info(tt::LogPerfPostProcess, "Generating backend profiler report for process_id {} thread {}", process_id, thread_id);
        if (last_thread_processing() || called_by_destructor) {
            if (active_profiler_threads.find(thread_id) == active_profiler_threads.end()) {
                log_warning(tt::LogPerfPostProcess, "Skipping host postprocess report for pid {}, thread {}, Since profiler was not initialized for this thread", process_id, thread_id);
                return;
            }

            // if (called_by_destructor && !perf_buffer.empty()) {
            //     log_warning(tt::LogPerfPostProcess,
            //         "Backend profiler desctructor was called by process_id {} thread {} and there are un-processed events. Running the postprocessor on the remaining events", uint(process_id), thread_id);
            // }
            if (!called_by_destructor || !perf_buffer.empty()) {
                postprocess::run_host_perf_postprocess(
                    perf_buffer,
                    all_labels,
                    test_output_dir,
                    perf_output_dir,
                    process_id,
                    false,
                    total_input_count);
            }

            log_assert(active_profiler_threads.find(thread_id) != active_profiler_threads.end(), "thread id {} is not initialized for backend profiler", thread_id);
            active_profiler_threads.erase(thread_id);
        } else if (!all_threads_finished()) {
            log_warning(tt::LogPerfPostProcess, "Skipping backend profiler postprocessor for process {} thread {}, since there are other active threads", process_id, thread_id);
            active_profiler_threads.erase(thread_id);
        } else {
            log_debug(tt::LogPerfPostProcess, "Skipping backend profiler postprocessor for process {} thread {}, since there are no active profiler threads", process_id, thread_id);
        }
        if (all_threads_finished()) {
            // log_info(tt::LogPerfPostProcess, "Cleaning all events in host profiler for all threads");
            perf_buffer.clear();
            active_profiler_threads.clear();
        }
        postprocessor_executed = true;
    }
}

// For device alignment, pass these variables in directly from runtime since
// setup_host_perf_profiler is not always called (if the backend profiler is not enabled)
void tt_backend_perf::generate_device_alignment_report(string in_test_output_dir, string in_perf_output_dir, pid_t in_process_id) {
    postprocess::run_host_perf_postprocess(
        device_start_end_buffer,
        vector<string>(),
        in_test_output_dir,
        in_perf_output_dir,
        in_process_id,
        true,
        total_input_count);
}

void tt_backend_perf::set_total_input_count(int total_sample_count) {
    total_input_count = total_sample_count;
}

ScopedEventProfiler::ScopedEventProfiler(const string &event_label) {
    this->profiler_func = [event_label]() { backend_profiler.record_loader_event(event_label); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(const string &event_label, uint64_t event_value) {
    this->profiler_func = [event_label, event_value]() { backend_profiler.record_loader_event(event_label, event_value); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(uint64_t event_id) {
    this->profiler_func = [event_id]() { backend_profiler.record_loader_event(event_id); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(uint64_t event_id, uint64_t event_value) {
    this->profiler_func = [event_id, event_value]() { backend_profiler.record_loader_event(event_id, event_value); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(const perf::HostEventType &event) {
    this->profiler_func = [event]() { backend_profiler.record_loader_event(event); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler( const perf::HostEventType &event, uint device_id) {
    this->profiler_func = [event, device_id]() { backend_profiler.record_loader_event(event, device_id); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(const perf::HostEventType &event, uint device_id, uint epoch_id) {
    this->profiler_func = [event, device_id, epoch_id]() { backend_profiler.record_loader_event(event, device_id, epoch_id); };
    this->profiler_func();
}

ScopedEventProfiler::ScopedEventProfiler(const perf::HostEventType &event, uint device_id, uint epoch_id, uint program_id) {
    this->profiler_func = [event, device_id, epoch_id, program_id]() {backend_profiler.record_loader_event(event, device_id, epoch_id, program_id); };
    this->profiler_func();
}


} // namespace perf

namespace table {

// ------------------------------------------------
//           Pretty Table
// ------------------------------------------------

std::string spaces(int num) {
    std::string spaces_string;
    for (int i = 0; i < num; i++) {
        spaces_string.append(" ");
    }
    return spaces_string;
}

std::string lines(int num) {
    std::string spaces_string;
    for (int i = 0; i < num; i++) {
        spaces_string.append("-");
    }
    return spaces_string;
}

void PrettyTable::add_row(std::vector<std::string> incoming_new_row) {
    std::vector<std::string> new_row;

    for (int i = 0; i < incoming_new_row.size(); i++) {
        new_row.push_back(incoming_new_row[i]);
    }
    table_.push_back(new_row);

    // std::cout << table_.size() << std::endl;
    // std::cout << table_[0].size() << std::endl;
}

void PrettyTable::add_divider() {
    table_.push_back({"__divider__"});
}

bool PrettyTable::is_divider_row(int row_index) const {
    return table_[row_index].size() == 1 and table_[row_index][0] == "__divider__";
}

std::string PrettyTable::generate_table_string(Format format) {
    // Validate - all rows must be equal in size
    std::string column_delimeter = (format == Format::CSV) ? "," : "|  ";

    int row_size = table_[0].size();
    for (int row_index = 0; row_index < table_.size(); row_index++) {
        if (is_divider_row(row_index)) {
            continue;
        }
        if (row_size != table_[row_index].size()) {
            throw std::runtime_error("ERROR: al table rows must be the same size");
        }
    }

    // Pad cells per column
    if (format == Format::Pretty) {
        for (int col_index = 0; col_index < row_size; col_index++) {
            int max_cell_size = 0;

            // Determine max cell size in this column
            for (int row_index = 0; row_index < table_.size(); row_index++) {
                if (is_divider_row(row_index)) {
                    continue;
                }
                int cell_size = table_[row_index][col_index].size();
                if (max_cell_size < cell_size) {
                    max_cell_size = cell_size;
                }
            }

            // Pad cells to max cell size
            for (int row_index = 0; row_index < table_.size(); row_index++) {
                if (is_divider_row(row_index)) {
                    continue;
                }
                int cell_size = table_[row_index][col_index].size();
                std::string cell_padding = spaces(max_cell_size - cell_size + padding_between_cells);
                table_[row_index][col_index].append(cell_padding);
            }
        }
    }

    // Calculate horizontal line size
    int string_length_of_row = 0;
    int string_length_of_vertical_lines = (row_size + 1) * 3 - 2;
    for (int col_index = 0; col_index < row_size; col_index++) {
        string_length_of_row += table_[0][col_index].size();
    }
    int horizontal_line_string_size = string_length_of_row + string_length_of_vertical_lines;

    // Create string
    std::string table_string;

    if (format == Format::Pretty) {
        table_string.append(lines(horizontal_line_string_size));
        table_string.append("\n");
    }

    for (int row_index = 0; row_index < table_.size(); row_index++) {
        int row_size = table_[row_index].size();

        if (row_index == horizontal_line_row and format == Format::Pretty) {
            table_string.append(lines(horizontal_line_string_size));
            table_string.append("\n");
        }

        if (is_divider_row(row_index)) {
            table_string.append(lines(horizontal_line_string_size));
            table_string.append("\n");
        } else {
            for (int col_index = 0; col_index < row_size; col_index++) {
                table_string.append(column_delimeter);
                table_string.append(table_[row_index][col_index]);
            }
            table_string.append(column_delimeter);
            table_string.append("\n");
        }
    }

    if (format == Format::Pretty) {
        table_string.append(lines(horizontal_line_string_size));
        table_string.append("\n");
    }

    return table_string;
}

}
