// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "create_reports.hpp"
#include "utils.hpp"
#include "third_party/json/json.hpp"
#include "yaml-cpp/yaml.h"

using namespace perf;
namespace postprocess {

uint64_t get_rebiased_value(const tt_perf_device_alignment &device_alignment, const InstructionInfo &instr, uint64_t original_value) {
    log_assert(device_alignment.device_id_to_start_cycle.find(instr.device_id) != device_alignment.device_id_to_start_cycle.end(),
            "Device start time not recorded for perf alignment");
    uint64_t device_start_cycle = device_alignment.device_id_to_start_cycle.at(instr.device_id);
    
    if (original_value >= device_start_cycle) {
        return original_value - device_start_cycle;
    } else {
        return UINT64_MAX - (device_start_cycle - original_value);
    }
}

uint64_t get_cycle(const tt_perf_device_alignment &device_alignment, const InstructionInfo &instr, uint64_t original_value, bool align_devices) {
    if (align_devices) {
        return get_rebiased_value(device_alignment, instr, original_value);
    } else {
        return original_value;
    }
}

inline void update_longest_op(const core_events& core, uint input_idx, uint64_t new_runtime, map<uint, pair<string, uint64_t>>& input_to_longest_op) {
    if (input_to_longest_op.find(input_idx) != input_to_longest_op.end()) {
        uint64_t previous_longest_op = input_to_longest_op.at(input_idx).second;
        if (new_runtime > previous_longest_op) {
            input_to_longest_op.at(input_idx) = {core.descriptor.op_name, new_runtime};
        }
    } else {
        input_to_longest_op.insert({input_idx, {core.descriptor.op_name, new_runtime}});
    }
}

bool is_op_decoupled(const PerfDesc &perf_desc, const string& op_name) {
    return (perf_desc.trisc_decouplings.find(op_name) != perf_desc.trisc_decouplings.end()) &&
           (perf_desc.trisc_decouplings.at(op_name).find(PerfTriscDecoupleMode::MathPack) != perf_desc.trisc_decouplings.at(op_name).end() ||
            perf_desc.trisc_decouplings.at(op_name).find(PerfTriscDecoupleMode::UnpMath)  != perf_desc.trisc_decouplings.at(op_name).end());
}

inline void check_and_populate_per_thread_events(json &per_thread_events_map, const core_events& core, const tt_perf_device_alignment &device_alignment_info, const PerfDesc &perf_desc, const InstructionInfo &instr, map<uint, pair<string, uint64_t>>& input_to_longest_op, bool align_devices) {
    
    map<uint, outer_loop_events> all_outer_loop_events = core.all_outer_loop_events;
    for (const auto& outer_loop_to_event:all_outer_loop_events) {
        string input_key = "input-" + to_string(outer_loop_to_event.first);
        if (outer_loop_to_event.second.unpack_first_instruction != ULLONG_MAX) {
            per_thread_events_map[input_key]["unpack-first-block-data-available"] = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.unpack_first_instruction, align_devices);
        } else {
            per_thread_events_map[input_key]["unpack-first-block-data-available"] = "N/A";
        }

        if (outer_loop_to_event.second.pack_first_start != ULLONG_MAX) {
            per_thread_events_map[input_key]["pack-start-outer-loop"] = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_first_start, align_devices);
        } else {
            per_thread_events_map[input_key]["pack-start-outer-loop"] = "N/A";
        }

