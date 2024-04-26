// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "utils.hpp"
#include "create_reports.hpp"
#include "third_party/json/json.hpp"
#include "common/model/tt_core.hpp"
#include "netlist_utils.hpp"
#include "model/data_format.hpp"
#include "op_model/op_model.hpp"

using namespace perf;
namespace postprocess {

// Same functionality as GET_L1_TILE_SIZE in ckernel_defs.h
uint32_t get_tile_size(tt::DataFormat data_format) {
    log_assert(data_format != tt::DataFormat::Invalid, "invalid data format");

    uint32_t tile_size = 0;
    uint32_t header_and_padding_size = 32;

    if (data_format == DataFormat::Float32 ||
        data_format == DataFormat::Int32 ||
        data_format == DataFormat::RawUInt32) {
        tile_size = 32 * 32 * 4 + 32;
    
    } else if (data_format == DataFormat::Float16 ||
            data_format == DataFormat::Float16_b ||
            data_format == DataFormat::UInt16 ||
            data_format == DataFormat::RawUInt16) {    
        tile_size = 32 * 32 * 2 + 32;
    
    } else if (data_format == DataFormat::Bfp8 ||
            data_format == DataFormat::Bfp8_b) {
        tile_size = 32 * 32 * 1 + 32 + 4*16;
    
    } else if (data_format == DataFormat::Bfp4 ||
            data_format == DataFormat::Bfp4_b) {
        tile_size = 32 * 32 / 2 + 32 + 4*16;
    
    } else if (data_format == DataFormat::Bfp2 ||
            data_format == DataFormat::Bfp2_b) {
        tile_size = 32 * 32 / 4 + 32 + 4*16;
    } else if ((data_format == DataFormat::Lf8) ||
               (data_format == DataFormat::Fp8_e4m3) ||
               (data_format == DataFormat::Int8) ||
               (data_format == DataFormat::UInt8)) {
        tile_size = 32 * 32 + 32;
    
    } else {
        log_assert(false, "Tile size for data-format does not exist. Please add the tile size to this list");
    }
    return tile_size;
}

bool is_normal_perf_mode(const PerfDesc &perf_desc) {
    if (!perf_desc.triplet_modes.empty()) {
        return false;
    }
    for (const auto& op_name_to_decouple: perf_desc.initial_trisc_decouplings) {
        for (const auto& decouple: op_name_to_decouple.second) {
            if (decouple != PerfTriscDecoupleMode::None) {
                return false;
            }
        }
    }
    return true;
}

string get_decouple_mode_name(const PerfDesc &perf_desc) {
    string decouple_str;
    uint op_id = 0;
    if (is_normal_perf_mode(perf_desc)) {
        decouple_str += "device";
    }
    else {
        for (const auto &op_name_to_config: perf_desc.initial_trisc_decouplings) {
            // If the op decoupling is in mode "None" skip it in generating the name for this mode.
            if (op_name_to_config.second.find(PerfTriscDecoupleMode::None) != op_name_to_config.second.end()) {
                continue;
            }
            if (op_id > 0) {
                decouple_str += "\n";
            }
            decouple_str += "- decoupling_op_" + op_name_to_config.first;
            uint decouple_id = 0;
            for (const auto& config: op_name_to_config.second) {
                decouple_str +=  "_" + PerfTriscDecoupleModetoString(config);
                decouple_id++;
            }
            op_id++;
        }
        int triplet_idx = 0;
        for (const auto& triplet: perf_desc.triplet_modes) {
            if (!decouple_str.empty() || triplet_idx > 0) {
                decouple_str += "\n";
            }
            decouple_str += "- triplet_" + triplet;
            triplet_idx++;
        }
    }
    return decouple_str;
}

string get_overlay_decouple_string(const PerfDesc &perf_desc) {
    std::stringstream ss;
    for(const auto &[op_name, overlay_decouple_config] : perf_desc.overlay_decouplings) {
        ss << op_name << ": ";
        ss << overlay_decouple_config << "\n";
    }
    return ss.str();
}

string get_perf_lib_directory() {
    string root;
    if (getenv("BUDA_HOME")) {
        root = getenv("BUDA_HOME");
    } else {
        root = "./";
    }
    string perf_lib_dir = root + "perf_lib/";
    return perf_lib_dir;
}

void skip_lines(ifstream &input_file, int max_num_lines) {
    for (int i = 0; i < max_num_lines; i++) {
        string str;
        std::getline(input_file, str);
    }
}

uint read_hex_number(ifstream &input_file) {
    string event_line;
    char bullet;
    uint event_val;
    if (!std::getline(input_file, event_line)) { log_error("Invalid line read from input file"); exit(-1); }
    std::istringstream iss(event_line);
    iss >> bullet >> std::hex >> event_val;
    return event_val;
}

// Extracts the number in "[KEYWORD]: NUMBER"
uint64_t convert_string_line_to_64b_val(const string& input_number_line_str) {
    uint64_t event_val;
    string identifier;
    std::istringstream iss(input_number_line_str);
    iss >> identifier >> event_val;
    return event_val;
}

// Converts "- 0x..." to an unsigned integer.
uint convert_string_line_to_hex(const string& input_number_line_str) {
    char bullet;
    uint event_val;
    std::istringstream iss(input_number_line_str);
    iss >> bullet >> std::hex >> event_val;
    return event_val;
}

void set_core_id(core_events &current_core, const string& core_id_str) {
    auto delimiter_pos = core_id_str.find('-');
    log_assert (delimiter_pos != string::npos, "Could not find '-' in core descriptor");
    current_core.core_x = std::stoi(core_id_str.substr(0, delimiter_pos));
    int core_y_dim_num_chars = core_id_str.size() - 1 - delimiter_pos;
    current_core.core_y = std::stoi(core_id_str.substr(delimiter_pos + 1, core_y_dim_num_chars));
}

// Buffer size for each worker is 32B aligned
// The buffer space for every worker allocated in dram
uint32_t get_dram_perf_buf_inc(const buda_SocDescriptor *soc_descriptor, tt_xy_pair dram_core) {

    std::unordered_map<tt_xy_pair, vector<tt_xy_pair>> dram_core_to_workers = soc_descriptor->get_perf_dram_bank_to_workers();

    log_assert(dram_core_to_workers.find(dram_core) != dram_core_to_workers.end(), "DRAM core not found");

    int num_workers_current_dram_core = dram_core_to_workers[dram_core].size();
    int num_threads = thread_names.size();
    //perf_buf_inc should be 32-byte aligned.
    uint32_t perf_buf_inc = num_workers_current_dram_core ? (dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_SIZE/num_workers_current_dram_core) & 0xFFFFFFE0 : 0;
    return perf_buf_inc;

}

// Returns the number of times ncrisc will copy l1-buffers to dram for a single thread of a single core.
// Ncrisc dumps buffers in chunks of 2KB.
// Therefore, the total space allocated to each worker core should be a multiple of number_of_threads * half_l1_buffer_size.
uint32_t get_num_max_trisc_dram_reqs(const buda_SocDescriptor *soc_descriptor, tt_xy_pair dram_core, const PerfDesc& perf_desc) {
    uint32_t total_buf_size = 0;
    for (uint thread = 0; thread < l1_mem::address_map::PERF_NUM_THREADS; thread++) {
        total_buf_size += perf_desc.get_perf_buf_size(thread) / 2;
    }
    uint32_t perf_buf_max_req = get_dram_perf_buf_inc(soc_descriptor, dram_core) / total_buf_size;
    // Always have space for two consecutive dumps
    perf_buf_max_req -= (perf_buf_max_req % 2);
    log_assert(perf_buf_max_req >= 2, "We should always have enough space for at least two dumps");
    log_assert(perf_buf_max_req >= 2, "We should at least have space for 2 request to dump perf buffer");
    return perf_buf_max_req;
}

// Buffer size for each thread is 32B aligned
// perf_buf_size must already be 32B aligned
uint32_t get_dram_thread_inc(const buda_SocDescriptor *soc_descriptor, tt_xy_pair dram_core, const PerfDesc& perf_desc, int thread_id) {
    uint32_t perf_buf_size = perf_desc.get_perf_buf_size(thread_id)/2;
    log_assert(perf_buf_size % 32 == 0, "Perf buffer size {} must be 32B aligned", perf_buf_size);
    uint32_t max_reqs = get_num_max_trisc_dram_reqs(soc_descriptor, dram_core, perf_desc);
    return perf_buf_size * max_reqs;
}

uint32_t get_l1_perf_buf_size_each_dump(const PerfDesc& perf_desc, int thread_id) {
    if (perf_desc.device_perf_mode == PerfDumpMode::IntermediateDump) {
        return perf_desc.get_perf_buf_size(thread_id)/2;
    } else {
        return perf_desc.get_perf_buf_size(thread_id);
    }
}

uint32_t get_num_events_to_skip_between_threads(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const PerfDesc &perf_desc, int thread_id) {

    // Perf buffer base and addresses must be aligned. So there should not be any paddings between threads
    return 0;
    // uint32_t perf_buf_max_req = get_num_max_trisc_dram_reqs(soc_descriptor, dram_core, perf_desc);
    // uint32_t thread_actual_dump_bytes = perf_buf_max_req * (perf_desc.get_perf_buf_size(thread_id)/2);
    // uint32_t thread_inc = get_dram_thread_inc(soc_descriptor, dram_core, perf_desc, thread_id);
    // log_assert(thread_inc >= thread_actual_dump_bytes);
    // log_assert((thread_inc - thread_actual_dump_bytes) % NumBytesPerEvent == 0);
    // return ((thread_inc - thread_actual_dump_bytes) / NumBytesPerEvent);
}

uint32_t get_num_events_to_skip_between_workers(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const PerfDesc &perf_desc) {

    uint32_t thread_inc_all = 0;
    for (uint thread_id = 0; thread_id < l1_mem::address_map::PERF_NUM_THREADS; thread_id++) {
        thread_inc_all += get_dram_thread_inc(soc_descriptor, dram_core, perf_desc, thread_id);
    }
    uint32_t perf_buf_inc = get_dram_perf_buf_inc(soc_descriptor, dram_core);

    log_assert(perf_buf_inc >= thread_inc_all, "Expected perf_buf_inc >= thread_inc_all");
    log_assert((perf_buf_inc - thread_inc_all) % NumBytesPerEvent == 0, "Expected difference between buf_inc and thread_inc to be NumBytesPerEvent aligned");
    return ((perf_buf_inc - thread_inc_all) / NumBytesPerEvent);
}

uint32_t get_num_events_per_threads_in_dram(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core, const PerfDesc &perf_desc, int thread_id) {

    uint32_t total_space_per_thread = get_dram_thread_inc(soc_descriptor, dram_core, perf_desc, thread_id);
    int total_num_events_per_thread = total_space_per_thread / NumBytesPerEvent;
    return total_num_events_per_thread;
}

// In the current yaml from versim/silicon, only the core-id's have both '-' and ':' character
// TODO: Properly detect #-# sequence to verify this is a core-id
bool is_core_id(string current_line) {
    bool dash_exists = current_line.find('-') != string::npos;
    bool colon_exists = current_line.find(':') != string::npos;
    return dash_exists && colon_exists;
}

bool is_thread_id(string current_line) {
    current_line.pop_back(); // Last character is a ':'
    for (const auto& thread_name: thread_names) {
        if (current_line.find(thread_name) != string::npos) {
            return true;
        }
    }
    return false;
}

// Each l1 dump into dram is l1_mem::address_map::TRISC_PERF_BUF_SIZE / 2.
// When PerfValLast is seen before an event aligned to this value, to go to next epoch,
// we need to skip to the beginning of next dump.
// Following function returns the smallest event_idx aligned to l1-dump that is larger than thread_event_idx.
uint find_beginning_of_next_l1_dump(uint thread_event_idx, uint num_events_per_thread, const PerfDesc& perf_desc, int thread_id) {
    uint num_events_each_dump = (get_l1_perf_buf_size_each_dump(perf_desc, thread_id)) / NumBytesPerEvent;
    log_assert(num_events_per_thread % num_events_each_dump == 0, "Unexpected num_events_per_thread");
    log_assert(thread_event_idx < num_events_per_thread, "thread_event_idx is out of bounds");
    int smallest_aligned_event = 0;
    while (smallest_aligned_event <= num_events_per_thread) {
        if (smallest_aligned_event >= thread_event_idx) {
            break;
        } else {
            smallest_aligned_event += num_events_each_dump;
        }
    }
    log_assert (smallest_aligned_event <= num_events_per_thread && smallest_aligned_event >= num_events_each_dump, "Incorrect value for smallest_aligned_event");
    return smallest_aligned_event;
}

unordered_map<string, core_descriptor> populate_cores_to_ops_for_graph(const tt_graph_info &graph_info, const buda_SocDescriptor &soc_descriptor) {
    unordered_map<string, core_descriptor> core_to_op;
    log_debug(tt::LogPerfInfra, "Generating the cores to ops for graph {}", graph_info.name);

    for (auto &op_it: graph_info.op_map) {
        string op_name = op_it.first;
        const tt_op_info &op_info = op_it.second;
        log_assert(op_name == op_info.name, "op_name {} inside the graph_info does not match the op_name {} inside op_info.", op_name, op_info.name);
        if (netlist_utils::is_non_tensix_op(op_info.type)) {
            continue;
        }
        int grid_shape_x = op_info.grid_size_x();
        int grid_shape_y = op_info.grid_size_y();
        int grid_loc_x = op_info.grid_loc_x();
        int grid_loc_y = op_info.grid_loc_y();
        for (int x = 0; x < grid_shape_x; x++) {
            for (int y = 0; y < grid_shape_y; y++) {
                
                int logical_x = grid_loc_x + x;
                int logical_y = grid_loc_y + y;

                int physical_x = soc_descriptor.worker_log_to_routing_x.at(logical_x);
                int physical_y = soc_descriptor.worker_log_to_routing_y.at(logical_y);
                
                tt_xy_pair physical_core(physical_x, physical_y);
                tt_xy_pair logical_core(logical_x, logical_y);

                core_descriptor core_desc(op_name, op_info.type, physical_core, logical_core);
                core_to_op.insert({core_desc.get_core_str(CoordType::Physical), core_desc});
            }
        }
    }
    return core_to_op;
}

json get_core_config_json(const unordered_map<string, core_descriptor> &core_to_op) {
    json all_cores_config;
    for (const auto &core_it: core_to_op) {
        json single_core_config;
        single_core_config["op-name"] = core_it.second.op_name;
        single_core_config["op-type"] = core_it.second.op_type;
        single_core_config["logical-core-id"] = core_it.second.get_core_str(CoordType::Logical, false);
        all_cores_config[core_it.second.get_core_str(CoordType::Physical)] = single_core_config;
    }
    return all_cores_config;
}

std::set<EpochType> get_unique_epoch_types(vector<EpochType> epoch_types_executed) {
    std::set<EpochType> unique_epoch_tpes;
    for (EpochType epoch_type: epoch_types_executed) {
        if (unique_epoch_tpes.find(epoch_type) == unique_epoch_tpes.end()) {
            unique_epoch_tpes.insert(epoch_type);
        }
    }
    return unique_epoch_tpes;
}

map<tt_xy_pair, vector<int>> get_cores_to_instr_idx_from_model(const vector<InstructionInfo> &all_instructions, int target_device_id, const buda_SocDescriptor* soc_descriptor) {
    map<tt_xy_pair, vector<int>> core_to_instr_idx;
    int instr_idx = 0;
    for (InstructionInfo instr: all_instructions) {
        string program_name = instr.program_name;
        string graph_name = instr.graph_name;
        tt_graph_info graph_info = instr.netlist_graph.my_graph_info;

        if (target_device_id != instr.device_id) {
            continue;
        }

        for (auto &op_it: graph_info.op_map) {
            string op_name = op_it.first;
            std::shared_ptr<tt_op> op = op_it.second.my_op;
            log_assert(op != nullptr, "Expected op to be populated");
            if (netlist_utils::is_non_tensix_op(op->type)) {
                continue;
            }
            int grid_shape_y = op->grid_shape.at(0);
            int grid_shape_x = op->grid_shape.at(1);
            for (int x = 0; x < grid_shape_x; x++) {
                for (int y = 0; y < grid_shape_y; y++) {
                    int core_y_id = op->cores.at(y).at(x)->get_logical_absolute_row_id();
                    int core_x_id = op->cores.at(y).at(x)->get_logical_absolute_col_id();
                    int physical_core_y;
                    int physical_core_x;
                    if (soc_descriptor == nullptr) {
                        log_error("soc-descriptor not initialized before dumping cores_to_ops for perf dump post-process");
                        int physical_core_y = -1;
                        int physical_core_x = -1;
                    } else {
                        physical_core_y = soc_descriptor->worker_log_to_routing_y.at(core_y_id);
                        physical_core_x = soc_descriptor->worker_log_to_routing_x.at(core_x_id);
                        tt_xy_pair core_coord = tt_xy_pair(physical_core_x, physical_core_y);
                        if (core_to_instr_idx.find(core_coord) != core_to_instr_idx.end()) {
                            core_to_instr_idx.at(core_coord).push_back(instr_idx);
                        } else {
                            core_to_instr_idx.insert({core_coord, {instr_idx}});
                        }
                    }
                }
            }
        }
        instr_idx++;
    }
    log_debug(tt::LogPerfPostProcess, "The map of each physical core to vector of instruction ids for which they are active:");
    log_debug(tt::LogPerfPostProcess, "Device id {}", target_device_id);
    for (const auto& core_to_instr_idx_iter: core_to_instr_idx) {
        log_debug(tt::LogPerfPostProcess, "{} = [", core_to_instr_idx_iter.first.str());
        for (int i = 0; i < core_to_instr_idx_iter.second.size(); i++) {
            log_debug(tt::LogPerfPostProcess, "    {}, ", core_to_instr_idx_iter.second.at(i));
        }
        log_debug(tt::LogPerfPostProcess, "]");
    }
    return core_to_instr_idx;
}

// This function must be called after calculate_first_to_last_outer_loop_cycles.
void set_number_of_inputs_recorded(core_events &current_core) {

    int num_inputs = 0;

    
    int num_unpack_first_block = 0;
    int num_unpack_last_block = 0;
    
    int num_pack_start = 0;
    int num_pack_end = 0;

    for (const auto& outer_loop_to_event: current_core.all_outer_loop_events) {
        if (outer_loop_to_event.second.unpack_first_instruction                     != ULLONG_MAX) { num_unpack_first_block++   ; }
        
        if (outer_loop_to_event.second.pack_first_start                             != ULLONG_MAX) { num_pack_start++       ; }
        if (outer_loop_to_event.second.pack_last_end                                != ULLONG_MAX) { num_pack_end++         ; }
    }
    
    
    int num_unpacker_inputs = num_unpack_first_block;

    string error_str = "Failed for core " + to_string(current_core.core_x) + "-" + to_string(current_core.core_y) + " ";
    current_core.threads.at("T0").num_inputs_recorded = num_unpacker_inputs;

    int num_pack_inputs = num_pack_start;
    current_core.threads.at("T2").num_inputs_recorded = num_pack_inputs;

    if (!current_core.threads.at("T2").out_of_memory) {
        log_assert(num_pack_inputs == num_pack_end, "Failed for core {} Number of pack end events {} must be equal to number of inputs {} if there is no out of memory errors",
            tt_xy_pair(current_core.core_x, current_core.core_y).str(), num_pack_end, num_pack_inputs);
    }

    if (!current_core.threads.at("T0").out_of_memory && !current_core.threads.at("T2").out_of_memory) {
        log_assert(num_unpacker_inputs == num_pack_inputs, "Failed for core {} Number of inputs recorded for unpacker {} and packer {} must be the same if there is no out of memory errors.",
            tt_xy_pair(current_core.core_x, current_core.core_y).str(), num_unpacker_inputs, num_pack_inputs);
    }
}

void calculate_brisc_bw_info(core_events &current_core) {
    auto brisc_thread_it = current_core.threads.find("BRISC");
    if (brisc_thread_it != current_core.threads.end()) {
        for (auto &event_it: brisc_thread_it->second.events) {
            BriscEventProperties event_prop(event_it.first);
            if (event_prop.event_type == uint(BriscEventType::INPUT_NUM_TILES)) {
                uint operand_idx = event_prop.operand_idx;
                log_assert(event_it.second.size() == 1, "Only a single event must be recorded for operand idx {} input num_tiles, op_name {}", operand_idx, current_core.descriptor.op_name);
                uint64_t num_tiles = event_it.second.at(0).first_val;
                current_core.brisc_operand_to_num_tiles.insert({operand_idx, num_tiles});
            } else if (event_prop.event_type == uint(BriscEventType::INPUT_TILE_POP)) {
                uint operand_idx = event_prop.operand_idx;
                log_assert(event_it.second.size() == 1, "Only a single event must be recorded for operand idx {} input tile timestamp, op_name {}", operand_idx, current_core.descriptor.op_name);
                uint64_t start_timestamp = event_it.second.at(0).first_val;
                uint64_t end_timestamp = event_it.second.at(0).second_val;
                current_core.brisc_operand_to_pop_tiles_num_cycles.insert({operand_idx, end_timestamp - start_timestamp});
            } else if (event_prop.event_type == uint(BriscEventType::OUTPUT_NUM_TILES)) {
                log_assert(event_prop.operand_idx == 0, "Currently only a single output operand is supported");
                uint operand_idx = event_prop.operand_idx + PERF_MAX_NUM_INPUTS;
                log_assert(event_it.second.size() == 1, "Only a single event must be recorded for operand idx {} output num_tiles, op_name {}", operand_idx, current_core.descriptor.op_name);
                uint64_t num_tiles = event_it.second.at(0).first_val;
                current_core.brisc_operand_to_num_tiles.insert({operand_idx, num_tiles});
            } else if (event_prop.event_type == uint(BriscEventType::OUTPUT_TILE_PUSH)) {
                log_assert(event_prop.operand_idx == 0, "Currently only a single output operand is supported");
                uint operand_idx = event_prop.operand_idx + PERF_MAX_NUM_INPUTS;
                log_assert(event_it.second.size() == 1, "Only a single event must be recorded for operand idx {} output tile timestamp, op_name {}", operand_idx, current_core.descriptor.op_name);
                uint64_t start_timestamp = event_it.second.at(0).first_val;
                uint64_t end_timestamp = event_it.second.at(0).second_val;
                current_core.brisc_operand_to_pop_tiles_num_cycles.insert({operand_idx, end_timestamp - start_timestamp});
            }
        }
    }
}

// This pass calculates and populates most of per thread events.
// All the fields in outer_loop_events will be populated
void calculate_first_to_last_outer_loop_cycles(core_events &current_core) {

    auto unpack_thread_it = current_core.threads.find("T0");
    // Total wait for tile in the unpacker and wait for free space in the packer after the first block of data has arrived.
    map<uint, uint64_t> outer_loop_to_total_wait_time;
    if (unpack_thread_it != current_core.threads.end()) {
        for (auto &event_id: unpack_thread_it->second.events) {
            EventProperties event_prop(event_id.first);
            
            // Add new outer loop entry if already doesn't exists
            if (current_core.all_outer_loop_events.find(event_prop.outer_loop_idx) == current_core.all_outer_loop_events.end()) {
                current_core.all_outer_loop_events.insert({event_prop.outer_loop_idx, outer_loop_events()});
            }
            
            // If no events are recorded. Continue to next one
            if (event_id.second.size() == 0) {
                continue;
            }

            uint64_t new_event_start_val = event_id.second.at(0).first_val;
            uint64_t new_event_end_val = event_id.second.at(0).second_val;
            
            if (event_prop.event_type == uint(perf::EventType::UNPACK_FIRST_INSTRUCTION)) {
                uint64_t current_unpack_end_val = current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).unpack_first_instruction;
                log_assert(current_unpack_end_val == ULLONG_MAX, "There should be a single first-unpack-instruction event recorded per-input");
                current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).unpack_first_instruction = new_event_start_val;            
            } else if (event_prop.event_type == uint(perf::EventType::STALL_TRISC_FOR_DRAM_PERF_DUMP)) {
                for (int i = 0; i < event_id.second.size(); i++) {
                    uint64_t event_first_val = event_id.second.at(i).first_val;
                    uint64_t event_second_val = event_id.second.at(i).second_val;
                    current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).total_trisc0_stalled_on_ncrisc += (event_second_val - event_first_val);
                }
            }
        }

        for (auto &event_id: unpack_thread_it->second.events) {
            EventProperties event_prop(event_id.first);
            log_assert(current_core.all_outer_loop_events.find(event_prop.outer_loop_idx) != current_core.all_outer_loop_events.end(), "All input entries must have been created in previous loop");
            if (event_prop.event_type == uint(perf::EventType::WAIT_FOR_INCOMING_TILES)) {
                for (int i = 0; i < event_id.second.size(); i++) {
                    if (event_id.second.at(i).first_val > current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).unpack_first_instruction) {
                        uint64_t wait_for_tile_diff = (event_id.second.at(i).second_val - event_id.second.at(i).first_val);
                        current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).total_wait_for_tile_after_first_unpack += wait_for_tile_diff;
                    }
                }                
            }
        }
    }

    auto math_thread_it = current_core.threads.find("T1");
    if (math_thread_it != current_core.threads.end()) {
        for (auto &event_id: math_thread_it->second.events) {
            uint64_t event_first_val = event_id.second.at(0).first_val;
            uint64_t event_second_val = event_id.second.at(0).second_val;
            uint64_t previous_math_activity = current_core.math_activity;
            if (previous_math_activity != ULLONG_MAX) {
                log_assert(previous_math_activity == event_second_val, "Math activity must be the same across all inputs.");
            } else {
                current_core.math_activity = event_second_val;
            }
        }
    }

    auto pack_thread_it = current_core.threads.find("T2");
    if (pack_thread_it != current_core.threads.end()) {
        for (auto &event_id: pack_thread_it->second.events) {
            EventProperties event_prop(event_id.first);
            if (event_prop.event_type == uint(perf::EventType::OUTPUT_NUM_TILES)) {
                current_core.packer_num_tiles = event_id.second.at(0).first_val;
            }
            if (event_prop.event_type == uint(perf::EventType::OUTPUT_TIMESTAMP)) {
                current_core.packer_push_runtime = event_id.second.at(0).second_val - event_id.second.at(0).first_val;
            }
            // Add new outer loop entry if already doesn't exists
            if (current_core.all_outer_loop_events.find(event_prop.outer_loop_idx) == current_core.all_outer_loop_events.end()) {
                current_core.all_outer_loop_events.insert({event_prop.outer_loop_idx, outer_loop_events()});
            }
            if (event_prop.event_type == uint(perf::EventType::PACK_EACH_INPUT)) {
                uint64_t event_first_val = event_id.second.at(0).first_val;
                uint64_t event_second_val = event_id.second.at(0).second_val;
                // event last_event_with_this_type = event_id.second.back();
                current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).pack_first_start = event_first_val;
                current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).pack_last_end = event_second_val;
            } else if (event_prop.event_type == uint(perf::EventType::WAIT_FOR_FREE_TILES)) {
                for (int i = 0; i < event_id.second.size(); i++) {
                    uint64_t event_first_val = event_id.second.at(i).first_val;
                    uint64_t event_second_val = event_id.second.at(i).second_val;
                    uint64_t first_block_available = current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).unpack_first_instruction;
                    if (event_second_val >= first_block_available) {
                        uint64_t wait_free_tile_start = (event_first_val > first_block_available) ? event_first_val : first_block_available;
                        current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).total_wait_for_free_tiles_after_first_unpack += (event_second_val - wait_free_tile_start);
                    }
                }
            } else if (event_prop.event_type == uint(perf::EventType::STALL_TRISC_FOR_DRAM_PERF_DUMP)) {
                for (int i = 0; i < event_id.second.size(); i++) {
                    uint64_t event_first_val = event_id.second.at(i).first_val;
                    uint64_t event_second_val = event_id.second.at(i).second_val;
                    current_core.all_outer_loop_events.at(event_prop.outer_loop_idx).total_trisc2_stalled_on_ncrisc += (event_second_val - event_first_val);
                }
            }
        }
    }
    set_number_of_inputs_recorded(current_core);
}

