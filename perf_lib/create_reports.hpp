// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include "perf_base.hpp"
#include "common/buda_soc_descriptor.h"
#include "third_party/json/json.hpp"
#include "netlist/tt_digraph.hpp"

namespace perf {
    struct PerfDesc;
}

namespace postprocess {

struct PerfState;
using json = nlohmann::json;

void write_json_report_to_file(const json &report, const std::string& output_path);
json create_postprocess_report(
    const std::vector<perf::core_events> &all_cores,
    const std::vector<std::string>& all_core_ids,
    perf::InstructionInfo &instr,
    const perf::PerfDesc &perf_desc,
    const perf::tt_perf_device_alignment &device_alignment_info,
    const tt_digraph &graph,
    const std::unordered_map<std::string, perf::PostprocessModelDesc> &op_to_perf_model_desc,
    bool align_devices,
    bool versim
);
json create_runtime_report(const json &all_events);
void create_runtime_table(const json &runtime_report, const std::string& output_dir);
void generate_summary_reports(const std::vector<perf::InstructionInfo> &all_instructions_info, const std::string& perf_out_dir, const perf::PerfDesc &perf_desc);
void gen_vcd_from_perf_dump(const std::vector<perf::InstructionInfo> &all_instructions, const std::string& perf_output_dir, const perf::PerfDesc &perf_desc);
void create_all_epochs_perf_info_report(const std::vector<tt::EpochPerfInfo> &all_epochs_info, const int &input_count, const std::string& output_path);
json create_host_postprocess_report(
    const perf::thread_events &current_thread,
    perf::host_global_events global_events,
    const std::string &postprocess_report_path,
    const int &total_input_count,
    bool device_alignment_report);
void create_host_summary_postprocess_report(const std::string &host_summary_json_path, const json& host_report);
void create_op_report(const json &runtime_table, const std::string &output_dir, const perf::PerfDesc &perf_desc);
// Returns a map of str(physical_core_x - physical_core_y) -> op_name
unordered_map<std::string, perf::core_descriptor> generate_graph_descriptor_map(
    const std::string report_output_dir,
    const tt_graph_info &graph_info,
    const std::unordered_map<std::string, tt_queue_wrap> &queues,
    const buda_SocDescriptor &soc_descriptor,
    const std::unordered_map<std::string, std::unordered_set<std::string>> &op_to_outputs);
void generate_queue_descriptor(
    const std::string &report_output_dir,
    const std::map<std::string, tt_queue_wrap> &queues);
void generate_metadata_reports(
    const std::string &report_output_dir,
    const std::string &graph_name, 
    const buda_SocDescriptor &sdesc,
    const tt_digraph &graph);
void print_perf_info_all_epochs(const string &output_dir, const PerfState &perf_state, bool print_to_file);
}