        if (outer_loop_to_event.second.pack_last_end != ULLONG_MAX) {
            per_thread_events_map[input_key]["pack-end-outer-loop"] = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_last_end, align_devices);
        } else {
            per_thread_events_map[input_key]["pack-end-outer-loop"] = "N/A";
        }

        bool record_total_waits = (
            perf_desc.device_perf_mode == PerfDumpMode::IntermediateDump || perf_desc.device_perf_mode == PerfDumpMode::Concurrent) &&
            perf_desc.perf_dump_level > 0;
        if (record_total_waits && outer_loop_to_event.second.total_wait_for_tile_after_first_unpack != ULLONG_MAX) {
            per_thread_events_map[input_key]["total-unpack-wait-for-tile-after-first-unpack"] = outer_loop_to_event.second.total_wait_for_tile_after_first_unpack;
        } else {
            per_thread_events_map[input_key]["total-unpack-wait-for-tile-after-first-unpack"] = "N/A";
        }

        if (record_total_waits && outer_loop_to_event.second.total_wait_for_free_tiles_after_first_unpack != ULLONG_MAX) {
            per_thread_events_map[input_key]["total-wait-for-free-tile-after-first-unpack"] = outer_loop_to_event.second.total_wait_for_free_tiles_after_first_unpack;
        } else {
            per_thread_events_map[input_key]["total-wait-for-free-tile-after-first-unpack"] = "N/A";
        }

        if (perf_desc.device_perf_mode == PerfDumpMode::IntermediateDump || perf_desc.device_perf_mode == PerfDumpMode::Concurrent) {
            per_thread_events_map[input_key]["unpacker-stalled-on-ncrisc-intermediate-dump"] = outer_loop_to_event.second.total_trisc0_stalled_on_ncrisc;
            per_thread_events_map[input_key]["packer-stalled-on-ncrisc-intermediate-dump"] = outer_loop_to_event.second.total_trisc2_stalled_on_ncrisc;
        } else {
            per_thread_events_map[input_key]["unpacker-stalled-on-ncrisc-intermediate-dump"] = "N/A";
            per_thread_events_map[input_key]["packer-stalled-on-ncrisc-intermediate-dump"] = "N/A";
        }

        bool unpack_start_exist = outer_loop_to_event.second.unpack_first_instruction             != ULLONG_MAX;
        bool pack_start_exist   = outer_loop_to_event.second.pack_first_start                         != ULLONG_MAX;
        bool pack_end_exist     = outer_loop_to_event.second.pack_last_end                            != ULLONG_MAX;
        bool unpack_wait_exist  = outer_loop_to_event.second.total_wait_for_tile_after_first_unpack   > 0;
        bool math_activity_exist= core.math_activity != ULLONG_MAX;
                
        if (unpack_start_exist && pack_end_exist && !is_op_decoupled(perf_desc, core.descriptor.op_name)) {
            uint64_t pack_end = outer_loop_to_event.second.pack_last_end;
            uint64_t unpack_start = outer_loop_to_event.second.unpack_first_instruction;
            if (unpack_start != ULLONG_MAX && pack_end != ULLONG_MAX) {
                if (pack_end >= unpack_start) {
                    per_thread_events_map[input_key]["first-unpack-to-last-pack"] = pack_end - unpack_start;
                    update_longest_op(core, outer_loop_to_event.first, pack_end - unpack_start, input_to_longest_op);
                    if (unpack_wait_exist) {
                        uint64_t wait_for_tile = outer_loop_to_event.second.total_wait_for_tile_after_first_unpack;
                        if (pack_end - unpack_start >= wait_for_tile) {
                            per_thread_events_map[input_key]["first-unpack-to-last-pack-without-wait-tile"] = pack_end - unpack_start - wait_for_tile;
                        }
                    }
                    if (math_activity_exist) {
                        per_thread_events_map[input_key]["math-utilization-first-unpack-to-last-pack"] = float(core.math_activity) / (pack_end - unpack_start) * 100;
                        if (unpack_wait_exist) {
                            uint64_t wait_for_tile = outer_loop_to_event.second.total_wait_for_tile_after_first_unpack;
                            if (pack_end - unpack_start >= wait_for_tile) {
                                per_thread_events_map[input_key]["math-utilization-first-unpack-to-last-pack-without-wait-tile"] = float(core.math_activity) / (pack_end - unpack_start - wait_for_tile) * 100;
                            }
                        }
                    }
                } else {
                    per_thread_events_map[input_key]["first-unpack-to-last-pack"] = "Invalid Runtime Value";
                }
            }
        }

        if (pack_start_exist && pack_end_exist) {
            uint64_t pack_start = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_first_start, align_devices);
            uint64_t pack_end = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_last_end, align_devices);
            if (pack_start != ULLONG_MAX && pack_end != ULLONG_MAX) {
                per_thread_events_map[input_key]["pack-runtime"] = pack_end - pack_start;
            }
        }

        if (math_activity_exist) {
            per_thread_events_map[input_key]["math-activity"] = core.math_activity;
        }

        if (!per_thread_events_map[input_key].contains("first-unpack-to-last-pack")) {
            per_thread_events_map[input_key]["first-unpack-to-last-pack"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("first-unpack-to-last-pack-without-wait-tile")) {
            per_thread_events_map[input_key]["first-unpack-to-last-pack-without-wait-tile"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("pack-runtime")) {
            per_thread_events_map[input_key]["pack-runtime"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("math-utilization-first-unpack-to-last-pack")) {
            per_thread_events_map[input_key]["math-utilization-first-unpack-to-last-pack"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("math-utilization-first-unpack-to-last-pack-without-wait-tile")) {
            per_thread_events_map[input_key]["math-utilization-first-unpack-to-last-pack-without-wait-tile"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("math-utilization-over-math-thread")) {
            per_thread_events_map[input_key]["math-utilization-over-math-thread"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("total-unpack-wait-for-tile-after-first-unpack")) {
            per_thread_events_map[input_key]["total-unpack-wait-for-tile-after-first-unpack"] = "N/A";
        }
        if (!per_thread_events_map[input_key].contains("math-activity")) {
            per_thread_events_map[input_key]["math-activity"] = "N/A";
        }
    }
}

void calculate_and_populate_average_utilization_and_total_runtime(
        const core_events &core,
        json &inputs_common_events,
        const tt_perf_device_alignment &device_alignment_info,
        const PerfDesc &perf_desc,
        const tt_digraph &graph,
        const InstructionInfo &instr,
        const PostprocessModelDesc &perf_model_desc,
        bool align_devices) {
    
    string math_thread_name = thread_names.at(1);
    uint64_t first_unpack_first_input = ULLONG_MAX;
    uint64_t first_unpack_last_input = ULLONG_MAX;
    uint64_t last_pack_last_input = ULLONG_MAX;
    uint64_t last_pack_first_input = ULLONG_MAX;
    uint64_t total_math_activity = ULLONG_MAX;
    pair<int, int> first_and_last_inputs = {-1, -1};
    
    string op_name = core.descriptor.op_name;

    if (core.threads.find(math_thread_name) != core.threads.end()) {
        thread_events math_thread = core.threads.at(math_thread_name);
        uint64_t math_activity = core.math_activity;
        first_and_last_inputs = get_first_and_last_recorded_input_idx(core);
        bool both_inputs_populated = first_and_last_inputs.first != -1 &&
                                    first_and_last_inputs.second != -1;
        uint num_inputs = first_and_last_inputs.second - first_and_last_inputs.first + 1;
        if (math_activity != ULLONG_MAX && first_and_last_inputs.first != -1 && first_and_last_inputs.second != ULLONG_MAX) {
            total_math_activity = num_inputs * math_activity;
        }
    }
    bool both_inputs_populated = first_and_last_inputs.first != -1 &&
                                first_and_last_inputs.second != -1;
    if (both_inputs_populated) {
        if (core.all_outer_loop_events.find(first_and_last_inputs.first) != core.all_outer_loop_events.end()) {
            first_unpack_first_input =  get_cycle(device_alignment_info, instr, core.all_outer_loop_events.at(first_and_last_inputs.first).unpack_first_instruction, align_devices);
            last_pack_first_input =  get_cycle(device_alignment_info, instr, core.all_outer_loop_events.at(first_and_last_inputs.first).pack_last_end, align_devices);
        }
        if (core.all_outer_loop_events.find(first_and_last_inputs.second) != core.all_outer_loop_events.end()) {
            first_unpack_last_input =  get_cycle(device_alignment_info, instr, core.all_outer_loop_events.at(first_and_last_inputs.second).unpack_first_instruction, align_devices);
            last_pack_last_input =  get_cycle(device_alignment_info, instr, core.all_outer_loop_events.at(first_and_last_inputs.second).pack_last_end, align_devices);
        }
    }
    bool total_runtime_available = first_unpack_first_input != ULLONG_MAX &&
                        last_pack_last_input != ULLONG_MAX &&
                        last_pack_first_input != ULLONG_MAX &&
                        first_and_last_inputs.first != -1 &&
                        first_and_last_inputs.second != -1;
    bool utilization_available = total_runtime_available && total_math_activity != ULLONG_MAX;
    if (total_runtime_available) {
        inputs_common_events["first-input-recorded"] = first_and_last_inputs.first;
        inputs_common_events["last-input-recorded"] = first_and_last_inputs.second;
        inputs_common_events["first-unpack-first-input"] = first_unpack_first_input;
        inputs_common_events["first-unpack-last-input"] = first_unpack_last_input;
        inputs_common_events["last-pack-last-input"] = last_pack_last_input;
        inputs_common_events["last-pack-first-input"] = last_pack_first_input;
        const tt_op_info& op_info = graph.my_graph_info.op_map.at(op_name);
        string operand_key_str;
        uint8_t rebiased_operand_idx;
        for (const auto& operand_it : core.trisc_operand_to_num_tiles) {
            uint operand_idx = operand_it.first;
            uint tile_size;
            uint num_tiles = operand_it.second;
            uint64_t runtime;
            // If the operand is an input (unpacker) operand
            if (operand_idx < PERF_MAX_NUM_INPUTS) {
                log_assert(operand_idx < op_info.input_data_formats.size(), "operand_idx {} must be smaller than total num inputs {} for op {} graph {}",
                        operand_idx, op_info.input_data_formats.size(), op_name, graph.my_graph_info.name);
                tile_size = get_tile_size(op_info.input_data_formats.at(operand_idx));
                operand_key_str = "input-";
                rebiased_operand_idx = operand_idx;
                runtime = first_unpack_last_input - first_unpack_first_input;
            } 
            else {
                log_assert(operand_idx == PERF_MAX_NUM_INPUTS, "Currently only a single output operand is supported");
                tile_size = get_tile_size(op_info.output_data_format);
                operand_key_str = "output-";
                rebiased_operand_idx = operand_idx - PERF_MAX_NUM_INPUTS;
                runtime = last_pack_last_input - last_pack_first_input;
            }
            inputs_common_events["trisc-num-tiles-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = num_tiles;
            inputs_common_events["trisc-total-tensor-size-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = (num_tiles * tile_size) * (first_and_last_inputs.second - first_and_last_inputs.first + 1);
            if (first_and_last_inputs.second > first_and_last_inputs.first) {
                inputs_common_events["trisc-bw-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = ((num_tiles * tile_size) * (first_and_last_inputs.second - first_and_last_inputs.first)) /
                                                    (runtime / (instr.aiclk / 1000.0));
            }
        }
    } else {
        inputs_common_events["first-input-recorded"] = "N/A";
        inputs_common_events["last-input-recorded"] = "N/A";
        inputs_common_events["first-unpack-first-input"] = "N/A";
        inputs_common_events["first-unpack-last-input"] = "N/A";
        inputs_common_events["last-pack-last-input"] = "N/A";
        inputs_common_events["last-pack-first-input"] = "N/A";
    }
    if (total_runtime_available && !is_op_decoupled(perf_desc, core.descriptor.op_name)) {
        inputs_common_events["total-runtime"] = last_pack_last_input - first_unpack_first_input;
        float average_throughput = float(last_pack_last_input - last_pack_first_input) / (first_and_last_inputs.second - first_and_last_inputs.first);
        inputs_common_events["num-cycles-per-tensor"] = average_throughput;
        float average_throughput_seconds = (1.0/average_throughput) * instr.aiclk * 1000000;
        inputs_common_events["num-tensors-per-second"] = average_throughput_seconds;
    } else {
        inputs_common_events["total-runtime"] = "N/A";
        inputs_common_events["num-cycles-per-tensor"] = "N/A";
        inputs_common_events["num-tensors-per-second"] = "N/A";
    }
    string perf_output_dir = instr.output_dir_path;
    uint32_t model_num_cycles = run_perf_model(perf_model_desc.model_desc, perf_output_dir);
    if (perf_model_desc.valid) {
        if (core.math_activity != ULLONG_MAX && model_num_cycles != 0) {
            float model_math_utilization = float(core.math_activity) / model_num_cycles * 100;
            inputs_common_events["model-math-utilization"] = model_math_utilization;
        } else {
            inputs_common_events["model-math-utilization"] = "N/A";
        }
        if (model_num_cycles != 0) {
            inputs_common_events["model-num-cycles"] = model_num_cycles;
        } else {
            inputs_common_events["model-num-cycles"] = "N/A";
        }
    } else {
        inputs_common_events["model-num-cycles"] = "N/A";
        inputs_common_events["model-math-utilization"] = "N/A";
    }

    if (!inputs_common_events.contains("trisc-num-tiles-operand-input-0")) {
        inputs_common_events["trisc-num-tiles-operand-input-0"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-num-tiles-operand-input-1")) {
        inputs_common_events["trisc-num-tiles-operand-input-1"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-num-tiles-operand-output-0")) {
        inputs_common_events["trisc-num-tiles-operand-output-0"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-total-tensor-size-operand-input-0")) {
        inputs_common_events["trisc-total-tensor-size-operand-input-0"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-total-tensor-size-operand-input-1")) {
        inputs_common_events["trisc-total-tensor-size-operand-input-1"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-total-tensor-size-operand-output-0")) {
        inputs_common_events["trisc-total-tensor-size-operand-output-0"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-bw-operand-input-0")) {
        inputs_common_events["trisc-bw-operand-input-0"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-bw-operand-input-1")) {
        inputs_common_events["trisc-bw-operand-input-1"] = "N/A";
    }
    if (!inputs_common_events.contains("trisc-bw-operand-output-0")) {
        inputs_common_events["trisc-bw-operand-output-0"] = "N/A";
    }

    if (utilization_available && !is_op_decoupled(perf_desc, core.descriptor.op_name)) {
        inputs_common_events["average-math-utilization"] = float(total_math_activity) / (last_pack_last_input - first_unpack_first_input) * 100;
    } else {
        inputs_common_events["average-math-utilization"] = "N/A";
    }
    for (const auto &operand_it: core.brisc_operand_to_num_tiles) {
        uint operand_idx = operand_it.first;
        log_assert(core.brisc_operand_to_pop_tiles_num_cycles.find(operand_idx) != core.brisc_operand_to_pop_tiles_num_cycles.end(),
            "First and last tile pop timestamp not recorded for operand {}", operand_idx);
        uint64_t pop_runtime = core.brisc_operand_to_pop_tiles_num_cycles.at(operand_idx);
        uint64_t num_tiles = operand_it.second;
        const tt_op_info &op_info = graph.my_graph_info.op_map.at(op_name);

        uint tile_size;
        string operand_key_str = "";
        uint rebiased_operand_idx;
        if (operand_idx < PERF_MAX_NUM_INPUTS) {
            log_assert(operand_idx < op_info.input_data_formats.size(), "operand_idx {} must be smaller than total num inputs {} for op {} graph {}",
                    operand_idx, op_info.input_data_formats.size(), op_name, graph.my_graph_info.name);
            tile_size = get_tile_size(op_info.input_data_formats.at(operand_idx));
            operand_key_str = "input-";
            rebiased_operand_idx = operand_idx;
        } else {
            tile_size = get_tile_size(op_info.output_data_format);
            operand_key_str = "output-";
            log_assert(operand_idx == PERF_MAX_NUM_INPUTS, "Currently only a single output operand is supported");
            rebiased_operand_idx = operand_idx - PERF_MAX_NUM_INPUTS;
        }
        inputs_common_events["brisc-num-tiles-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = num_tiles;
        inputs_common_events["brisc-pop-num-cycles-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = pop_runtime;
        inputs_common_events["brisc-bw-operand-" + operand_key_str + to_string(rebiased_operand_idx)] = float(tile_size * num_tiles) / (pop_runtime / (instr.aiclk / 1000.0));
    }
    if (core.packer_num_tiles != 0) {
        log_assert(core.packer_push_runtime != 0, "If packer thread pushes tiles in overlay decoupling mode, the runtime should be also recorded");
        inputs_common_events["packer-overlay-decoupled-num-tiles"] = core.packer_num_tiles;
        inputs_common_events["packer-overlay-decoupled-push-runtime"] = core.packer_push_runtime;
        const tt_op_info &op_info = graph.my_graph_info.op_map.at(op_name);
        uint tile_size = get_tile_size(op_info.output_data_format);
        inputs_common_events["packer-overlay-decoupled-output-bw"] = float(tile_size * core.packer_num_tiles) / (core.packer_push_runtime / (instr.aiclk / 1000.0));
    } else {
        inputs_common_events["packer-overlay-decoupled-output-bw"] = "N/A";
        inputs_common_events["packer-num-tiles"] = "N/A";
        inputs_common_events["packer-push-runtime"] = "N/A";
    }
}

void write_json_report_to_file(const json &report, const string& output_path) {
    std::ofstream output_file(output_path);
    output_file << std::setw(4) << report;
    output_file.flush();
    output_file.close();
}

json create_postprocess_report(
        const vector<core_events> &all_cores,
        const vector<string>& all_core_ids,
        InstructionInfo &instr,
        const PerfDesc &perf_desc,
        const tt_perf_device_alignment &device_alignment_info,
        const tt_digraph &graph,
        const std::unordered_map<string, PostprocessModelDesc> &op_to_perf_model_desc,
        bool align_devices) {
    json core_map;
    int core_idx = 0;
    map<uint, pair<tt_xy_pair, uint64_t>> input_to_last_pack;
    map<uint, pair<tt_xy_pair, uint64_t>> input_to_first_unpack;
    map<uint, pair<string, uint64_t>> input_to_longest_op;
    uint64_t largest_wait_for_q_empty = 0;
    for (const auto &core: all_cores) {
        string core_label = all_core_ids[core_idx];
        json thread_map;
        for (auto &thread: core.threads) {
            string thread_id = thread.first;
            json events_map;
            for (auto &event_id: thread.second.events) {
                string event_description = event_id.second[0].description;
                bool is_single_value_event = false;
                if (thread.second.thread_id < 3) {
                    EventProperties event_properties(event_id.first);
                    is_single_value_event = perf::single_value_events.find(event_properties.event_type) != perf::single_value_events.end();
                }
                if (thread.second.thread_id == 4) {
                    BriscEventProperties event_properties(event_id.first);
                    is_single_value_event = perf::brisc_single_value_events.find(event_properties.event_type) != perf::brisc_single_value_events.end();
                }
                // single event is only supported for trisc currently
                vector<uint64_t> all_first_values, all_second_values, all_diffs, all_single_values;
                vector<float> single_event_type_math_util;
                for (auto &events_with_same_id: event_id.second) {
                    if (is_single_value_event) {
                        if (thread.second.thread_id < 3) {
                            EventProperties event_properties(event_id.first);
                            uint outer_loop = event_properties.outer_loop_idx;
                            if (event_properties.event_type == uint(perf::EventType::NUM_TILES_UNPACK) || event_properties.event_type == uint(perf::EventType::NUM_TILES_PACK)) {
                                all_single_values.push_back(events_with_same_id.first_val);
                            } 
                            else {
                                all_single_values.push_back(get_cycle(device_alignment_info, instr, events_with_same_id.first_val, align_devices));
                            }
                        } else if (thread.second.thread_id == 4) {
                            all_single_values.push_back(events_with_same_id.first_val);
                        }
                    } else {
                        if (thread_id == "T0" || thread_id == "T2" || thread_id == "NCRISC" || thread_id == "BRISC") {
                            all_first_values.push_back(get_cycle(device_alignment_info, instr, events_with_same_id.first_val, align_devices));
                            all_second_values.push_back(get_cycle(device_alignment_info, instr, events_with_same_id.second_val, align_devices));
                            all_diffs.push_back(
                                get_cycle(device_alignment_info, instr, events_with_same_id.second_val, align_devices) - get_cycle(device_alignment_info, instr, events_with_same_id.first_val, align_devices));
                        } else {
                            all_first_values.push_back(events_with_same_id.first_val);
                            all_second_values.push_back(events_with_same_id.second_val);
                            float math_util = float(events_with_same_id.second_val) / events_with_same_id.first_val;
                            single_event_type_math_util.push_back(math_util);
                        }
                    }
                }
                if (is_single_value_event) {
                    events_map[event_description]["value"] = all_single_values;
                } 
                else {
                    string first_val_key;
                    string second_val_key;

                    if (thread_id == "NCRISC" && ((uint)perf::NcriscEventType::DRAM_READ_ISSUED == perf::get_ncrisc_event_type(event_id.first))) {
                        first_val_key   = "chunk-read-issued";
                        second_val_key  = "tiles-flushed";
                    }
                    else if (thread_id == "NCRISC" && (((uint)perf::NcriscEventType::DRAM_IO_Q_STATUS == perf::get_ncrisc_event_type(event_id.first)))) {
                        first_val_key   = "q-available";
                        second_val_key  = "q-empty";
                    }
                    else if (thread_id == "NCRISC" && (((uint)perf::NcriscEventType::STREAM_BUF_STATUS == perf::get_ncrisc_event_type(event_id.first)))) {
                        first_val_key   = "buf-available";
                        second_val_key  = "buf-full";
                    }
                    else if (thread_id == "NCRISC" && (((uint)perf::NcriscEventType::EPOCH_Q_EMPTY == perf::get_ncrisc_event_type(event_id.first)))) {
                        first_val_key   = "q-empty";
                        second_val_key  = "q-available";
                    }
                    else if (thread_id == "NCRISC" && (((uint)perf::NcriscEventType::STREAM_MISC_INFO == perf::get_ncrisc_event_type(event_id.first)))) {
                        first_val_key   = "time";
                        second_val_key  = "data";
                        all_diffs.clear();
                    }
                    else {
                        first_val_key   = (thread_id == "T1") ? "total-period"      : "start";
                        second_val_key  = (thread_id == "T1") ? "math-activity"     : "end";
                    }
                    string third_val_key   = (thread_id == "T1") ? "math-utilization"  : "diff";

                    if (thread_id == "NCRISC" && ((uint)perf::NcriscEventType::STREAM_INFO == perf::get_ncrisc_event_type(event_id.first))) {
                        string fourth_val_key;
                        string fifth_val_key;
                        string sixth_val_key;
                        first_val_key  = "flags";
                        second_val_key = "epoch-iterations-remaining";
                        third_val_key  = "epoch-q-slots-remaining";
                        fourth_val_key = "q-slot-size-tiles";
                        fifth_val_key  = "data-chunk-size-tiles";
                        sixth_val_key  = "data-chunk-size-bytes";
                        events_map[event_description][first_val_key] = event_id.second[0].first_val;
                        events_map[event_description][second_val_key] = event_id.second[0].second_val;
                        events_map[event_description][third_val_key] = event_id.second[0].extra_val[0];
                        events_map[event_description][fourth_val_key] = event_id.second[0].extra_val[1];
                        events_map[event_description][fifth_val_key] = event_id.second[0].extra_val[2];
                        events_map[event_description][sixth_val_key] = event_id.second[0].extra_val[3];
                    }
                    else {
                        events_map[event_description][first_val_key] = all_first_values;
                        events_map[event_description][second_val_key] = all_second_values;
                        if (thread_id == "T1") {
                            events_map[event_description][third_val_key] = single_event_type_math_util;
                        } else {
                            events_map[event_description][third_val_key] = all_diffs;
                        }
                    }
                }
            }
            events_map["out-of-memory"] = thread.second.out_of_memory ? "true" : "false";
            thread_map[thread_id] = events_map;
        }

        json per_thread_events_map;
        
        check_and_populate_per_thread_events(per_thread_events_map, core, device_alignment_info, perf_desc, instr, input_to_longest_op, align_devices);
        
        for (const auto& outer_loop_to_event: core.all_outer_loop_events) {
            
            bool pack_end_exist = outer_loop_to_event.second.pack_last_end != ULLONG_MAX;
            bool unpack_start_exist = outer_loop_to_event.second.unpack_first_instruction != ULLONG_MAX;
            uint input_idx = outer_loop_to_event.first;
            if (pack_end_exist) {
                if (input_to_last_pack.find(input_idx) != input_to_last_pack.end()) {
                    uint64_t last_pack = input_to_last_pack.at(input_idx).second;
                    if (last_pack < get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_last_end, align_devices)) {
                        input_to_last_pack.at(input_idx).second = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_last_end, align_devices);
                        input_to_last_pack.at(input_idx).first = tt_xy_pair(core.core_x, core.core_y);
                    }
                } else {
                    input_to_last_pack.insert({input_idx, {tt_xy_pair(core.core_x, core.core_y), get_cycle(device_alignment_info, instr, outer_loop_to_event.second.pack_last_end, align_devices)}});
                }
            }
            if (unpack_start_exist) {
                if (input_to_first_unpack.find(input_idx) != input_to_first_unpack.end()) {
                    uint64_t first_unpack = input_to_first_unpack.at(input_idx).second;
                    if (first_unpack > get_cycle(device_alignment_info, instr, outer_loop_to_event.second.unpack_first_instruction, align_devices)) {
                        input_to_first_unpack.at(input_idx).second = get_cycle(device_alignment_info, instr, outer_loop_to_event.second.unpack_first_instruction, align_devices);
                        input_to_first_unpack.at(input_idx).first = tt_xy_pair(core.core_x, core.core_y);
                    }
                } else {
                    input_to_first_unpack.insert({input_idx, {tt_xy_pair(core.core_x, core.core_y), get_cycle(device_alignment_info, instr, outer_loop_to_event.second.unpack_first_instruction, align_devices)}});
                }
            }
        }
        
        json inputs_common_events;
        inputs_common_events["op-type"] = core.descriptor.op_type;
        inputs_common_events["out-of-memory"] = core.out_of_memory;
        
        log_assert(op_to_perf_model_desc.find(core.descriptor.op_name) != op_to_perf_model_desc.end(), "Performance model descriptor is not initialized for op {}", core.descriptor.op_name);
        const PostprocessModelDesc &perf_model_desc = op_to_perf_model_desc.at(core.descriptor.op_name);
        calculate_and_populate_average_utilization_and_total_runtime(core, inputs_common_events, device_alignment_info, perf_desc, graph, instr, perf_model_desc, align_devices);
        thread_map["per-thread-events"] = per_thread_events_map;
        thread_map["inputs-common-events"] = inputs_common_events;
        core_map[core_label] = thread_map;
        core_idx++;
    }

    json per_epoch_events_map;
    
    int first_input_pack = -1;
    int last_input_pack = -1;
    for (auto& outer_loop_item: input_to_last_pack) {
        string input_key = "input_" + to_string(outer_loop_item.first);
        per_epoch_events_map["last-pack"][input_key]["end-timestamp"] = outer_loop_item.second.second;
        per_epoch_events_map["last-pack"][input_key]["core-id"] = outer_loop_item.second.first.str();
        if (first_input_pack == -1 || first_input_pack > outer_loop_item.first) {
            first_input_pack = outer_loop_item.first;
        }
        if (last_input_pack == -1 || last_input_pack < outer_loop_item.first) {
            last_input_pack = outer_loop_item.first;
        }
    }
    for (auto& outer_loop_item: input_to_first_unpack) {
        string input_key = "input_" + to_string(outer_loop_item.first);
        per_epoch_events_map["unpack-first-block-available"][input_key]["timestamp"] = outer_loop_item.second.second;
        per_epoch_events_map["unpack-first-block-available"][input_key]["core-id"] = outer_loop_item.second.first.str();
    }
    if (input_to_first_unpack.find(last_input_pack) != input_to_first_unpack.end()) {
        uint64_t last_input_runtime = input_to_last_pack.at(last_input_pack).second - input_to_first_unpack.at(last_input_pack).second;
        json last_input_exec;
        string input_key = "last-input-" + to_string(last_input_pack) + "-execution-time";
        per_epoch_events_map[input_key] = last_input_runtime;
        instr.last_recorded_input_execution_cycle = last_input_runtime;
    }

    if (input_to_last_pack.size() > 0) {
        per_epoch_events_map["last-input-recorded"] = last_input_pack;
        per_epoch_events_map["first-input-recorded"] = first_input_pack;
        instr.last_pack_last_input = input_to_last_pack.at(last_input_pack).second;
        instr.last_pack_first_input = input_to_last_pack.at(first_input_pack).second;
        per_epoch_events_map["last-pack-last-input"] = instr.last_pack_last_input;
        per_epoch_events_map["last-pack-first-input"] = instr.last_pack_first_input;
        if (input_to_first_unpack.find(first_input_pack) != input_to_first_unpack.end()) {
            instr.first_unpack_first_input = input_to_first_unpack.at(first_input_pack).second;
            per_epoch_events_map["first-unpack-first-input"] = instr.first_unpack_first_input;
            per_epoch_events_map["total-runtime"] = instr.last_pack_last_input - instr.first_unpack_first_input;
        } else {
            per_epoch_events_map["first-unpack-first-input"] = "N/A";
            per_epoch_events_map["total-runtime"] = "N/A";
            instr.first_unpack_first_input = ULLONG_MAX;
        }
        instr.first_and_last_inputs_recorded = {first_input_pack, last_input_pack};
        // log_info(tt::LogPerfPostProcess, "Throughput information for epoch {}:", instr.get_epoch_label());
        // std::stringstream ss;
        // ss << std::left << setw(10) << "   input: " << setw(15) << first_input_pack << " last-pack: " << input_to_last_pack.at(first_input_pack).second << " core: " << input_to_last_pack.at(first_input_pack).first.str();
        // log_info(tt::LogPerfPostProcess, "{}", ss.str());
        // ss.str(string());
        // ss.clear();
        // ss << std::left << setw(10) << "   input: " << setw(15) << last_input_pack << " last-pack: " << input_to_last_pack.at(last_input_pack).second << " core: " << input_to_last_pack.at(last_input_pack).first.str();
        // log_info(tt::LogPerfPostProcess, "{}", ss.str());
        // ss.str(string());
        // ss.clear();
        // ss << std::left << setw(10) << "   Average throughput for epoch " << instr.get_epoch_label() << " = " << std::setprecision(5) << average_thoughput;
        // log_info(tt::LogPerfPostProcess, "{}", ss.str());
    } else {
        per_epoch_events_map["first-unpack-first-input"] = "N/A";
        per_epoch_events_map["last-pack-last-input"] = "N/A";
        per_epoch_events_map["last-pack-first-input"] = "N/A";
        per_epoch_events_map["total-runtime"] = "N/A";
        instr.first_unpack_first_input = ULLONG_MAX;
        instr.last_pack_last_input = ULLONG_MAX;
        // log_info(tt::LogPerfPostProcess, "Skipping throughput information for epoch {} because perf is recorded for {} input", instr.get_epoch_label(), input_to_last_pack.size());
    }
    if (input_to_last_pack.size() > 1) {
        log_assert(input_to_last_pack.find(first_input_pack) != input_to_last_pack.end(), "Could not find first input pack event.");
        log_assert(input_to_last_pack.find(last_input_pack) != input_to_last_pack.end(), "Could not find last input pack event");

        float average_thoughput = float(input_to_last_pack.at(last_input_pack).second - input_to_last_pack.at(first_input_pack).second) / (last_input_pack - first_input_pack);
        per_epoch_events_map["num-cycles-per-input"] = average_thoughput;
        instr.num_cycles_per_input = average_thoughput;
        instr.num_inputs_per_second = (1.0/average_thoughput) * instr.aiclk * 1000000;
        per_epoch_events_map["num-inputs-per-second"] = instr.num_inputs_per_second;
    } else {
        per_epoch_events_map["num-cycles-per-input"] = "N/A";
        per_epoch_events_map["num-inputs-per-second"] = "N/A";
    }
    if (perf_desc.measure_steady_state_perf) {
        int quarter_input_idx = instr.input_count / 4;
        int three_q_input_idx = 3 * instr.input_count / 4;
        if (input_to_last_pack.find(quarter_input_idx) != input_to_last_pack.end() &&
                input_to_last_pack.find(three_q_input_idx) != input_to_last_pack.end()) {
            instr.steady_state_first_and_last_inputs = {quarter_input_idx, three_q_input_idx};
            float average_thoughput = float(input_to_last_pack.at(three_q_input_idx).second - input_to_last_pack.at(quarter_input_idx).second) / (three_q_input_idx - quarter_input_idx);
            instr.num_cycles_per_input_steady_state = average_thoughput;
            instr.num_inputs_per_second_steady_state = (1.0/average_thoughput) * instr.aiclk * 1000000;
        }
    }
    json longest_epoch_json;
    for (const auto& longest_op_it: input_to_longest_op) {
        json temp_json;
        temp_json["op-name"] = longest_op_it.second.first;
        temp_json["runtime"] = longest_op_it.second.second;
        longest_epoch_json["input-" + to_string(longest_op_it.first)] = temp_json;
    }
    per_epoch_events_map["longest-op-each-input"] = longest_epoch_json;
    per_epoch_events_map["AICLK"] = instr.aiclk;
    per_epoch_events_map["device-id"] = instr.device_id;
    per_epoch_events_map["epoch-global-id"] = instr.instr_id_global;
    per_epoch_events_map["epoch-local-id"] = instr.instr_id_local;
    per_epoch_events_map["program-id"] = instr.program_id;
    per_epoch_events_map["program-name"] = instr.program_name;
    per_epoch_events_map["graph-name"] = instr.graph_name;
    core_map["per-epoch-events"] = per_epoch_events_map;
    return core_map;
}

json create_runtime_report(const json &all_events) {
    json runtime_table;
    for (auto &core_label: all_events.items()) {
        auto core_label_value = core_label.value();
        if (core_label.key() == "per-epoch-events") {
            runtime_table[core_label.key()] = core_label_value;
        } else {
            for (auto &input: core_label_value["per-thread-events"].items()) {
                runtime_table[core_label.key()][input.key()]["math-activity"] = input.value().at("math-activity");
                runtime_table[core_label.key()][input.key()]["pack-runtime"] = input.value().at("pack-runtime");
                runtime_table[core_label.key()][input.key()]["math-utilization-first-unpack-to-last-pack"] = input.value().at("math-utilization-first-unpack-to-last-pack");
                runtime_table[core_label.key()][input.key()]["first-unpack-to-last-pack"] = input.value().at("first-unpack-to-last-pack");
                runtime_table[core_label.key()][input.key()]["first-unpack-to-last-pack-without-wait-for-tile"] = input.value().at("first-unpack-to-last-pack-without-wait-tile");
                runtime_table[core_label.key()][input.key()]["total-unpack-wait-for-tile-after-first-unpack"] = input.value().at("total-unpack-wait-for-tile-after-first-unpack");
                runtime_table[core_label.key()][input.key()]["total-wait-for-free-tile-after-first-unpack"] = input.value().at("total-wait-for-free-tile-after-first-unpack");
                runtime_table[core_label.key()][input.key()]["unpacker-stalled-on-ncrisc-intermediate-dump"] = input.value().at("unpacker-stalled-on-ncrisc-intermediate-dump");
                runtime_table[core_label.key()][input.key()]["packer-stalled-on-ncrisc-intermediate-dump"] = input.value().at("packer-stalled-on-ncrisc-intermediate-dump");
            }
            runtime_table[core_label.key()]["average-math-utilization"] = core_label_value["inputs-common-events"]["average-math-utilization"];
            runtime_table[core_label.key()]["first-unpack-first-input"] = core_label_value["inputs-common-events"]["first-unpack-first-input"];
            runtime_table[core_label.key()]["first-unpack-last-input"] = core_label_value["inputs-common-events"]["first-unpack-last-input"];
            runtime_table[core_label.key()]["last-pack-last-input"] = core_label_value["inputs-common-events"]["last-pack-last-input"];
            runtime_table[core_label.key()]["last-pack-first-input"] = core_label_value["inputs-common-events"]["last-pack-first-input"];
            runtime_table[core_label.key()]["total-runtime"] = core_label_value["inputs-common-events"]["total-runtime"];
            runtime_table[core_label.key()]["first-input-recorded"] = core_label_value["inputs-common-events"]["first-input-recorded"];
            runtime_table[core_label.key()]["last-input-recorded"] = core_label_value["inputs-common-events"]["last-input-recorded"];
            runtime_table[core_label.key()]["out-of-memory"] = core_label_value["inputs-common-events"]["out-of-memory"];
            

            for (uint operand_idx = 0; operand_idx < PERF_MAX_NUM_INPUTS; operand_idx++) {
                if (core_label_value["inputs-common-events"].contains("trisc-num-tiles-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-num-tiles-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-num-tiles-operand-input-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("trisc-total-tensor-size-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-total-tensor-size-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-total-tensor-size-operand-input-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("trisc-bw-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-bw-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-bw-operand-input-" + to_string(operand_idx)];
                } 
            }

            for (uint operand_idx = 0; operand_idx < PERF_MAX_NUM_OUTPUTS; operand_idx++) {
                if (core_label_value["inputs-common-events"].contains("trisc-num-tiles-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-num-tiles-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-num-tiles-operand-output-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("trisc-total-tensor-size-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-total-tensor-size-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-total-tensor-size-operand-output-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("trisc-bw-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["trisc-bw-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["trisc-bw-operand-output-" + to_string(operand_idx)];
                }     
            }

            runtime_table[core_label.key()]["num-cycles-per-tensor"] = core_label_value["inputs-common-events"]["num-cycles-per-tensor"];
            runtime_table[core_label.key()]["num-tensors-per-second"] = core_label_value["inputs-common-events"]["num-tensors-per-second"];

            if (core_label_value["inputs-common-events"].contains("packer-push-runtime")) {
                runtime_table[core_label.key()]["packer-push-runtime"] = core_label_value["inputs-common-events"]["packer-push-runtime"];
            }
            if (core_label_value["inputs-common-events"].contains("packer-overlay-decoupled-output-bw")) {
                runtime_table[core_label.key()]["packer-overlay-decoupled-output-bw"] = core_label_value["inputs-common-events"]["packer-overlay-decoupled-output-bw"];
            }
            if (core_label_value["inputs-common-events"].contains("packer-num-tiles")) {
                runtime_table[core_label.key()]["packer-num-tiles"] = core_label_value["inputs-common-events"]["packer-num-tiles"];
            }
            
            for (uint operand_idx = 0; operand_idx < PERF_MAX_NUM_INPUTS; operand_idx++) {
                if (core_label_value["inputs-common-events"].contains("brisc-num-tiles-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-num-tiles-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-num-tiles-operand-input-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("brisc-pop-num-cycles-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-pop-num-cycles-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-pop-num-cycles-operand-input-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("brisc-bw-operand-input-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-bw-operand-input-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-bw-operand-input-" + to_string(operand_idx)];
                }
            }
            for (uint operand_idx = 0; operand_idx < PERF_MAX_NUM_OUTPUTS; operand_idx++) {
                if (core_label_value["inputs-common-events"].contains("brisc-num-tiles-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-num-tiles-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-num-tiles-operand-output-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("brisc-pop-num-cycles-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-pop-num-cycles-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-pop-num-cycles-operand-output-" + to_string(operand_idx)];
                }
                if (core_label_value["inputs-common-events"].contains("brisc-bw-operand-output-" + to_string(operand_idx))) {
                    runtime_table[core_label.key()]["brisc-bw-operand-output-" + to_string(operand_idx)] = core_label_value["inputs-common-events"]["brisc-bw-operand-output-" + to_string(operand_idx)];
                }
            }
            runtime_table[core_label.key()]["model-math-utilization"] = core_label_value["inputs-common-events"]["model-math-utilization"];
            runtime_table[core_label.key()]["model-num-cycles"] = core_label_value["inputs-common-events"]["model-num-cycles"];
        }
    }
    return runtime_table;
}

std::string get_float_str_with_precision(const string &str, const int num_decimals = 2)
{
    // If the number was undefined return
    if (str == "N/A") {
        return str;
    }
    // If the number did not have any decimal points return
    if (str.find('.') == string::npos) {
        return str;
    }
    const float &num = std::stof(str);
    std::ostringstream out;
    out << std::fixed << std::setprecision(num_decimals) << num;
    return out.str();
}

void create_runtime_table(const json &runtime_report, const string& output_dir) {

    table::PrettyTable table;
    table.horizontal_line_row = 1;
    table.vertical_line_col = 1;
    table.add_row({
        "core-label",
        "device_id",
        "aiclk",
        "first-and-last-inputs",
        "total-runtime",
        "active-math-cycles-single-input",
        "math-utilization-over-all-inputs",
        "math-utilization-last-input-recorded",
        "runtime-last-input-recorded",
        "num-cycles-per-tensor",
        "num-tensors-per-second",
        "input0-bw",
        "input1-bw",
        "output-bw",
    });

    log_assert(runtime_report.contains("per-epoch-events"), "The runtime report must always contain the per-epoch-events section");
    for (auto &core_label: runtime_report.items()) {
    
        auto core_label_value = core_label.value();
        if (core_label.key() == "per-epoch-events") {
            continue;
        }
        log_assert(core_label_value.contains("last-input-recorded"), "last-input-recorded must always exist under inputs-common-events section of the runtime report");
        int last_input_recorded;
        if (core_label_value.at("last-input-recorded") == "N/A") {
            last_input_recorded = -1;
        } else {
            last_input_recorded = core_label_value.at("last-input-recorded").get<uint>();
        }
        string last_input_label = (last_input_recorded == -1) ? "N/A" : "input-" + to_string(last_input_recorded);
        if (last_input_label != "N/A") {
            log_assert(core_label_value.contains(last_input_label), "Input label {} must exist under runtime report for core label {}", last_input_label, core_label.key());
        }
        
        table.add_row({
            core_label.key(),
            runtime_report.at("per-epoch-events").at("device-id").dump(),
            runtime_report.at("per-epoch-events").at("AICLK").dump(),
            core_label_value.at("first-input-recorded").dump() + " -> " + core_label_value.at("last-input-recorded").dump(),
            core_label_value.at("total-runtime").dump(),
            last_input_label != "N/A" ? core_label_value.at(last_input_label).at("math-activity").dump() : "N/A",
            get_float_str_with_precision(core_label_value.at("average-math-utilization").dump()),
            last_input_label != "N/A" ? get_float_str_with_precision(core_label_value.at(last_input_label).at("math-utilization-first-unpack-to-last-pack").dump()) : "N/A",
            last_input_label != "N/A" ? core_label_value.at(last_input_label).at("first-unpack-to-last-pack").dump() : "N/A",
            get_float_str_with_precision(core_label_value.at("num-cycles-per-tensor").dump()),
            get_float_str_with_precision(core_label_value.at("num-tensors-per-second").dump()),
            get_float_str_with_precision(core_label_value.at("trisc-bw-operand-input-0").dump()),
            get_float_str_with_precision(core_label_value.at("trisc-bw-operand-input-1").dump()),
            get_float_str_with_precision(core_label_value.at("trisc-bw-operand-output-0").dump()),
        });
    }
    log_debug(tt::LogPerfInfra, "Creating runtime table csv under {}", output_dir + runtime_table_csv_name);
    std::ofstream output_file(output_dir + runtime_table_csv_name);
    output_file << table.generate_table_string(table::PrettyTable::Format::CSV);

    log_debug(tt::LogPerfInfra, "Creating runtime table log under {}", output_dir + runtime_table_log_name);
    std::ofstream output_file_log(output_dir + runtime_table_log_name);
    output_file_log << table.generate_table_string();
}

tuple<string, string> parse_op_key(std::string const &str){
    size_t start;
    size_t end = 0;
    int collected_tokens = 0;
    vector<string> out;
    while ((start = str.find_first_not_of("-", end)) != std::string::npos and collected_tokens <= 2){
        end = str.find("-", start);
        out.push_back(str.substr(start, end - start));
        collected_tokens++;
    }
    log_assert(out.size() == 3, "Invalid OP Key in Runtime table");
    return {out[0] + "-" + out[1], out[2]};
}

void create_op_report(const json &runtime_table, const string &output_dir, const perf::PerfDesc &perf_desc){
    string core_loc, op_name;
    json op_to_core_report;
    unordered_map<string, uint64_t> op_to_first_unpack_first_input;
    unordered_map<string, uint64_t> op_to_first_unpack_last_input;
    unordered_map<string, uint64_t> op_to_last_pack_first_input;
    unordered_map<string, uint64_t> op_to_last_pack_last_input;
    unordered_map<string, uint64_t> op_to_num_cores;
    unordered_map<string, uint64_t> op_to_math_activity;
    vector<unordered_map<string, uint64_t>> op_to_input_total_tensor_size(PERF_MAX_NUM_INPUTS);
    unordered_map<string, uint64_t> op_to_output_total_tensor_size;
    int32_t aiclk = -1;

    vector<string> ops_with_invalid_pack_or_unpack = {};
    for(auto &op: runtime_table.items()){
        auto op_value = op.value();
        if (op.key() != "per-epoch-events") {
            tie(core_loc, op_name) = parse_op_key(op.key());
            if (op_to_core_report.find(op_name) == op_to_core_report.end()) {
                if (!(op_value.at("first-unpack-first-input") == "N/A" or op_value.at("last-pack-first-input") == "N/A" or op_value.at("first-unpack-last-input") == "N/A" or op_value.at("last-pack-last-input") == "N/A")) {
                    op_to_first_unpack_first_input[op_name] = op_value.at("first-unpack-first-input").get<uint64_t>();
                    op_to_last_pack_first_input[op_name] = op_value.at("last-pack-first-input").get<uint64_t>();
                    op_to_first_unpack_last_input[op_name] = op_value.at("first-unpack-last-input").get<uint64_t>();
                    op_to_last_pack_last_input[op_name] = op_value.at("last-pack-last-input").get<uint64_t>();
                } else {
                    op_to_last_pack_first_input[op_name] = ULONG_MAX;
                    op_to_first_unpack_first_input[op_name] = ULONG_MAX;
                    op_to_last_pack_last_input[op_name] = ULONG_MAX;
                    op_to_first_unpack_last_input[op_name] = ULONG_MAX;
                }
                for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_INPUTS; operand_idx++) {
                    if (op_value.contains("trisc-total-tensor-size-operand-input-" + to_string(operand_idx)) && op_value.at("trisc-total-tensor-size-operand-input-" + to_string(operand_idx)) != "N/A") {
                        if (op_to_input_total_tensor_size[operand_idx].find(op_name) == op_to_input_total_tensor_size[operand_idx].end()) {
                            op_to_input_total_tensor_size[operand_idx][op_name] = op_value.at("trisc-total-tensor-size-operand-input-" + to_string(operand_idx)).get<uint64_t>();
                        } else {
                            log_assert(op_value.at("trisc-total-tensor-size-operand-input-" + to_string(operand_idx)).get<uint64_t>() == op_to_input_total_tensor_size[operand_idx].at(op_name), "The tiles size for each entry should match between all cores running the same op");
                        }   
                    }
                }
                for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_OUTPUTS; operand_idx++) {
                    if (op_value.contains("trisc-total-tensor-size-operand-output-" + to_string(operand_idx)) && op_value.at("trisc-total-tensor-size-operand-output-" + to_string(operand_idx)) != "N/A") {
                        if (op_to_output_total_tensor_size.find(op_name) == op_to_output_total_tensor_size.end()) {
                            op_to_output_total_tensor_size[op_name] = op_value.at("trisc-total-tensor-size-operand-output-" + to_string(operand_idx)).get<uint64_t>();
                        } else {
                            log_assert(op_value.at("trisc-total-tensor-size-operand-output-" + to_string(operand_idx)).get<uint64_t>() == op_to_output_total_tensor_size.at(op_name), "The tiles size for each entry should match between all cores running the same op");
                        }   
                    }
                }
                op_to_num_cores[op_name] = 0;
                op_to_math_activity[op_name] = 0;
            } else {
                if (op_value.at("first-unpack-first-input") == "N/A" or op_value.at("last-pack-first-input") == "N/A" or op_value.at("first-unpack-last-input") == "N/A" or op_value.at("last-pack-last-input") == "N/A") {
                    op_to_first_unpack_first_input.at(op_name) = ULONG_MAX;
                    op_to_last_pack_first_input.at(op_name) = ULONG_MAX;
                    op_to_first_unpack_last_input.at(op_name) = ULONG_MAX;
                    op_to_last_pack_last_input.at(op_name) = ULONG_MAX;
                } else {
                    if(op_to_first_unpack_first_input.at(op_name) > op_value.at("first-unpack-first-input").get<uint64_t>()){
                        op_to_first_unpack_first_input.at(op_name) = op_value.at("first-unpack-first-input").get<uint64_t>();
                    }
                    if(op_to_first_unpack_last_input.at(op_name) > op_value.at("first-unpack-last-input").get<uint64_t>()){
                        op_to_first_unpack_last_input.at(op_name) = op_value.at("first-unpack-last-input").get<uint64_t>();
                    }
                    if(op_to_last_pack_first_input.at(op_name) < op_value.at("last-pack-first-input").get<uint64_t>()){
                        op_to_last_pack_first_input.at(op_name) = op_value.at("last-pack-first-input").get<uint64_t>();
                    }
                    if(op_to_last_pack_last_input.at(op_name) < op_value.at("last-pack-last-input").get<uint64_t>()){
                        op_to_last_pack_last_input.at(op_name) = op_value.at("last-pack-last-input").get<uint64_t>();
                    }
                }
            }
            op_to_core_report[op_name][core_loc]["first-input-recorded"] = op_value.at("first-input-recorded");
            op_to_core_report[op_name][core_loc]["last-input-recorded"] = op_value.at("last-input-recorded");
            op_to_core_report[op_name][core_loc]["math-utilization-across-inputs"] = op_value.at("average-math-utilization");
            op_to_core_report[op_name][core_loc]["runtime-across-inputs"] = op_value.at("total-runtime");
            for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_INPUTS; operand_idx++) {
                if (op_value.contains("trisc-bw-operand-input-" + to_string(operand_idx))) {
                    op_to_core_report[op_name][core_loc]["trisc-bw-operand-input-" + to_string(operand_idx)] = op_value.at("trisc-bw-operand-input-" + to_string(operand_idx));
                }
            }
            for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_OUTPUTS; operand_idx++) {
                if (op_value.contains("trisc-bw-operand-output-" + to_string(operand_idx))) {
                    op_to_core_report[op_name][core_loc]["trisc-bw-operand-output-" + to_string(operand_idx)] = op_value.at("trisc-bw-operand-output-" + to_string(operand_idx));
                }  
            }
            if(op_value.at("first-input-recorded") != "N/A" and op_value.at("last-input-recorded") != "N/A" and op_to_math_activity.at(op_name) != ULONG_MAX){
                stringstream first_input_info;
                first_input_info << "input-" << op_value.at("first-input-recorded");
                string first_recorded_input_str = first_input_info.str();
                log_assert(op_value.find(first_recorded_input_str) != op_value.end(), "{} was recorded but not present in the runtime table for op {}", first_recorded_input_str, op.key());
                log_assert(op_value.at(first_recorded_input_str).find("math-activity") != op_value.at(first_recorded_input_str).end(), "Math activity for the first recorded input ({}) is not present for op {}", first_recorded_input_str, op.key());

                if(op_value.at(first_recorded_input_str).at("math-activity") != "N/A"){
                    op_to_math_activity.at(op_name) = op_value.at(first_recorded_input_str).at("math-activity").get<uint64_t>() * (op_value.at("last-input-recorded").get<uint64_t>() - op_value.at("first-input-recorded").get<uint64_t>() + 1);
                }
                else{
                    op_to_math_activity.at(op_name) = ULONG_MAX;
                }
            }
            else{
                op_to_math_activity.at(op_name) = ULONG_MAX;
            }
            op_to_num_cores.at(op_name) ++;
        }
        else{
            op_to_core_report["epoch-information"]["epoch-global-id"] = op_value["epoch-global-id"];
            aiclk = op_value["AICLK"].get<int32_t>();
            op_to_core_report["epoch-information"]["AICLK"] = aiclk;
        }
    }
    log_assert(aiclk != -1, "Invalid value for aiclk");
    for (const auto &op: op_to_core_report.items()) {
        if (op.key() != "epoch-information") {
            if(op_to_last_pack_last_input.at(op.key()) != ULONG_MAX and op_to_first_unpack_first_input.at(op.key()) != ULONG_MAX){
                op_to_core_report.at(op.key())["runtime-across-cores"] = op_to_last_pack_last_input.at(op.key()) - op_to_first_unpack_first_input.at(op.key());
            }
            else{
                op_to_core_report.at(op.key())["runtime-across-cores"] = "N/A";
            }
            if(op_to_math_activity.at(op.key()) != ULONG_MAX and op_to_last_pack_last_input.at(op.key()) != ULONG_MAX and op_to_first_unpack_first_input.at(op.key()) != ULONG_MAX){
                op_to_core_report.at(op.key())["math-utilization-across-cores"] = 100 * op_to_math_activity.at(op.key())/(op_to_core_report.at(op.key()).at("runtime-across-cores").get<float>());
            }
            else{
                op_to_core_report.at(op.key())["math-utilization-across-cores"] = "N/A";
            }
            op_to_core_report.at(op.key())["num-cores"] = op_to_num_cores[op.key()];
            if (op_to_first_unpack_first_input.find(op.key()) != op_to_first_unpack_first_input.end() &&
                op_to_last_pack_last_input.find(op.key()) != op_to_last_pack_last_input.end() &&
                !is_op_decoupled(perf_desc, op.key())) {
                if (op_to_first_unpack_first_input.at(op.key()) != ULONG_MAX && op_to_last_pack_last_input.at(op.key()) != ULLONG_MAX)  {
                    op_to_core_report.at(op.key())["first-unpack-first-input"] = op_to_first_unpack_first_input.at(op.key());
                    op_to_core_report.at(op.key())["last-pack-last-input"] = op_to_last_pack_last_input.at(op.key());
                    for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_INPUTS; operand_idx++) {
                        if (op_to_input_total_tensor_size[operand_idx].find(op.key()) != op_to_input_total_tensor_size[operand_idx].end()) {
                            op_to_input_total_tensor_size[operand_idx].at(op.key()) *= op_to_num_cores[op.key()];
                            op_to_core_report.at(op.key())["trisc-total-tensor-size-all-cores-operand-input-" + to_string(operand_idx)] = op_to_input_total_tensor_size[operand_idx].at(op.key());
                            op_to_core_report.at(op.key())["trisc-bw-total-runtime-operand-input-" + to_string(operand_idx)] = op_to_input_total_tensor_size[operand_idx].at(op.key()) /
                                                                        ((op_to_last_pack_last_input.at(op.key()) - op_to_first_unpack_first_input.at(op.key())) / (aiclk / 1000.0));
                        }
                    }
                }
                if (op_to_output_total_tensor_size.find(op.key()) != op_to_output_total_tensor_size.end()) {
                    op_to_output_total_tensor_size.at(op.key()) *= op_to_num_cores[op.key()];
                    for (uint8_t operand_idx = 0; operand_idx < PERF_MAX_NUM_OUTPUTS; operand_idx++) {
                        op_to_core_report.at(op.key())["trisc-total-tensor-size-all-cores-operand-output-" + to_string(operand_idx)] = op_to_output_total_tensor_size.at(op.key());
                        op_to_core_report.at(op.key())["trisc-bw-total-runtime-operand-output-" + to_string(operand_idx)] = op_to_output_total_tensor_size.at(op.key()) /
                                                                    ((op_to_last_pack_last_input.at(op.key()) - op_to_first_unpack_first_input.at(op.key())) / (aiclk / 1000.0));
                    }
                }
            }
        }
        if (!op_to_core_report.at(op.key()).contains("first-unpack-first-input")) {
            op_to_core_report.at(op.key())["first-unpack-first-input"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("first-unpack-last-input")) {
            op_to_core_report.at(op.key())["first-unpack-last-input"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("last-pack-first-input")) {
            op_to_core_report.at(op.key())["last-pack-first-input"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("last-pack-last-input")) {
            op_to_core_report.at(op.key())["last-pack-last-input"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("trisc-bw-total-runtime-operand-input-0")) {
            op_to_core_report.at(op.key())["trisc-bw-total-runtime-operand-input-0"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("trisc-bw-total-runtime-operand-input-1")) {
            op_to_core_report.at(op.key())["trisc-bw-total-runtime-operand-input-1"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("trisc-bw-total-runtime-operand-output-0")) {
            op_to_core_report.at(op.key())["trisc-bw-total-runtime-operand-output-0"] = "N/A";
        }

        if (!op_to_core_report.at(op.key()).contains("trisc-total-tensor-size-all-cores-operand-input-0")) {
            op_to_core_report.at(op.key())["trisc-total-tensor-size-all-cores-operand-input-0"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("trisc-total-tensor-size-all-cores-operand-input-1")) {
            op_to_core_report.at(op.key())["trisc-total-tensor-size-all-cores-operand-input-1"] = "N/A";
        }
        if (!op_to_core_report.at(op.key()).contains("trisc-total-tensor-size-all-cores-operand-output-0")) {
            op_to_core_report.at(op.key())["trisc-total-tensor-size-all-cores-operand-output-0"] = "N/A";
        }
    }
    
    ofstream output_op_table(output_dir + op_table_json_name);
    output_op_table << std::setw(4) << op_to_core_report;
    output_op_table.flush();
    output_op_table.close();
}

json create_host_postprocess_report(
    const thread_events &current_thread,
    host_global_events global_events,
    const string &postprocess_report_path,
    const int &total_input_count,
    bool device_alignment_report) {

    const uint thread_id = perf::get_thread_id();
    json events_map;
    for (auto &event_id: current_thread.events) {
        string event_description = event_id.second[0].description;
        HostEventProperties event_properties(event_id.first);
        bool is_single_value = perf::host_single_value_events.find(event_properties.event_type) != perf::host_single_value_events.end();
        vector<uint64_t> all_first_values, all_second_values, all_diffs, all_single_values;
        for (auto &events_with_same_id: event_id.second) {
            if (!is_single_value) {
                all_first_values.push_back(events_with_same_id.first_val);
                all_second_values.push_back(events_with_same_id.second_val);
                all_diffs.push_back(events_with_same_id.second_val - events_with_same_id.first_val);
            } else {
                all_single_values.push_back(events_with_same_id.first_val);
            }
        }
        if (!is_single_value) {
            string first_val_key = "start";
            string end_val_key = "end";
            string third_val_key = "diff";
            events_map[event_description][first_val_key] = all_first_values;
            events_map[event_description][end_val_key] = all_second_values;
            events_map[event_description][third_val_key] = all_diffs;
        } else {
            string first_val_key = "value";
            events_map[event_description][first_val_key] = all_single_values;
        }
    }
    json global_events_map;
    if (global_events.first_program_start != ULLONG_MAX) {
        global_events_map["first-program-start"] = global_events.first_program_start;
    }
    if (global_events.last_output_pop_end != ULLONG_MAX) {
        global_events_map["last-output-pop-end"] = global_events.last_output_pop_end;
    }
    if (total_input_count != -1) {
        global_events_map["total-input-count"] = total_input_count;
    }
    if (global_events.first_program_start != ULLONG_MAX &&
        global_events.last_output_pop_end != ULLONG_MAX) {
        global_events_map["first-program-to-last-output-pop"] = global_events.last_output_pop_end - global_events.first_program_start;
        if (total_input_count != -1) {
            global_events_map["samples-per-second"] = total_input_count * 1000000000.0 / float(global_events.last_output_pop_end - global_events.first_program_start);
        }
    }
    if (!global_events_map.contains("first-program-start")) {
        global_events_map["first-program-start"] = "N/A";
    }
    if (!global_events_map.contains("last-output-pop-end")) {
        global_events_map["last-output-pop-end"] = "N/A";
    }
    if (!global_events_map.contains("total-input-count")) {
        global_events_map["total-input-count"] = "N/A";
    }
    if (!global_events_map.contains("first-program-to-last-output-pop")) {
        global_events_map["first-program-to-last-output-pop"] = "N/A";
    }
    if (!global_events_map.contains("samples-per-second")) {
        global_events_map["samples-per-second"] = 0;
    }
    events_map["global-events"] = global_events_map;
    log_info(tt::LogPerfPostProcess, "Writing the host postprocess report in {}", postprocess_report_path);
    ofstream report_file(postprocess_report_path);
    report_file << std::setw(4) << events_map;
    report_file.flush();
    report_file.close();
    return events_map;
}

void create_host_summary_postprocess_report(const string &host_summary_json_path, const json& host_report) {
    const uint thread_id = perf::get_thread_id();
    
    json summary_report;
    if (fs::exists(host_summary_json_path)) {
        ifstream ifs(host_summary_json_path);
        json report_existing = json::parse(ifs);
        summary_report = report_existing;
    }
    if (host_report.contains("global-events") && host_report.at("global-events").contains("samples-per-second")) {
        if (host_report.at("global-events").at("samples-per-second").get<uint64_t>() != 0) {
            summary_report["samples-per-second"] = host_report.at("global-events").at("samples-per-second");
        }
    }
    if (!summary_report.contains("samples-per-second")) {
        summary_report["samples-per-second"] = 0;
    }

    ofstream summamry_file(host_summary_json_path);
    summamry_file << std::setw(4) << summary_report;
    summamry_file.flush();
    summamry_file.close();
}

void create_summary_report(const InstructionInfo &instr, const string& perf_out_dir, const json& all_events, const json& runtime_table, const PerfDesc &perf_desc) {
    string report_dir = perf_out_dir + "/../";
    string report_path = report_dir + "perf_mode_report_" + instr.get_epoch_label() +".json";
    bool report_file_exists = fs::exists(report_path);
    ofstream report_file;
    json report;
    string current_mode = get_decouple_mode_name(perf_desc);
    if (report_file_exists) {
        ifstream ifs(report_path);
        json report_existing = json::parse(ifs);
        report = report_existing;
    }
    report_file.open(report_path);
    for (auto &core_label: all_events.items()) {
        auto core_label_value = core_label.value();
        // Not all core_labels are cores anymore.
        if (core_label.key() == "per-epoch-events") {
            report[core_label.key()][current_mode]["AICLK"] = core_label_value.at("AICLK");
            continue;
        }
        if (core_label.value().contains("inputs-common-events")) {
            report[core_label.key()]["inputs-common-events"][current_mode]["op-type"] = core_label_value.at("inputs-common-events").at("op-type");
        }
        if (core_label_value.contains("per-thread-events")) {
            for (auto &input: core_label_value["per-thread-events"].items()) {
                report[core_label.key()][input.key()][current_mode]["pack-runtime"]         = input.value().at("pack-runtime");
                report[core_label.key()][input.key()][current_mode]["math-utilization-first-unpack-to-last-pack"]     = input.value().at("math-utilization-first-unpack-to-last-pack");

                report[core_label.key()][input.key()][current_mode]["first-unpack-to-last-pack"] = input.value().at("first-unpack-to-last-pack");
                report[core_label.key()][input.key()][current_mode]["first-unpack-to-last-pack-without-wait-tile"] = input.value().at("first-unpack-to-last-pack-without-wait-tile");

                // Some metrics come from runtime_table.
                // // int model_cycles_per_core, model_prop_cycles_per_core = 0;
                // if (runtime_table.contains(core_label.key()) && runtime_table[core_label.key()].contains(input.key())){
                //     if (runtime_table[core_label.key()][input.key()].contains("model-cycles-per-core")){
                //         auto model_cycles_per_core = runtime_table.at(core_label.key()).at(input.key()).at("model-cycles-per-core");
                //         report[core_label.key()][input.key()][current_mode]["model-cycles-per-core"] = model_cycles_per_core;
                //     }
                //     if (runtime_table[core_label.key()][input.key()].contains("model-prop-cycles-per-core")){
                //         auto model_prop_cycles_per_core = runtime_table.at(core_label.key()).at(input.key()).at("model-prop-cycles-per-core");
                //         report[core_label.key()][input.key()][current_mode]["model-prop-cycles-per-core"] = model_prop_cycles_per_core;
                //     }
                // }

                // report[core_label.key()][input.key()][current_mode]["model-cycles-per-core"] = model_cycles_per_core;
                // report[core_label.key()][input.key()][current_mode]["model-prop-cycles-per-core"] = model_prop_cycles_per_core;
            }
        }
    }
    report_file << std::setw(4) << report;
    report_file.flush();
    report_file.close();
}

void generate_summary_reports(const vector<InstructionInfo> &all_instructions_info, const string& perf_out_dir, const PerfDesc &perf_desc) {
    log_info(tt::LogPerfPostProcess, "Generating the summary reports...");
    for (const InstructionInfo &instr: all_instructions_info) {
        log_assert(fs::exists(instr.output_dir_path + postprocess_json_name), "Postprocess json does not exist");
        log_assert(fs::exists(instr.output_dir_path + "/runtime_table.json"), "runtime_table.json does not exist");
        ifstream ifs_pp_json(instr.output_dir_path + postprocess_json_name);
        ifstream ifs_runtime_json(instr.output_dir_path + "/runtime_table.json");
        json all_events = json::parse(ifs_pp_json);
        json runtime_table = json::parse(ifs_runtime_json);
        log_debug(tt::LogPerfPostProcess, "Generating the summary report for instruction output dir {}", instr.output_dir_path);
        create_summary_report(instr, perf_out_dir, all_events, runtime_table, perf_desc);
    }
}

void create_all_epochs_perf_info_report(const vector<tt::EpochPerfInfo> &all_epochs_info, const int &input_count, const string& output_path) {
    YAML::Node report;
    float total_runtime_excluding_last_epoch = 0.0;
    int largest_program_id = 0;
    bool report_exists = fs::exists(output_path);
    std::ofstream output_file(output_path, std::ios::app);
    if (report_exists) {
        output_file << "\n";
    }
    for (const auto& epoch_info: all_epochs_info) {
        // YAML::Node program_node;
        if (epoch_info.program_id > largest_program_id) {
            largest_program_id = epoch_info.program_id;
        }
        YAML::Node graph_node;
        string program_name = to_string(epoch_info.program_id) + "-" + epoch_info.program_name;
        string graph_name = to_string(epoch_info.graph_id) + "-" + epoch_info.graph_name;
        graph_node["aiclk"] = epoch_info.aiclk;
        graph_node["device_id"] = epoch_info.device_id;
        graph_node["global_epoch_id"] = epoch_info.global_epoch_id;
        graph_node["output_directory"] = epoch_info.perf_output_directory;
        string num_cycles_per_input_str = (epoch_info.num_cycles_per_input != 0) ? std::to_string(epoch_info.num_cycles_per_input) : "N/A";
        string num_inputs_per_second_str = (epoch_info.num_inputs_per_second != 0) ? std::to_string(epoch_info.num_inputs_per_second) : "N/A";
        graph_node["Number of cycles per input  (Skipping first input)"] = num_cycles_per_input_str;
        graph_node["Number of inputs per second (Skipping first input)"] = num_inputs_per_second_str;
        graph_node["Number of cycles (First unpack to last pack) for last input"] = epoch_info.last_recorded_input_execution_cycle;
        graph_node["Largest epoch binary queue empty among all cores"] = epoch_info.largest_wait_for_epoch_binary_cycles;
        report[program_name][graph_name] = graph_node;
        if (epoch_info.first_and_last_inputs_recorded.first != 0 ||
            epoch_info.first_and_last_inputs_recorded.second != epoch_info.input_count-1 ||
            epoch_info.first_unpack_first_input == ULLONG_MAX ||
            epoch_info.last_pack_last_input == ULLONG_MAX) {
                total_runtime_excluding_last_epoch = ULLONG_MAX;
        }
        if (total_runtime_excluding_last_epoch != ULLONG_MAX && epoch_info.graph_id < epoch_info.num_epochs_current_program - 1) {
            total_runtime_excluding_last_epoch += (epoch_info.last_pack_last_input - epoch_info.first_unpack_first_input) / (epoch_info.aiclk * 1000000.0);
        }
    }
    if (!report_exists) {
        int num_programs = largest_program_id + 1;
        if (total_runtime_excluding_last_epoch != ULLONG_MAX && total_runtime_excluding_last_epoch != 0.0 && input_count != -1 && num_programs == 1) {
            report["global-events"]["samples-per-second-excluding-last-epoch-of-each-program"] = input_count / total_runtime_excluding_last_epoch;
        } else {
            report["global-events"]["samples-per-second-excluding-last-epoch-of-each-program"] = 0;
        }
        char* host_name;
        int MAX_HOST_LEN = 100;
        host_name = new char [MAX_HOST_LEN];
        gethostname(host_name, MAX_HOST_LEN);
        string host_name_str(host_name);
        string host_name_envvar = "";
        if (std::getenv("BOARD_HOSTNAME")) {
            host_name_envvar = std::getenv("BOARD_HOSTNAME");
        }
        report["global-events"]["host-name"] = host_name_str;
        report["global-events"]["host-name-envvar"] = host_name_envvar;
        delete [] host_name;
    }
    output_file << report;
}

void gen_vcd_from_perf_dump(const vector<InstructionInfo> &all_instructions, const string& perf_output_dir, const PerfDesc &perf_desc) {

    if (all_instructions.empty()) {
        perf_warning(perf_desc, tt::LogPerfPostProcess, "Skipping ncrisc vcd dump since no instructions were given to the generator");
        return;
    }
    log_info(tt::LogPerfPostProcess, "Generating ncrisc vcd");

    stringstream vcd_cmd;
    string log_file =  perf_output_dir + "gen_vcd.log";
    vcd_cmd << " python3 "<< get_perf_lib_directory() << "perf_to_vcd.py";
    vcd_cmd << " " << all_instructions.at(0).aiclk;
    vcd_cmd << " " << perf_output_dir << "perf_postprocess.vcd";

    for (const auto &instr: all_instructions) {
        log_assert(instr.aiclk == all_instructions.at(0).aiclk, "In ncrisc vcd generator, all epochs must have run with the same aiclk");
        vcd_cmd << " " << instr.output_dir_path + postprocess_json_name;
    }

    log_info(tt::LogPerfPostProcess, "Generate vcd command {}", vcd_cmd.str());
    if (!tt::run_command(vcd_cmd.str(), log_file)) {
        log_fatal("Failed to generate ncrisc vcd file from perf dump");
    }
}

// Returns a map of str(physical_core_x - physical_core_y) -> op_name
unordered_map<string, core_descriptor> generate_graph_descriptor_map(
    const string report_output_dir,
    const tt_graph_info &graph_info,
    const std::unordered_map<string, tt_queue_wrap> &queues,
    const buda_SocDescriptor &soc_descriptor,
    const std::unordered_map<string, unordered_set<string>> &op_to_outputs) {
    
    log_assert(report_output_dir != "", "Output directory cannot be empty");
    log_assert(fs::exists(report_output_dir), "Output directory for graph descriptor does not exist {}", report_output_dir);
    const string queue_label = "queue";
    const string op_label = "op";
    
    ofstream output_file(report_output_dir + cores_to_ops_json_name);
    json all_cores_config;
    unordered_map<string, core_descriptor> core_to_op;
    // Postprocessor currently only works for a single epoch.
    unordered_set<string> all_op_names;

    for (auto &op_it: graph_info.op_map) {
        string op_name = op_it.first;
        if (all_op_names.find(op_name) == all_op_names.end()) {
            all_op_names.insert(op_name);
        }
    }

    core_to_op = populate_cores_to_ops_for_graph(graph_info, soc_descriptor);
    all_cores_config = get_core_config_json(core_to_op);

    // ---------------------------------------------
    // Record the input and output nodes for all ops
    for (const auto& core_to_config: all_cores_config.items()) {
        string op_name = core_to_config.value().at("op-name");
        if (op_name == "Invalid") {
            continue;
        }
        
        log_assert(graph_info.op_map.find(op_name) != graph_info.op_map.end(), "op_map of graph_info with graph_name {} does not contain op {}", graph_info.name, op_name);
        log_assert(op_to_outputs.find(op_name) != op_to_outputs.end(), "op_to_outputs map is not initialized for op name {}", op_name);
        const unordered_set<string> &actual_output_nodes = op_to_outputs.at(op_name);
        vector<string> input_vec = graph_info.op_map.at(op_name).input_names;

        vector<json> inputs;
        for (uint idx = 0; idx < input_vec.size(); idx++) {
            json single_input;
            string input_name = input_vec.at(idx);
            string input_type;
            if (queues.find(input_name) == queues.end()) {
                input_type = op_label;
            } else {
                input_type = queue_label;
            }
            single_input["name"] = input_name;
            single_input["type"] = input_type;
            single_input["index"] = idx;
            inputs.push_back(single_input);
        }
        all_cores_config.at(core_to_config.key())["inputs"] = inputs;
        all_cores_config.at(core_to_config.key())["num-inputs"] = input_vec.size();

        vector<json> outputs;
        for (const auto &output_name: actual_output_nodes) {
            json single_output;
            string output_type;
            if (queues.find(output_name) == queues.end()) {
                output_type = op_label;
            } else {
                output_type = queue_label;
            }
            single_output["name"] = output_name;
            single_output["type"] = output_type;
            outputs.push_back(single_output);
        }
        all_cores_config.at(core_to_config.key())["outputs"] = outputs;
        all_cores_config.at(core_to_config.key())["num-outputs"] = actual_output_nodes.size();
    }

    if (all_cores_config.empty()) {
        all_cores_config["N/A"] = "Empty Graph";
    }

    output_file << std::setw(4) << all_cores_config;
    output_file.flush();
    output_file.close();
    return core_to_op;
}

void generate_queue_descriptor(
    const string &report_output_dir,
    const std::map<string, tt_queue_wrap> &queues) {

    log_assert(report_output_dir != "", "Output directory cannot be empty");
    log_assert(fs::exists(report_output_dir), "Output directory for queue descriptor does not exist {}", report_output_dir);
    ofstream output_file(report_output_dir + queue_descriptor_json_name);
    json all_queues;

    for (const auto &it: queues) {
        json queue_json;
        const string &queue_name = it.first;
        const tt_queue_info &queue_info = it.second.my_queue_info;
        queue_json["input"] = queue_info.input;
        queue_json["device-id"] = queue_info.target_device;
        queue_json["entries"] = queue_info.entries;
        queue_json["grid-size"] = queue_info.grid_size.to_vec();
        stringstream df;
        df << queue_info.data_format;
        queue_json["data-format"] = df.str();
        queue_json["tile-dim"] = tt::get_string(queue_info.tile_dim);
        queue_json["type"] = tt::get_string(queue_info.type);
        stringstream loc;
        loc << queue_info.loc;
        queue_json["location"] = loc.str();
        vector<json> alloc_info;
        for (const auto info: queue_info.alloc_info) {
            json info_json;
            info_json["channel"] = info.channel;
            info_json["subchannel"] = -1;
            info_json["address"] = info.address;
            alloc_info.push_back(info_json);
        }
        queue_json["allocation-info"] = alloc_info;
        queue_json["source-device-id"] = queue_info.src_device_id;
        stringstream tilized;
        tilized << queue_info.layout;
        queue_json["layout"] = tilized.str();
        queue_json["alias"] = queue_info.alias;
        stringstream block_dim;
        block_dim << queue_info.dim;
        queue_json["block-dim"] = block_dim.str();
        all_queues[queue_name] = queue_json;
    }

    output_file << std::setw(4) << all_queues;
    output_file.flush();
    output_file.close();
}

void generate_metadata_reports(
    const string &report_output_dir,
    const string &graph_name, 
    const buda_SocDescriptor &sdesc,
    const tt_digraph &graph) {
    
    const string arch_name_key = "arch_name";
    json metadata_json;
    const string graph_json_file_dir = report_output_dir + "/" + graph_name + ".json";
    ofstream graph_metadata_file(graph_json_file_dir);

    /* add arch name to metadata for graph */
    uint target_device = graph.my_graph_info.target_device;
    string arch_str = get_arch_str(sdesc.arch);
    metadata_json[arch_name_key] = arch_str;

    /* write metadata to graph metdata json*/
    graph_metadata_file << std::setw(4) << metadata_json;
    graph_metadata_file.close();

}

}