void set_unpack_pack_num_tiles(core_events& current_core) {
    if (current_core.threads.find("T0") != current_core.threads.end()) {
        for (const auto& unpack_event_it : current_core.threads.at("T0").events) {
            EventProperties event_prop(unpack_event_it.first);
            if (event_prop.event_type == uint(perf::EventType::NUM_TILES_UNPACK)) {
                current_core.trisc_operand_to_num_tiles[event_prop.operand_idx] = unpack_event_it.second.at(0).first_val;
            }
        }
    }
    
    if (current_core.threads.find("T2") != current_core.threads.end()) {
        for (const auto& pack_event_it : current_core.threads.at("T2").events) {
            EventProperties event_prop(pack_event_it.first);
            if (event_prop.event_type == uint(perf::EventType::NUM_TILES_PACK)) {
                log_assert(event_prop.operand_idx == 0, "Currently only a single output operand is supported");
                uint operand_idx = event_prop.operand_idx + PERF_MAX_NUM_INPUTS;
                log_assert(pack_event_it.second.size() == 1, "Only a single event must be recorded for operand idx {} output num_tiles, op_name {}", operand_idx, current_core.descriptor.op_name);
                current_core.trisc_operand_to_num_tiles[operand_idx] = pack_event_it.second.at(0).first_val;
            }
        }
    }
}

