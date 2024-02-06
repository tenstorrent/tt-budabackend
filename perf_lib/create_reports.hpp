// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "postprocess.hpp"

namespace postprocess {

void write_json_report_to_file(const json &report, const string& output_path);
json create_postprocess_report(
    const vector<core_events> &all_cores,
    const vector<string>& all_core_ids,
    InstructionInfo &instr,
    const PerfDesc &perf_desc,
    const tt_perf_device_alignment &device_alignment_info,
    const tt_digraph &graph,
    const std::unordered_map<string, PostprocessModelDesc> &op_to_perf_model_desc,
    bool align_devices);
json create_runtime_report(const json &all_events);
void create_runtime_table(const json &runtime_report, const string& output_dir);
void generate_summary_reports(const vector<InstructionInfo> &all_instructions_info, const string& perf_out_dir, const perf::PerfDesc &perf_desc);
void gen_vcd_from_perf_dump(const vector<InstructionInfo> &all_instructions, const string& perf_output_dir, const perf::PerfDesc &perf_desc);
void create_all_epochs_perf_info_report(const vector<tt::EpochPerfInfo> &all_epochs_info, const int &input_count, const string& output_path);
json create_host_postprocess_report(
    const thread_events &current_thread,
    host_global_events global_events,
    const string &postprocess_report_path,
    const int &total_input_count,
    bool device_alignment_report);
void create_host_summary_postprocess_report(const string &host_summary_json_path, const json& host_report);
void create_op_report(const json &runtime_table, const string &output_dir, const perf::PerfDesc &perf_desc);
// Returns a map of str(physical_core_x - physical_core_y) -> op_name
unordered_map<string, core_descriptor> generate_graph_descriptor_map(
    const string report_output_dir,
    const tt_graph_info &graph_info,
    const std::unordered_map<string, tt_queue_wrap> &queues,
    const buda_SocDescriptor &soc_descriptor,
    const std::unordered_map<string, unordered_set<string>> &op_to_outputs);
void generate_queue_descriptor(
    const string &report_output_dir,
    const std::map<string, tt_queue_wrap> &queues);
void generate_metadata_reports(
    const string &report_output_dir,
    const string &graph_name, 
    const buda_SocDescriptor &sdesc,
    const tt_digraph &graph);
}