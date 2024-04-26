// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <experimental/filesystem> // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>

#include "model/model.hpp"
#include "postprocess.hpp"

namespace postprocess {

inline string epoch_type_str(EpochType epoch_type) {
    switch (epoch_type) {
        case EpochType::Forward: return "Forward";
        case EpochType::Backward: return "Backward";
        case EpochType::Optimizer: return "Optimizer";
        case EpochType::Control: return "Control";
        default: log_assert(false ,  "unrecognized enum type for EpochType");
    }
    return "Unknown";
}

uint32_t get_tile_size(tt::DataFormat data_format);
bool is_normal_perf_mode(const perf::PerfDesc& perf_desc);
string get_decouple_mode_name(const perf::PerfDesc& perf_Desc);
string get_overlay_decouple_string(const perf::PerfDesc &perf_desc);
string get_perf_lib_directory();
void skip_lines(ifstream &input_file, int max_num_lines);
uint read_hex_number(ifstream &input_file);
uint convert_string_line_to_hex(const string& input_number_line_str);
uint64_t convert_string_line_to_64b_val(const string& input_number_line_str);
void set_core_id(core_events &current_core, const string& core_id_str);
uint32_t get_num_events_to_skip_between_threads(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const perf::PerfDesc &perf_desc, int thread_id);
uint32_t get_num_events_to_skip_between_workers(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const perf::PerfDesc &perf_desc);
uint32_t get_num_events_per_threads_in_dram(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const perf::PerfDesc &perf_desc, int thread_id);
bool is_core_id(string current_line);
bool is_thread_id(string current_line);
uint find_beginning_of_next_l1_dump(uint thread_event_idx, uint num_events_per_thread, const perf::PerfDesc& perf_desc, int thread_id);
std::set<EpochType> get_unique_epoch_types(vector<EpochType> epoch_types_executed);
map<tt_xy_pair, vector<int>> get_cores_to_instr_idx_from_model(const vector<InstructionInfo> &all_instructions, int target_device_id, const buda_SocDescriptor* soc_descriptor);
void calculate_first_to_last_outer_loop_cycles(core_events &current_core);
void calculate_brisc_bw_info(core_events &current_core);
void set_unpack_pack_num_tiles(core_events& current_core);
void check_end_time_recorded(core_events &current_core, const perf::PerfDesc &perf_desc);
void check_end_time_recorded_host(thread_events &current_thread);
void combine_ncrisc_events(thread_events &thread_events);
void process_thread_end(
    thread_events &current_thread,
    core_events &current_core,
    vector<core_events> &all_core_events,
    vector<string> & all_core_ids,
    vector<uint32_t> &current_thread_events,
    string core_id_str,
    const perf::PerfDesc& perf_desc,
    bool &skip_to_next_core,
    bool &skip_to_next_thread,
    bool modify_skip_flags,
    bool out_of_memory);

void process_thread_main(vector<uint32_t> &current_thread_events, thread_events &current_thread, bool concurrent);
void process_new_thread(thread_events& current_thread, string current_line, bool &skip_to_next_thread);
void process_new_core(core_events &current_core, string core_id_str, const unordered_map<string, core_descriptor> &cores_to_ops, ofstream &output_log, bool &skip_to_next_core, bool &skip_to_next_thread);
pair<uint64_t, uint64_t> get_earliest_start_and_lastest_end(const json& all_events);
pair<uint64_t, uint64_t> get_epoch_q_empty_largest_delay(const json& all_events);
pair<int, int> get_first_and_last_recorded_input_idx(const core_events& core);
host_global_events populate_host_global_events(const thread_events& thread);
unordered_map<string, core_descriptor> populate_cores_to_ops_for_graph(const tt_graph_info &graph_info, const buda_SocDescriptor &soc_descriptor);
json get_core_config_json(const unordered_map<string, core_descriptor> &core_to_op);
uint32_t run_perf_model(const tt_op_model_desc& op_desc, string perf_output_dir);

}