void check_end_time_recorded(core_events &current_core, const PerfDesc &perf_desc) {
    for (auto &thread_item: current_core.threads) {
        vector<uint64_t> empty_event_ids;
        for (auto &event_vec_item: thread_item.second.events) {
            if (thread_item.second.thread_id < 3) {
                EventProperties properties(event_vec_item.first);
                // Keep the single-value trisc events
                if (thread_item.second.thread_id < 3 && perf::single_value_events.find(properties.event_type) != perf::single_value_events.end()) {
                    continue;
                }
            }
            if (thread_item.second.thread_id == 4) {
                BriscEventProperties properties(event_vec_item.first);
                if (thread_item.second.thread_id == 4 && perf::brisc_single_value_events.find(properties.event_type) != perf::brisc_single_value_events.end()) {
                    continue;
                }
            }
            string event_description = event_vec_item.second[0].description;
            for (int i = 0; i < event_vec_item.second.size(); /*i++*/) {
                auto event = event_vec_item.second[i];
                if (event.first_val == ULLONG_MAX) {
                    std::stringstream ss;
                    ss << "Removing event with description " << event_description << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_item.first << " because first_value is not found";
                    perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                    event_vec_item.second.erase(event_vec_item.second.begin() + i);
                } else if (event.second_val == ULLONG_MAX) {
                    std::stringstream ss;
                    if ((event.id >> 24) == (uint)perf::NcriscEventType::DRAM_IO_Q_STATUS)
                    {
                        ss << "Patching event with description " << event_description << " with id " << event_vec_item.second[i].id << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_item.first << " because second_value is not found";
                        perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                        event_vec_item.second[i].second_val = event.first_val + 1000;
                    }
                    else
                    {
                        ss << "Removing event with description " << event_description << " with id " << event_vec_item.second[i].id << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_item.first << " because second_value is not found";
                        perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                        event_vec_item.second.erase(event_vec_item.second.begin() + i);
                    }
                } else {
                    i++;
                }
            }
            if (event_vec_item.second.size() == 0) {
                empty_event_ids.push_back(event_vec_item.first);
            }
        }
        // After removing individual events from list of event ids, remove ids that no longer have any events associated with them.
        for (auto &event_id: empty_event_ids) {
            thread_item.second.events.erase(event_id);
        }
    }
}

void check_end_time_recorded_host(thread_events &current_thread) {
    vector<uint64_t> empty_event_ids;
    for (auto &event_vec_item: current_thread.events) {
        HostEventProperties properties(event_vec_item.first);
        string event_description = event_vec_item.second[0].description;
        if (perf::host_single_value_events.find(properties.event_type) != perf::host_single_value_events.end()) {
            continue;
        }
        for (int i = 0; i < event_vec_item.second.size(); /*i++*/) {
            auto event = event_vec_item.second[i];
            if (event.first_val == ULLONG_MAX) {
                std::stringstream ss;
                ss << "Removing host event with description " << event_description << " because first_value is not found";
                log_warning(tt::LogPerfPostProcess, "{}", ss.str());
                event_vec_item.second.erase(event_vec_item.second.begin() + i);
            } else if (event.second_val == ULLONG_MAX) {
                std::stringstream ss;
                ss << "Removing host event with description " << event_description << " because second_value is not found";
                log_warning(tt::LogPerfPostProcess, "{}", ss.str());
                event_vec_item.second.erase(event_vec_item.second.begin() + i);
            } else {
                i++;
            }
        }
        if (event_vec_item.second.size() == 0) {
            empty_event_ids.push_back(event_vec_item.first);
        }
        
    }

    // After removing individual events from list of event ids, remove ids that no longer have any events associated with them.
    for (auto &event_id: empty_event_ids) {
        current_thread.events.erase(event_id);
    }
    
}

void combine_ncrisc_events(thread_events &thread_events) {
    log_debug(tt::LogPerfPostProcess, "Running combine_ncrisc_events");
    for (auto &event_vec_item: thread_events.events) {
        string event_description = event_vec_item.second[0].description;
        uint event_id = event_vec_item.first;
        uint event_type = perf::get_ncrisc_event_type(event_id);
        if (event_type == (uint)perf::NcriscEventType::DRAM_READ_ISSUED) {
            uint dram_read_tile_flushed_event = (event_id & 0xFFFFFF) | ((uint)perf::NcriscEventType::DRAM_READ_TILE_FLUSHED << 24);
            auto event_ref = thread_events.events.find(dram_read_tile_flushed_event);

            if (event_vec_item.second.back().second_val != UINT_MAX) {
                event last_read_event = {.id = event_id, .first_val = event_vec_item.second.back().second_val};
                last_read_event.description = event_description;
                event_vec_item.second.push_back(last_read_event);
            }
            if (event_ref != thread_events.events.end()) {
                event last_flush_event = {.id = dram_read_tile_flushed_event, .first_val = event_ref->second.back().second_val};
                last_flush_event.description = event_ref->second[0].description;
                event_ref->second.push_back(last_flush_event);
                for (int i = 0; i < min(event_vec_item.second.size(), event_ref->second.size()); i++)
                {
                    event_vec_item.second[i].second_val = event_ref->second[i].first_val;
                }
                thread_events.events.erase(dram_read_tile_flushed_event);
            }
        }
    }
}

pair<uint64_t, uint64_t> get_earliest_start_and_lastest_end(const json& all_events) {
    uint64_t earliest_start = 0;
    uint64_t latest_end = 0;

    for (auto &core_label: all_events.items()) {
        auto core_label_value = core_label.value();
        if (core_label.key() != "per-epoch-events") {
            if (!core_label_value["NCRISC"].contains("epoch")) {
                // cout << "Generating partial perf vcd file." << endl;
                // gen_vcd_from_perf_dump(gstate, ai_clk, epoch_perf_json_files);
                log_assert(false, "NCRISC epoch event does not exist for core {}", core_label.key() );
            }
            uint64_t start = core_label_value["NCRISC"]["epoch"]["start"].at(0).get<uint64_t>();
            uint64_t end = core_label_value["NCRISC"]["epoch"]["end"].at(0).get<uint64_t>();
            if (earliest_start == 0 || start < earliest_start) {
                earliest_start = start;
            }
            if (end > latest_end) {
                latest_end = end;
            }
        }
    }
    return pair<uint64_t, uint64_t>(earliest_start, latest_end);
}

pair<uint64_t, uint64_t> get_epoch_q_empty_largest_delay(const json& all_events) {
    uint64_t largest_delay = 0;
    uint64_t largest_delay_start = 0;
    uint64_t largest_delay_end = 0;

    for (auto &core_label: all_events.items()) {
        auto core_label_value = core_label.value();
        if (core_label.key() != "per-epoch-events") {
            if (core_label_value.at("NCRISC").contains("epoch-q-empty")) {
                // string error_msg = "NCRISC epoch-q-empty event does not exist for core " + core_label.key() + " for json file " + all_events_path;
                // log_assert(false, error_msg);
                uint64_t diff_current_core = core_label_value["NCRISC"]["epoch-q-empty"]["diff"].at(0).get<uint64_t>();
                if (largest_delay == 0 || largest_delay < diff_current_core) {
                    largest_delay = diff_current_core;
                    largest_delay_start = core_label_value["NCRISC"]["epoch-q-empty"]["q-empty"].at(0).get<uint64_t>();
                    largest_delay_end = core_label_value["NCRISC"]["epoch-q-empty"]["q-available"].at(0).get<uint64_t>();
                }
            }
        }
    }
    return pair<uint64_t, uint64_t>(largest_delay_start, largest_delay_end);
}

void process_new_core(core_events &current_core, string core_id_str, const unordered_map<string, core_descriptor> &cores_to_ops, ofstream &output_log, bool &skip_to_next_core, bool &skip_to_next_thread) {
    current_core = core_events();
    core_id_str.pop_back(); // Last character is a ':'
    set_core_id(current_core, core_id_str);

    string op_name;
    if (cores_to_ops.find(core_id_str) != cores_to_ops.end()) {
        output_log << "Processing core\t(x=" << current_core.core_x << ", y=" << current_core.core_y << ") with op-name = " << op_name << endl;
        skip_to_next_core = false;
        skip_to_next_thread = true;
        current_core.descriptor = cores_to_ops.at(core_id_str);
    } else {
        op_name = "op-name-not-found";
        output_log << "Skipping core\t(x=" << current_core.core_x << ", y=" << current_core.core_y << ") op-name not found." << endl;
        skip_to_next_core = true;
    }
}

void process_new_thread(thread_events& current_thread, string current_line, bool &skip_to_next_thread) {

    current_thread = thread_events();
    current_line.pop_back();
    int thread_id = -1;
    for (int i = 0; i < thread_names.size(); i++) {
        if (current_line.find(thread_names[i]) != string::npos) {
            thread_id = i;
            break;
        }
    }
    log_assert(thread_id >= 0 && thread_id < thread_names.size(), "thread_id out of range");

    current_thread.thread_id = thread_id;
    skip_to_next_thread = false;
}

void process_thread_main(vector<uint32_t> &current_thread_events, thread_events &current_thread, bool concurrent) {
    
    int thread_id = current_thread.thread_id;

    if (concurrent) {
        if (thread_id == 1) {
            for (int i = 0; i < 2; i++) {
                log_assert(current_thread_events.at(i) == 0xffffffff, "The words 0 and 1 after header in math thread perf dump must be set to 0xffffffff");
            }
            current_thread_events.erase(current_thread_events.begin(), current_thread_events.begin() + 2);
        }
    }

    log_assert(thread_id >= 0 && thread_id < thread_names.size(), "Invalid thread id {}", thread_id);
    int num_values_per_event;
    if (thread_id == 1) {
        num_values_per_event = NumValuesPerEventMath;
    } else if (thread_id == 3) {
        num_values_per_event = NumValuesPerEventNcrisc;
    } else {
        num_values_per_event = NumValuesPerEventUnpackPack;
    }
    uint initial_math_perf_counter_value = 0;
    uint32_t clock_top_32h = 0;
    uint64_t ncrisc_loop = 0;
    uint64_t ncrisc_first_write = 0;
    // Loop over all the events found for the current thread.
    for (int i = 0; i < current_thread_events.size(); i += num_values_per_event) {
        // For thread-1, perf events are recorded in groups of four 4B values.
        //      1- total cycles in this period.
        //      2- math cycles.
        //      3- event id.
        //      4- Unused currently.
        // The first 16B math events are dummy values to get the initial math counter number.

        // For thread-0 and thread-2, perf events are recorded in groups of two.
        //      1- event-id.
        //      2- timestamp.
        if (i + num_values_per_event > current_thread_events.size()) { break; }

        if (concurrent && current_thread_events.at(i) == PerfValLast) {
            break;
        }
        if (concurrent && current_thread_events.at(i) == 0xffffffff && (thread_id == 0 || thread_id == 2)) {
            // Only skip a single event (increment i by 1) for the next iteration of the loop
            i -= (num_values_per_event-1);
            continue;
        }

        if (thread_id == 1) {
            if (i == 0) {
                // Read the dummy perf event we always dump to get the initial perf counter value.
                initial_math_perf_counter_value = current_thread_events[i+1];
                continue;
            }
            uint event_id = 0; // Hardcoding event id to 0
            // The math cycle counter (second value) does not get reset between records.
            // So we need to subtract each counter from its previous value.
            uint second_val = current_thread_events[i+1];
            if (i >= num_values_per_event) {
                second_val -= current_thread_events[i-3];
            } else {
                second_val -= initial_math_perf_counter_value;
            }
            event current_event = {.id = event_id, .first_val = current_thread_events[i], .second_val = second_val};
            current_event.description = decode_event_name(event_id, thread_id);

            auto event_ref = current_thread.events.find(event_id);
            if (event_ref == current_thread.events.end()) {
                current_thread.events.insert(pair<int, vector<event>>(event_id, {current_event}));
            } else {
                event_ref->second.push_back(current_event);
            }
        } else {
            uint event_type;
            uint event_id = current_thread_events[i];
            // log_trace(tt::LogPerfInfra, "thread {} i {} reading event-first {} event-second {} event-third {}",
            //     thread_id, i, get_hex_representation_of_num(current_thread_events[i]).str(),
            //     get_hex_representation_of_num(current_thread_events[i+1]).str(), get_hex_representation_of_num(current_thread_events[i+2]).str());
            
            //ncrisc inserts padding in case we are at half buffer end and we do not have space to log full event.
            //keep skipping words that contain PerfValPadding value.
            if (thread_id == 3 && (event_id == PerfValPadding)) {
                i--;
                continue;
            }
            // If the event is for the timestampe top 32b, record the new value and skip this event.
            else if (thread_id == 3 && (uint(perf::NcriscEventType::WALL_CLOCK_TOP_32B) == perf::get_ncrisc_event_type(event_id))) {
                clock_top_32h = current_thread_events[i+1];
                continue;
            }
            auto event_ref = current_thread.events.find(event_id);
            // If the current event-id does not exist in the current thread's map of all events.
            // Add this event-id and actual event.
            if (event_ref == current_thread.events.end()) {
                if (thread_id == 3) {
                    //ncrisc event
                    uint64_t event_val_64b;

                    event_type = perf::get_ncrisc_event_type(event_id);
                    if (event_type <= (uint)perf::NcriscEventType::STREAM_HANDLER_INIT) {
                        //these events contain 64-bit timestamp.
                        event_val_64b = perf::events_32b_to_64b(current_thread_events[i+2], current_thread_events[i+1]);
                        i++;
                    }
                    else {
                        event_val_64b = perf::events_32b_to_64b(clock_top_32h, current_thread_events[i+1]);
                    }
                    event current_event = {.id = event_id, .first_val = event_val_64b};
                    if (event_type == (uint)perf::NcriscEventType::STREAM_INFO) {
                        //stream info event has 5 d-words that contain stream information.
                        current_event.first_val = current_thread_events[i+1];
                        current_event.second_val = current_thread_events[i+2];
                        current_event.extra_val.push_back(current_thread_events[i+3]);
                        current_event.extra_val.push_back(current_thread_events[i+4]);
                        current_event.extra_val.push_back(current_thread_events[i+5]);
                        current_event.extra_val.push_back(current_thread_events[i+6]);
                        i+=5;
                    }
                    else if (event_type == (uint)perf::NcriscEventType::STREAM_MISC_INFO) {
                        current_event.first_val = event_val_64b;
                        current_event.second_val = current_thread_events[i+2];
                        i++;
                    }
                    else if (event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_LOOP) {
                        ncrisc_loop = event_val_64b;
                    }
                    else if (event_type == (uint)perf::NcriscEventType::EPOCH_Q_SLOT_COMPLETE || event_type == (uint)perf::NcriscEventType::DRAM_WRITE_SENT
                                || event_type == (uint)perf::NcriscEventType::DRAM_WRITE_TILES_CLEARED) {
                        current_event.first_val = ncrisc_loop;
                        if (event_type == (uint)perf::NcriscEventType::DRAM_WRITE_SENT) {
                            ncrisc_first_write = event_val_64b;
                        }
                        else if (event_type == (uint)perf::NcriscEventType::DRAM_WRITE_TILES_CLEARED) {
                            current_event.first_val = ncrisc_first_write;
                        }
                        current_event.second_val = event_val_64b;

                    }
                    //else if (event_type == (uint)perf::NcriscEventType::STREAM_RESTART) {
                    //    current_event.second_val = current_event.first_val;
                    //}
                    current_event.description = decode_ncrisc_event_name(event_id);
                    current_thread.events.insert(pair<int, vector<event>>(event_id, {current_event}));

                } else {
                    uint64_t event_val_64b = perf::events_32b_to_64b(current_thread_events[i+1], current_thread_events[i+2]);
                    event current_event = {.id = event_id, .first_val = event_val_64b};
                    current_event.description = (thread_id == 4) ? decode_brisc_event_name(event_id) : decode_event_name(event_id, thread_id);
                    current_thread.events.insert(pair<int, vector<event>>(event_id, {current_event}));
                }
            } else {
            // If the current event-id exists in the current thread:
            // Check the last event recorded with this id in the current thread:
            // Add a new event if all previous events already have both first-val and second-vals populated.
            // Otherwise populate the second-val of the last event.
                if (event_ref->second.back().second_val == ULLONG_MAX) {
                    if (thread_id == 3) {
                        uint64_t event_val_64b;
                        uint event_type = perf::get_ncrisc_event_type(event_id);
                        if (event_type <= (uint)perf::NcriscEventType::STREAM_HANDLER_INIT) {
                            //these events contain 64-bit timestamp.
                            event_val_64b = perf::events_32b_to_64b(current_thread_events[i+2], current_thread_events[i+1]);
                            i++;
                        }
                        else {
                            event_val_64b = perf::events_32b_to_64b(clock_top_32h, current_thread_events[i+1]);
                        }
                        event_ref->second.back().second_val = event_val_64b;
                    }
                    else {
                        event_ref->second.back().second_val = perf::events_32b_to_64b(current_thread_events[i+1], current_thread_events[i+2]);
                    }

                } else {
                    if (thread_id == 3) {
                        event new_event = {.id = event_id, .second_val = perf::events_32b_to_64b(clock_top_32h, current_thread_events[i+1])};
                        uint event_type = perf::get_ncrisc_event_type(event_id);
                        if (event_type == (uint)perf::NcriscEventType::STREAM_MISC_INFO) {
                            new_event.first_val = new_event.second_val;
                            new_event.second_val = current_thread_events[i+2];
                            i++;
                        }
                        else if (event_type == (uint)perf::NcriscEventType::DRAM_IO_Q_STATUS || event_type == (uint)perf::NcriscEventType::STREAM_RESTART
                                || event_type == (uint)perf::NcriscEventType::STREAM_BUF_STATUS || event_type == (uint)perf::NcriscEventType::EPOCH_Q_EMPTY) {
                            new_event.first_val = new_event.second_val;
                            new_event.second_val = ULLONG_MAX;
                        } else {
                            new_event.first_val = event_ref->second.back().second_val;
                        }

                        new_event.description = decode_ncrisc_event_name(event_id);
                        event_ref->second.push_back(new_event);
                    }
                    else {
                        uint64_t event_val_64b = perf::events_32b_to_64b(current_thread_events[i+1], current_thread_events[i+2]);
                        event new_event = {.id = event_id, .first_val = event_val_64b};
                        new_event.description = (thread_id == 4) ? decode_brisc_event_name(event_id) : decode_event_name(event_id, thread_id);
                        event_ref->second.push_back(new_event);
                    }
                }
            }
        }
    }
}

void process_thread_end(
    thread_events &current_thread,
    core_events &current_core,
    vector<core_events> &all_core_events,
    vector<string> & all_core_ids,
    vector<uint32_t> &current_thread_events,
    string core_id_str,
    const PerfDesc &perf_desc,
    bool &skip_to_next_core,
    bool &skip_to_next_thread,
    bool modify_skip_flags,
    bool out_of_memory)
{
    int thread_id = current_thread.thread_id;
    log_assert(thread_id >= 0 && thread_id < thread_names.size(), "thread_id out of range");
    bool is_last_thread = thread_id == thread_names.size() - 1;
    if (out_of_memory) {
        perf_warning(perf_desc, tt::LogPerfPostProcess, "Setting out-of-memory flag for thread {} core {}", thread_id, core_id_str);
        current_thread.set_out_of_memory();
    }
    process_thread_main(current_thread_events, current_thread, false);
    if (thread_id == 3) {
        combine_ncrisc_events(current_thread);
    }
    thread_events current_thread_copy = current_thread;
    current_core.threads.insert(pair<string, thread_events>(thread_names[thread_id], current_thread_copy));

    current_thread_events.clear();

    if (is_last_thread) {
        if (modify_skip_flags) {
            skip_to_next_core = true;
            skip_to_next_thread = false;
        }
        check_end_time_recorded(current_core, perf_desc);
        calculate_first_to_last_outer_loop_cycles(current_core);
        calculate_brisc_bw_info(current_core);
        set_unpack_pack_num_tiles(current_core);
        current_core.check_and_set_out_of_memory();
        core_events current_core_copy = current_core;
        all_core_events.push_back(current_core_copy);
        all_core_ids.push_back(core_id_str + "-" + current_core_copy.descriptor.op_name);
    } else {
        if (modify_skip_flags) {
            skip_to_next_core = false;
            skip_to_next_thread = true;
        }
    }

}

pair<int, int> get_first_and_last_recorded_input_idx(const core_events& core) {
    int first_input = -1;
    int last_input = -1;
    for (const auto& outer_loop_it: core.all_outer_loop_events) {
        uint input_idx = outer_loop_it.first;
        if (first_input == -1 || input_idx < first_input) {
            first_input = input_idx;
        }
        if (last_input == -1 || input_idx > last_input) {
            last_input = input_idx;
        }
    }
    return {first_input, last_input};
}

host_global_events populate_host_global_events(const thread_events& thread) {
    host_global_events global_events;
    for (const auto& event_ref: thread.events) {
        uint event_id = event_ref.first;
        HostEventProperties event_properties(event_id);
        if (event_properties.event_type == uint(HostEventType::RUN_PROGRAM)) {
            for (const auto& each_event: event_ref.second) {
                if (global_events.first_program_start == ULLONG_MAX ||
                    global_events.first_program_start > each_event.first_val) {
                        global_events.first_program_start = each_event.first_val;
                    }
            }
        }
        if (event_properties.event_type == uint(HostEventType::POP_OUTPUT)) {
            for (const auto& each_event: event_ref.second) {
                if (global_events.last_output_pop_end == ULLONG_MAX ||
                    global_events.last_output_pop_end < each_event.second_val) {
                        global_events.last_output_pop_end = each_event.second_val;
                    }
            }
        }
    }
    return global_events;
}

uint32_t run_perf_model(const tt_op_model_desc& op_desc, string perf_output_dir) {
    log_assert(fs::exists(perf_output_dir), "Perf output directory {} does not exist", perf_output_dir);
    // Save the original streambufs of cout and cerr
    std::streambuf* originalCoutBuffer = std::cout.rdbuf();
    std::streambuf* originalCerrBuffer = std::cerr.rdbuf();

    // Create and open a file stream for redirection
    std::ofstream fileStream(perf_output_dir + "/" + perf_model_log);

    // Redirect cout and cerr to the file stream
    std::cout.rdbuf(fileStream.rdbuf());
    std::cerr.rdbuf(fileStream.rdbuf());

    uint32_t model_execution_cycles = 0;
    try {
        model_execution_cycles = OpModel::get_op_cycles(op_desc);
    } catch (std::exception &e) {}

    // Restore the original streambufs
    std::cout.rdbuf(originalCoutBuffer);
    std::cerr.rdbuf(originalCerrBuffer);

    return model_execution_cycles;
}

}
