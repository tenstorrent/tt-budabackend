// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "postprocess.hpp"
#include "perf_utils.hpp"
#include "create_reports.hpp"
#include "analyse.hpp"
#include "common/env_lib.hpp"
#include "common/model/tt_core.hpp"
#include "utils/scoped_timer.hpp"
#include "netlist/netlist_utils.hpp"

extern perf::tt_backend_perf backend_profiler;

namespace postprocess {
std::recursive_mutex perf_state_mutex;
std::recursive_mutex postprocessor_queue_mutex;
std::recursive_mutex report_generator_queue_mutex;

string decode_event_name(int event_id, ThreadType thread_type) {

    EventProperties event_properties(event_id);

    auto iter = event_labels.find(event_properties.event_type);
    if (iter == event_labels.end()) {
        return "";
    }
    string event_description = iter->second;
    
    event_description += "-outer-loop-" + to_string(event_properties.outer_loop_idx);
    if (thread_type == ThreadType::UNPACK or thread_type == ThreadType::PACK) {
        const std::unordered_set<uint> events_without_operand_ids = {
            uint(EventType::PACK_EACH_INPUT),
            uint(EventType::UNPACK_FIRST_INSTRUCTION),
            uint(EventType::NUM_TILES_PACK)
        };
        if (events_without_operand_ids.find(event_properties.event_type) == events_without_operand_ids.end()) {
            event_description += "-operand-" + to_string(event_properties.operand_idx);
            // include -num-tiles- in the description if event is not NUM_TILES_UNPACK
            if (event_properties.event_type != uint(EventType::NUM_TILES_UNPACK)) {
                event_description += "-num-tiles-" + to_string(event_properties.num_tiles);
            }
        }
    }
    return event_description;
}

string decode_host_event_name(uint64_t event_id, int pid, uint thread_id, const vector<string> &all_labels) {
    HostEventProperties event_properties(event_id);
    
    auto iter = host_event_labels.find(event_properties.event_type);
    if (iter == host_event_labels.end()) {
        return "";
    }
    string event_description;
    if (event_properties.event_type == uint(HostEventType::CUSTOM)) {
        log_assert(all_labels.size() > event_properties.custom_event_label, "custom event label {} is out of bound {}", event_properties.custom_event_label, all_labels.size());
        event_description = all_labels.at(event_properties.custom_event_label);
    } else {
        event_description  = iter->second;
        if (event_properties.event_type == uint(HostEventType::DEVICE_EPOCH_FIRST_UNPACK_LAST_PACK)) {
            event_description += "-program-id-" + to_string(event_properties.program_id);
            event_description += "-epoch-id-" + to_string(event_properties.epoch_id);
        }
        if (event_properties.event_type == uint(HostEventType::DEVICE_START_CYCLE) || 
            event_properties.event_type == uint(HostEventType::DEVICE_END_CYCLE) || 
            event_properties.event_type == uint(HostEventType::DEVICE_START_CYCLE_ALIGNED) || 
            event_properties.event_type == uint(HostEventType::DEVICE_END_CYCLE_ALIGNED) || 
            event_properties.event_type == uint(HostEventType::DEVICE_RUNTIME) ||
            event_properties.event_type == uint(HostEventType::WAIT_FOR_EPOCH_COMPLETE) ||
            event_properties.event_type == uint(HostEventType::DEVICE_EPOCH_FIRST_UNPACK_LAST_PACK)) {
                event_description += "-device-" + to_string(event_properties.device_id);
        }
    }
    event_description += "_pid_" + to_string(pid) + "_th_" + to_string(thread_id);
    return event_description;
}

string decode_ncrisc_event_name(int event_id) {
    string event_description;

    uint event_type = (event_id >> 24)  & 0xff;

    auto iter = ncrisc_event_labels.find(event_type);
    if (iter == ncrisc_event_labels.end()) {
        return "";
    }
    event_description = iter->second;
    if (event_type == (uint)NcriscEventType::EPOCH_Q_SLOT_COMPLETE || event_type == (uint)NcriscEventType::DRAM_WRITE_TILES_CLEARED || event_type == (uint)NcriscEventType::DRAM_IO_Q_STATUS
         || event_type == (uint)NcriscEventType::STREAM_RESTART || event_type == (uint)NcriscEventType::STREAM_INFO || event_type == (uint)NcriscEventType::STREAM_BUF_STATUS || event_type == (uint)NcriscEventType::STREAM_MISC_INFO) {
        uint stream_id = event_id & 0xff;
        event_description += "-stream-" + to_string(stream_id);
    } else if (event_type == (uint)NcriscEventType::DRAM_READ_ISSUED || event_type == (uint)NcriscEventType::DRAM_WRITE_SENT) {
        uint stream_id = (event_id >> 16) & 0xff;
        uint size = event_id & 0xffff;
        event_description += "-stream-" + to_string(stream_id) + "-" + to_string(size);
    }

    return event_description;
}

string decode_brisc_event_name(int event_id) {
    string event_description;

    BriscEventProperties event_properties(event_id);

    auto iter = brisc_event_labels.find(event_properties.event_type);
    if (iter == brisc_event_labels.end()) {
        return "";
    }
    event_description = iter->second;
    event_description += "-epoch-id-" + to_string(event_properties.epoch_id);
    if (
        event_properties.event_type == uint(BriscEventType::INPUT_NUM_TILES) ||
        event_properties.event_type == uint(BriscEventType::OUTPUT_NUM_TILES) ||
        event_properties.event_type == uint(BriscEventType::INPUT_TILE_POP) ||
        event_properties.event_type == uint(BriscEventType::OUTPUT_TILE_PUSH)) {
        
        event_description += "-operand-idx-" + to_string(event_properties.operand_idx);
    }
    return event_description;
}

string get_perf_out_directory(const string& test_output_dir, const string& override_perf_output_dir, bool clean) {

    string perf_root_dir = "";
    if (override_perf_output_dir.empty()) {
        perf_root_dir = test_output_dir;
    } else {
        perf_root_dir = override_perf_output_dir;
        string perf_default_dir = test_output_dir + "/perf_results/";
        if (fs::exists(perf_default_dir)) {
            fs::remove_all(perf_default_dir);
        }
        fs::create_directories(perf_default_dir);
        std::ofstream output_file(perf_default_dir + "perf_output_directory_path.txt");
        output_file << "Performance output directory was overriden and moved to " << perf_root_dir + "/perf_results/" << endl;
        output_file.close();
    }
    string perf_dir = perf_root_dir + "/perf_results/";
    if (!fs::exists(perf_root_dir)) {
        fs::create_directories(perf_root_dir);
    }
    if (clean) {
        if (fs::exists(perf_dir)) {
            fs::remove_all(perf_dir);
        }
    }
    if (!fs::exists(perf_dir)) {
        fs::create_directories(perf_dir);
    }
    return perf_dir;
}

string get_device_perf_out_directory(const string& test_output_dir, const PerfDesc& perf_desc, bool clean) {

    string perf_lib_dir = get_perf_out_directory(test_output_dir, perf_desc.override_perf_output_dir, false);
    string trisc_decouple_str = perf_desc.get_decouple_mode_name();
    string overlay_decouple_str = perf_desc.get_overlay_decouple_string();
    string device_perf_dir = perf_lib_dir + "device/";
    if (clean) {
        if (fs::exists(device_perf_dir)) {
            fs::remove_all(device_perf_dir);
        }
        log_info(tt::LogPerfPostProcess, "Perf output directory: {}", device_perf_dir);
    }
    if (!fs::exists(device_perf_dir)) {
        fs::create_directories(device_perf_dir);
    }
    string decouple_file_path = device_perf_dir + decouple_info_file_name;
    if (!fs::exists(decouple_file_path)) {
        // Create file that records decouplings
        ofstream decouple_info(decouple_file_path);
        // Record trisc decouplings
        decouple_info << "trisc decouplings/triplets: " << "\n";
        decouple_info <<  (trisc_decouple_str == "device" ? "none" : trisc_decouple_str) << "\n\n";

        // Record overlay decouplings
        decouple_info << "overlay decouplings: " << "\n";
        decouple_info << (overlay_decouple_str.empty() ? "none" : overlay_decouple_str) << std::endl;

        decouple_info.flush();
        decouple_info.close();
    }
    
    return device_perf_dir;
}

string get_host_perf_out_directory(const string& test_output_dir, const string& override_perf_output_dir, bool clean) {
    string perf_lib_dir = get_perf_out_directory(test_output_dir, override_perf_output_dir, false);
    string host_perf_dir = perf_lib_dir + "/host/";
    if (clean) {
        if (fs::exists(host_perf_dir)) {
            fs::remove_all(host_perf_dir);
        }
    }
    if (!fs::exists(host_perf_dir)) {
        fs::create_directories(host_perf_dir);
    }
    return host_perf_dir;
}

string get_graph_descriptor_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean) {
    
    string perf_lib_dir = get_perf_out_directory(test_output_dir, override_perf_output_dir, false);
    string graph_perf_dir = perf_lib_dir + "/graph_descriptor/";
    if (clean) {
        if (fs::exists(graph_perf_dir)) {
            fs::remove_all(graph_perf_dir);
        }
    }
    if (!fs::exists(graph_perf_dir)) {
        fs::create_directories(graph_perf_dir);
    }
    return graph_perf_dir;
}

string get_queue_descriptor_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean) {
    
    string perf_lib_dir = get_perf_out_directory(test_output_dir, override_perf_output_dir, false);
    string queue_perf_dir = perf_lib_dir + "/queue_descriptor/";
    if (clean) {
        if (fs::exists(queue_perf_dir)) {
            fs::remove_all(queue_perf_dir);
        }
    }
    if (!fs::exists(queue_perf_dir)) {
        fs::create_directories(queue_perf_dir);
    }
    return queue_perf_dir;
}


string get_metadata_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean) {
    string perf_lib_dir = get_perf_out_directory(test_output_dir, override_perf_output_dir, false);
    string metadata_perf_dir = perf_lib_dir + "/metadata/";
    if (clean) {
        if (fs::exists(metadata_perf_dir)) {
            fs::remove_all(metadata_perf_dir);
        }
    }
    if (!fs::exists(metadata_perf_dir)) {
        fs::create_directories(metadata_perf_dir);
    }
    return metadata_perf_dir;
}

string get_host_profiler_report_path(const string& test_output_dir, const string& override_perf_output_dir, uint pid, uint thread_id, bool clean, bool device_alignment_report) {
    string host_dir = get_host_perf_out_directory(test_output_dir, override_perf_output_dir, clean);
    string prefix = device_alignment_report ? "device_alignment" : "postprocess";
    return host_dir + prefix + "_th_" + to_string(thread_id) + "_proc_" + to_string(pid) + ".json";
}

void remove_perf_output_directory(const string& output_dir, const string& override_perf_output_dir) {
    string perf_lib_dir = get_perf_out_directory(output_dir, override_perf_output_dir, false);
    if (fs::exists(perf_lib_dir)) {
        log_debug(tt::LogPerfPostProcess, "Removing perf output directory: {}", perf_lib_dir);
        fs::remove_all(perf_lib_dir);
    }
}

void set_core_id(core_events &current_core, const string& core_id_str) {
    auto delimiter_pos = core_id_str.find('-');
    log_assert (delimiter_pos != string::npos, "Could not find '-' in core descriptor");
    current_core.core_x = std::stoi(core_id_str.substr(0, delimiter_pos));
    int core_y_dim_num_chars = core_id_str.size() - 1 - delimiter_pos;
    current_core.core_y = std::stoi(core_id_str.substr(delimiter_pos + 1, core_y_dim_num_chars));
}

// In the current yaml from versim/silicon, only the core-id's have both '-' and ':' character
// TODO: Properly detect #-# sequence to verify this is a core-id, regex is too slow
bool is_core_id(const string &current_line) {
    bool dash_exists = current_line.find('-') != string::npos;
    bool colon_exists = current_line.find(':') != string::npos;
    return dash_exists and colon_exists;
}

bool is_thread_id(const string &current_line) {
    for (const string &thread_name : thread_names) {
        if (current_line.find(thread_name) != string::npos) {
            return true;
        }
    }
    return false;
}

pair<uint64_t, uint64_t> get_epoch_q_empty_largest_delay(const json &all_events) {
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
                if (largest_delay == 0 or largest_delay < diff_current_core) {
                    largest_delay = diff_current_core;
                    largest_delay_start = core_label_value["NCRISC"]["epoch-q-empty"]["q-empty"].at(0).get<uint64_t>();
                    largest_delay_end = core_label_value["NCRISC"]["epoch-q-empty"]["q-available"].at(0).get<uint64_t>();
                }
            }
        }
    }
    return pair<uint64_t, uint64_t>(largest_delay_start, largest_delay_end);
}

uint64_t find_stream_handler_loop_event_val(const vector<uint32_t> &current_thread_events) {
    int i = 0;
    while (i < current_thread_events.size()) {
        uint event_id = current_thread_events.at(i);
        if (event_id == PerfValPadding) {
            i++;
            continue;
        }
        if (perf::get_ncrisc_event_type(current_thread_events.at(i)) == (uint)perf::NcriscEventType::STREAM_HANDLER_LOOP) {
            if ((i + 2) < current_thread_events.size()) {
                return perf::events_32b_to_64b(current_thread_events.at(i + 2), current_thread_events.at(i + 1));
            }
            return UINT64_MAX;
        }
        uint event_type = perf::get_ncrisc_event_type(event_id);
        log_assert(ncrisc_event_to_num_values.find(event_type) != ncrisc_event_to_num_values.end(), "Event type {} not found in ncrisc_event_to_num_values", event_type);
        uint increment_to_next_event = ncrisc_event_to_num_values.at(event_type);
        i += increment_to_next_event;
    }
    return UINT64_MAX;
}

void process_new_thread(thread_events& current_thread, string current_line, bool &skip_to_next_thread) {

    current_thread = thread_events();
    current_line.pop_back();
    int thread_id = -1;
    for (int i = 0; i < thread_names.size(); i++) {
        if (current_line.find(thread_names.at(i)) != string::npos) {
            thread_id = i;
            break;
        }
    }
    log_assert(thread_id >= 0 and thread_id < thread_names.size(), "thread_id out of range");

    current_thread.thread_type = ThreadType(thread_id);
    skip_to_next_thread = false;
}

// Populates <current_thread> with events we extracted from device dram stored in <current_thread_events>
void process_thread_main(vector<uint32_t> &current_thread_events, thread_events &current_thread, bool concurrent) {
    
    ThreadType thread_type = current_thread.thread_type;

    if (concurrent) {
        if (thread_type == ThreadType::MATH) {
            for (int i = 0; i < 2; i++) {
                log_assert(current_thread_events.at(i) == 0xffffffff, "The words 0 and 1 after header in math thread perf dump must be set to 0xffffffff");
            }
            current_thread_events.erase(current_thread_events.begin(), current_thread_events.begin() + 2);
        }
    }

    log_assert(int(thread_type) >= 0 and int(thread_type) < thread_names.size(), "Invalid thread type {}", int(thread_type));
    int default_num_values_per_event;
    if (thread_type == ThreadType::MATH) {
        default_num_values_per_event = NumValuesPerEventMath;
    } else if (thread_type == ThreadType::NCRISC) {
        default_num_values_per_event = NumValuesPerEventNcrisc;
    } else {
        default_num_values_per_event = NumValuesPerEventUnpackPack;
    }
    uint initial_math_perf_counter_value = 0;
    uint32_t clock_top_32h = 0;
    uint64_t ncrisc_loop = UINT64_MAX;
    uint64_t ncrisc_first_write = 0;

    uint i = 0;
    uint increment_to_next_event;
    while (i < current_thread_events.size()) {
        increment_to_next_event = default_num_values_per_event;
        if (i + default_num_values_per_event > current_thread_events.size()) { 
            break; 
        }

        if (concurrent and current_thread_events.at(i) == PerfValLast) {
            break;
        }

        // If we hit 0xffffffff in concurrent mode, only increment i by 1 for the next iteration of the loop
        if (concurrent and current_thread_events.at(i) == 0xffffffff and (thread_type == ThreadType::UNPACK or thread_type == ThreadType::PACK)) {
            i++;
            continue;
        }
        // Process Unpack, Pack, Brisc events
        if (thread_type == ThreadType::UNPACK or thread_type == ThreadType::PACK or thread_type == ThreadType::BRISC) {
            uint event_type;
            uint event_id = current_thread_events.at(i);
            auto event_iterator = current_thread.events.find(event_id);
            if (event_iterator == current_thread.events.end()) {
                uint64_t event_val_64b = perf::events_32b_to_64b(current_thread_events.at(i + 1), current_thread_events.at(i + 2));
                device_perf_event current_event = {.id = event_id, .first_val = event_val_64b};
                current_event.description = (thread_type == ThreadType::BRISC) ? decode_brisc_event_name(event_id) : decode_event_name(event_id, thread_type);
                current_thread.events.emplace(event_id, vector<device_perf_event>{current_event});
            }
            // If the current event-id exists in the current thread:
            // Check the last event recorded with this id in the current thread:
            // Add a new event if all previous events already have both first-val and second-vals populated.
            // Otherwise populate the second-val of the last event.
            else if (event_iterator != current_thread.events.end()) {
                vector<device_perf_event> &events_vec = event_iterator->second;
                if (events_vec.back().second_val == ULLONG_MAX) {
                    events_vec.back().second_val = perf::events_32b_to_64b(current_thread_events.at(i + 1), current_thread_events.at(i + 2));
                }
                else if (events_vec.back().second_val != ULLONG_MAX) {
                    uint64_t event_val_64b = perf::events_32b_to_64b(current_thread_events.at(i + 1), current_thread_events.at(i + 2));
                    device_perf_event new_event = {.id = event_id, .first_val = event_val_64b};
                    new_event.description = (thread_type == ThreadType::BRISC) ? decode_brisc_event_name(event_id) : decode_event_name(event_id, thread_type);
                    events_vec.push_back(new_event);
                }
            }
        }
        // Process Math Events
        else if (thread_type == ThreadType::MATH) {
            if (i == 0) {
                // Read the dummy perf event we always dump to get the initial perf counter value.
                initial_math_perf_counter_value = current_thread_events.at(i + 1);
                i += default_num_values_per_event;
                continue;
            }
            uint event_id = 0; // Hardcoding event id to 0
            // The math cycle counter (second value) does not get reset between records.
            // So we need to subtract each counter from its previous value.
            uint second_val = current_thread_events.at(i + 1);
            uint previous_math_perf_counter_value = i >= default_num_values_per_event ? current_thread_events.at(i - 3) : initial_math_perf_counter_value;
            second_val -= previous_math_perf_counter_value;
            device_perf_event current_event = {.id = event_id, .first_val = current_thread_events.at(i), .second_val = second_val};
            current_event.description = decode_event_name(event_id, thread_type);

            auto event_iterator = current_thread.events.find(event_id);
            if (event_iterator == current_thread.events.end()) {
                current_thread.events.emplace(event_id, vector<device_perf_event>{current_event});
            } 
            else if (event_iterator != current_thread.events.end()) {
                event_iterator->second.push_back(current_event);
            }
        }
        // Process NCRISC events
        else if (thread_type == ThreadType::NCRISC) {
            uint event_id = current_thread_events.at(i);
            // ncrisc inserts padding in case we are at half buffer end and we do not have space to log full event.
            // keep skipping words that contain PerfValPadding value.
            if (event_id == PerfValPadding) {
                i++;
                continue;
            }
            // If the event is for the timestamp top 32b, record the new value and skip this event.
            else if (perf::get_ncrisc_event_type(event_id) == (uint)perf::NcriscEventType::WALL_CLOCK_TOP_32B) {
                clock_top_32h = current_thread_events.at(i + 1);
                i += default_num_values_per_event;
                continue;
            }
            auto event_iterator = current_thread.events.find(event_id);
            uint event_type = perf::get_ncrisc_event_type(event_id);
            log_assert(ncrisc_event_to_num_values.find(event_type) != ncrisc_event_to_num_values.end(), "Event type {} not found in ncrisc_event_to_num_values", event_type);
            increment_to_next_event = ncrisc_event_to_num_values.at(event_type);
            if (i + increment_to_next_event > current_thread_events.size()) {
                break;
            }
            // If the current event-id does not exist in the current thread's map of all events.
            // Add this event-id and actual event.
            if (event_iterator == current_thread.events.end()) {
                uint64_t event_val_64b;
                if (event_type == (uint)perf::NcriscEventType::EPOCH or event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_LOOP or
                    event_type == (uint)perf::NcriscEventType::EPOCH_EPILOGUE or event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_INIT) {
                    //these events contain 64-bit timestamp.
                    event_val_64b = perf::events_32b_to_64b(current_thread_events.at(i + 2), current_thread_events.at(i + 1));
                }
                else {
                    event_val_64b = perf::events_32b_to_64b(clock_top_32h, current_thread_events.at(i + 1));
                }
                device_perf_event new_ncrisc_event = {.id = event_id, .first_val = event_val_64b};
                if (event_type == (uint)perf::NcriscEventType::STREAM_INFO) {
                    //stream info event has 5 d-words that contain stream information.
                    new_ncrisc_event.first_val = current_thread_events.at(i + 1);
                    new_ncrisc_event.second_val = current_thread_events.at(i + 2);
                    new_ncrisc_event.extra_val.push_back(current_thread_events.at(i + 3));
                    new_ncrisc_event.extra_val.push_back(current_thread_events.at(i + 4));
                    new_ncrisc_event.extra_val.push_back(current_thread_events.at(i + 5));
                    new_ncrisc_event.extra_val.push_back(current_thread_events.at(i + 6));
                }
                else if (event_type == (uint)perf::NcriscEventType::STREAM_MISC_INFO) {
                    new_ncrisc_event.first_val = event_val_64b;
                    new_ncrisc_event.second_val = current_thread_events.at(i + 2);
                }
                else if (event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_LOOP) {
                    ncrisc_loop = event_val_64b;
                }
                else if (event_type == (uint)perf::NcriscEventType::EPOCH_Q_SLOT_COMPLETE or event_type == (uint)perf::NcriscEventType::DRAM_WRITE_SENT or
                         event_type == (uint)perf::NcriscEventType::DRAM_WRITE_TILES_CLEARED) {
                    // in concurrent mode, we store stream handler loop event in epoch_perf_scratch buffer, then spill it into the perf buffer at the end 
                    // therefore stream handler loop event could be parsed out of order (after the events it should precede in the perf buffer like DRAM_WRITE_SENT)
                    // risc_perf.h:record_perf_value_at_offset, risc_perf.cc:spill_risc_epoch_perf_scratch
                    // therefore, we need to find the stream handler loop event before populating the first_val of the current event
                    if (ncrisc_loop == UINT64_MAX) {
                        log_assert(concurrent, "Stream handler loop event should be out of order only in concurrent mode");
                        ncrisc_loop = find_stream_handler_loop_event_val(current_thread_events);
                        log_assert(ncrisc_loop != UINT64_MAX, "Stream handler loop event not found");
                        log_assert(ncrisc_loop <= event_val_64b, "Stream handler loop event should have smaller timestamp than the current event");
                    }
                    new_ncrisc_event.first_val = ncrisc_loop;
                    if (event_type == (uint)perf::NcriscEventType::DRAM_WRITE_SENT) {
                        ncrisc_first_write = event_val_64b;
                    }
                    else if (event_type == (uint)perf::NcriscEventType::DRAM_WRITE_TILES_CLEARED) {
                        new_ncrisc_event.first_val = ncrisc_first_write;
                    }
                    new_ncrisc_event.second_val = event_val_64b;
                }
                //else if (event_type == (uint)perf::NcriscEventType::STREAM_RESTART) {
                //    current_event.second_val = current_event.first_val;
                //}
                new_ncrisc_event.description = decode_ncrisc_event_name(event_id);
                current_thread.events.emplace(event_id, vector<device_perf_event>{new_ncrisc_event});
            }
            // If the current event-id exists in the current thread:
            // Check the last event recorded with this id in the current thread:
            // Add a new event if all previous events already have both first-val and second-vals populated.
            // Otherwise populate the second-val of the last event.
            else if (event_iterator != current_thread.events.end()) {
                vector<device_perf_event> &events_vec = event_iterator->second;
                // populate the second-val of the last event if it hasn't yet been populated
                if (events_vec.back().second_val == ULLONG_MAX) {
                    uint64_t event_val_64b;
                    if (event_type == (uint)perf::NcriscEventType::EPOCH or event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_LOOP or
                        event_type == (uint)perf::NcriscEventType::EPOCH_EPILOGUE or event_type == (uint)perf::NcriscEventType::STREAM_HANDLER_INIT) {
                        //these events contain 64-bit timestamp, thus each event has 3 values instead of 2
                        event_val_64b = perf::events_32b_to_64b(current_thread_events.at(i + 2), current_thread_events.at(i + 1));
                    }
                    else {
                        event_val_64b = perf::events_32b_to_64b(clock_top_32h, current_thread_events.at(i + 1));
                    }
                    events_vec.back().second_val = event_val_64b;
                }

                else if (events_vec.back().second_val != ULLONG_MAX) {
                    device_perf_event new_ncrisc_event= {.id = event_id};
                    // Misc info events have 3 values
                    if (event_type == (uint)perf::NcriscEventType::STREAM_MISC_INFO) {
                        new_ncrisc_event.first_val = perf::events_32b_to_64b(clock_top_32h, current_thread_events.at(i + 1));
                        new_ncrisc_event.second_val = current_thread_events.at(i + 2);
                    }
                    // Record singular timestamps as the first val for these events
                    else if (event_type == (uint)perf::NcriscEventType::DRAM_IO_Q_STATUS or event_type == (uint)perf::NcriscEventType::STREAM_RESTART or
                             event_type == (uint)perf::NcriscEventType::STREAM_BUF_STATUS or event_type == (uint)perf::NcriscEventType::EPOCH_Q_EMPTY) {
                        new_ncrisc_event.first_val = perf::events_32b_to_64b(clock_top_32h, current_thread_events.at(i + 1));
                        new_ncrisc_event.second_val = ULLONG_MAX;
                    } 
                    // use the finishing timestamp of the previous event as the starting timestamp of this event
                    else {
                        new_ncrisc_event.first_val = events_vec.back().second_val;
                        new_ncrisc_event.second_val = perf::events_32b_to_64b(clock_top_32h, current_thread_events.at(i + 1));
                    }

                    new_ncrisc_event.description = decode_ncrisc_event_name(event_id);
                    events_vec.push_back(new_ncrisc_event);
                }
            }
        }
        i += increment_to_next_event;
    }
}

void process_thread_end(
    thread_events &current_thread,
    core_events &current_core,
    vector<core_events> &all_core_events,
    vector<string> &all_core_ids,
    vector<uint32_t> &current_thread_events,
    const string &core_id_str,
    const PerfDesc &perf_desc,
    bool &skip_to_next_core,
    bool &skip_to_next_thread,
    bool modify_skip_flags,
    bool out_of_memory
) {
    ThreadType thread_type = current_thread.thread_type;
    log_assert(int(thread_type) >= 0 and int(thread_type) < thread_names.size(), "thread_id out of range");
    if (out_of_memory) {
        perf_warning(perf_desc, tt::LogPerfPostProcess, "Setting out-of-memory flag for thread {} core {}", int(thread_type), core_id_str);
        current_thread.set_out_of_memory();
    }
    process_thread_main(current_thread_events, current_thread, false);
    if (thread_type == ThreadType::NCRISC) {
        combine_ncrisc_events(current_thread);
    }
    thread_events current_thread_copy = current_thread;
    current_core.threads.insert(pair<string, thread_events>(thread_names.at(int(thread_type)), current_thread_copy));
    current_thread_events.clear();
    bool is_last_thread = int(thread_type) == thread_names.size() - 1;
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

void check_end_time_recorded(core_events &current_core, const PerfDesc &perf_desc) {
    for (auto &[thread_name, events_in_thread] : current_core.threads) {
        vector<uint64_t> empty_event_ids;
        for (auto &[event_id, perf_events_with_same_id] : events_in_thread.events) {
            if (events_in_thread.thread_type < ThreadType::NCRISC) {
                EventProperties properties(event_id);
                // Keep the single-value trisc events
                if (perf::single_value_events.find(properties.event_type) != perf::single_value_events.end()) {
                    continue;
                }
            }
            else if (events_in_thread.thread_type == ThreadType::BRISC) {
                BriscEventProperties properties(event_id);
                if (perf::brisc_single_value_events.find(properties.event_type) != perf::brisc_single_value_events.end()) {
                    continue;
                }
            }
            string event_description = perf_events_with_same_id.at(0).description;
            int i = 0;
            while (i < perf_events_with_same_id.size()) {
                device_perf_event event = perf_events_with_same_id.at(i);
                if (event.first_val == ULLONG_MAX) {
                    std::stringstream ss;
                    ss << "Removing event with description " << event_description << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_name << " because first_value is not found";
                    perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                    perf_events_with_same_id.erase(perf_events_with_same_id.begin() + i);
                } else if (event.second_val == ULLONG_MAX) {
                    std::stringstream ss;
                    if ((event.id >> 24) == (uint)perf::NcriscEventType::DRAM_IO_Q_STATUS)
                    {
                        ss << "Patching event with description " << event_description << " with id " << perf_events_with_same_id.at(i).id << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_name << " because second_value is not found";
                        perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                        perf_events_with_same_id.at(i).second_val = event.first_val + 1000;
                    }
                    else
                    {
                        ss << "Removing event with description " << event_description << " with id " << perf_events_with_same_id.at(i).id << " from core " << current_core.core_x << "," << current_core.core_y << " in thread " << thread_name << " because second_value is not found";
                        perf_warning(perf_desc, tt::LogPerfPostProcess, "{}", ss.str());
                        perf_events_with_same_id.erase(perf_events_with_same_id.begin() + i);
                    }
                } else {
                    i++;
                }
            }
            if (perf_events_with_same_id.size() == 0) {
                empty_event_ids.push_back(event_id);
            }
        }
        // After removing individual events from list of event ids, remove ids that no longer have any events associated with them.
        for (const auto &event_id: empty_event_ids) {
            events_in_thread.events.erase(event_id);
        }
    }
}

void check_end_time_recorded_host(thread_events &current_thread) {
    vector<uint64_t> empty_event_ids;
    for (auto &event_vec_item: current_thread.events) {
        HostEventProperties properties(event_vec_item.first);
        string event_description = event_vec_item.second.at(0).description;
        if (perf::host_single_value_events.find(properties.event_type) != perf::host_single_value_events.end()) {
            continue;
        }
        for (int i = 0; i < event_vec_item.second.size(); /*i++*/) {
            auto event = event_vec_item.second.at(i);
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
        string event_description = event_vec_item.second.at(0).description;
        uint event_id = event_vec_item.first;
        uint event_type = perf::get_ncrisc_event_type(event_id);
        if (event_type == (uint)perf::NcriscEventType::DRAM_READ_ISSUED) {
            uint dram_read_tile_flushed_event = (event_id & 0xFFFFFF) | ((uint)perf::NcriscEventType::DRAM_READ_TILE_FLUSHED << 24);
            auto event_ref = thread_events.events.find(dram_read_tile_flushed_event);

            if (event_vec_item.second.back().second_val != UINT_MAX) {
                device_perf_event last_read_event = {.id = event_id, .first_val = event_vec_item.second.back().second_val};
                last_read_event.description = event_description;
                event_vec_item.second.push_back(last_read_event);
            }
            if (event_ref != thread_events.events.end()) {
                device_perf_event last_flush_event = {.id = dram_read_tile_flushed_event, .first_val = event_ref->second.back().second_val};
                last_flush_event.description = event_ref->second.at(0).description;
                event_ref->second.push_back(last_flush_event);
                for (int i = 0; i < min(event_vec_item.second.size(), event_ref->second.size()); i++)
                {
                    event_vec_item.second.at(i).second_val = event_ref->second.at(i).first_val;
                }
                thread_events.events.erase(dram_read_tile_flushed_event);
            }
        }
    }
}

// Buffer size for each worker is NOC_ADDRESS_ALIGNMENT bytes aligned
// The buffer space for every worker allocated in dram
uint32_t get_dram_perf_buf_inc(const buda_SocDescriptor *soc_descriptor, const tt_xy_pair &dram_core) {

    std::unordered_map<tt_xy_pair, vector<tt_xy_pair>> dram_core_to_workers = soc_descriptor->get_perf_dram_bank_to_workers();

    log_assert(dram_core_to_workers.find(dram_core) != dram_core_to_workers.end(), "DRAM core not found");

    int num_workers_current_dram_core = dram_core_to_workers.at(dram_core).size();
    int num_threads = thread_names.size();
    //perf_buf_inc should be multiple of NOC_ADDRESS_ALIGNMENT, to ensure NOC_ADDRESS_ALIGNMENT bytes aligned addresses
    uint32_t perf_buf_inc = num_workers_current_dram_core ? (dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_SIZE/num_workers_current_dram_core) & ~(NOC_ADDRESS_ALIGNMENT-1) : 0;
    return perf_buf_inc;

}

// Returns the number of times ncrisc will copy l1-buffers to dram for a single thread of a single core.
// Ncrisc dumps buffers in chunks of 2KB.
// Therefore, the total space allocated to each worker core should be a multiple of number_of_threads * half_l1_buffer_size.
uint32_t get_num_max_trisc_dram_reqs(const buda_SocDescriptor *soc_descriptor, tt_xy_pair dram_core, const PerfDesc& perf_desc) {
    uint32_t total_buf_size = 0;
    for (uint thread_idx = 0; thread_idx < l1_mem::address_map::PERF_NUM_THREADS; thread_idx++) {
        total_buf_size += perf_desc.get_perf_buf_size(thread_idx) / 2;
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

uint32_t get_l1_perf_buf_size_each_dump(const PerfDesc& perf_desc, int thread_id) {
    if (perf_desc.device_perf_mode == PerfDumpMode::IntermediateDump) {
        return perf_desc.get_perf_buf_size(thread_id)/2;
    } else {
        return perf_desc.get_perf_buf_size(thread_id);
    }
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
    log_assert (smallest_aligned_event <= num_events_per_thread and smallest_aligned_event >= num_events_each_dump, "Incorrect value for smallest_aligned_event");
    return smallest_aligned_event;
}

// generates map of each physical core to vector of instruction ids for which they are active
unordered_map<tt_xy_pair, vector<int>> get_cores_to_instr_idx_from_model(const vector<InstructionInfo*> &instructions_on_device, const buda_SocDescriptor* soc_descriptor) {
    if (instructions_on_device.empty()) {
        log_debug(tt::LogPerfInfra, "No instructions found on device");
        return {};
    } 
    unordered_map<tt_xy_pair, vector<int>> core_to_instr_idx;
    int instr_idx = 0;
    int target_device_id = instructions_on_device.at(0)->device_id;
    for (const InstructionInfo* instr: instructions_on_device) {
        log_assert(instr->device_id == target_device_id, "Expected all instructions to be on the same device {}", target_device_id);
        string program_name = instr->program_name;
        string graph_name = instr->graph_name;
        const tt_graph_info &graph_info = instr->netlist_graph.my_graph_info;

        for (const auto &op_it: graph_info.op_map) {
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

    if (!current_core.threads.at("T0").out_of_memory and !current_core.threads.at("T2").out_of_memory) {
        log_assert(num_unpacker_inputs == num_pack_inputs, "Failed for core {} Number of inputs recorded for unpacker {} and packer {} must be the same if there is no out of memory errors.",
            tt_xy_pair(current_core.core_x, current_core.core_y).str(), num_unpacker_inputs, num_pack_inputs);
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

vector<InstructionInfo> populate_all_instructions(PerfState &perf_state, const string& perf_out_dir, std::shared_ptr<tt_device> device) {
    log_debug(tt::LogPerfPostProcess, "Populating InstructionInfo vector for all instructions executed.");
    vector<InstructionInfo> all_instructions_info;

    // Device ID to frequency map.
    std::map<int, int> freq_all_devices = device->get_clocks();
    set<int> mmio_devices;
    set<int> active_devices;
    for (const auto &it: freq_all_devices) {
        mmio_devices.insert(it.first);
    }
    for (const auto &it: perf_state.get_device_alignment_info().device_id_to_start_cycle) {
        active_devices.insert(it.first);
    }

    // Populate the aiclk freqs for versim
    if (freq_all_devices.size() == 0){
        for (const auto& device_id: active_devices){
            freq_all_devices.insert({device_id, 500}); // Default freq.
            mmio_devices.insert(device_id);
        }
    }

    // Check for consistent clock across mmio devices, if non-mmio devices are used
    if (mmio_devices.size() != active_devices.size()) {

        log_assert(mmio_devices.size() > 0, "MMIO devices are empty");
        
        int last_device_id = -1;
        for (const auto &device: mmio_devices) {
            if (last_device_id != -1) {
                log_assert(freq_all_devices.at(last_device_id) == freq_all_devices.at(device), "If we use non-mmio chips, the aiclk freq for all mmio devices must be the same");
            }
            last_device_id = device;
        }
        // After the check, add the same frequency for all the devices that netlist is using
        for (int active_device: active_devices) {
            if (mmio_devices.find(active_device) == mmio_devices.end()) {
                freq_all_devices.insert({active_device, freq_all_devices.at(last_device_id)});
            }
        }
    }

    for (auto &device: freq_all_devices){
        log_info(tt::LogPerfPostProcess, "Observed AICLK: {} for device_id: {}", device.second, device.first);
    }

    int global_idx = 0;
    const vector<instruction_info_wrapper> &unprocessed_instrs = perf_state.get_unprocessed_executed_instr();
    for (const instruction_info_wrapper &instr_wrap: unprocessed_instrs) {
        const string &program_name = instr_wrap.program_name;
        const string &graph_name = instr_wrap.instr->graph_name;
        const tt_digraph &graph = perf_state.get_graph(graph_name);
        const uint target_device = graph.my_graph_info.target_device;
        const int &current_aiclk = freq_all_devices.at(target_device);

        InstructionInfo info(
            instr_wrap.program_id, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id,
            perf_state.get_num_instructions_executed(instr_wrap.program_id),
            program_name, graph_name, target_device, graph, perf_out_dir, current_aiclk, graph.my_graph_info.input_count);
        all_instructions_info.push_back(info);
    }
    perf_state.update_num_instructions_processed(unprocessed_instrs.size());
    return all_instructions_info;
}

void populate_output_directory_paths(vector<InstructionInfo> &all_instructions, const perf::PerfDesc &perf_desc, const string& output_dir) {
    log_debug(tt::LogPerfPostProcess, "Populating output directory paths for all instructions");
    string perf_out_dir = get_device_perf_out_directory(
        output_dir,
        perf_desc,
        false);

    for (InstructionInfo &instr: all_instructions) {
        instr.set_output_dir_path(perf_out_dir);
        if (!fs::exists(instr.output_dir_path)) {
            fs::create_directories(instr.output_dir_path);
        }
    }
}

// Parses the perf dump from dram and splits the events between worker cores, epochs and threads.
// The events in dram are partitioned as follows:
// The buffer size reserved for perf in each dram bank is DRAM_EACH_BANK_PERF_BUFFER_SIZE.
// ncrisc uses soc_descriptor.get_perf_dram_bank_to_workers() to determine which worker core dumps its l1 perf-buffer into which dram bank
// The entire space is evenly divided between those workers.
// Therefore, perf-buffer-size-per-worker = DRAM_EACH_BANK_PERF_BUFFER_SIZE / num_workers_dumped_in_this_bank.
// Each of these worker buffers are further evenly split between the 4 threads (T0->T1->T2->NCRISC).
// Space allocated to each one of these threads is equal to perf-buffer-size-per-worker / 4.
// Epochs are densely stored in this space. Each epoch starts with PerfValFirst and ends with PerfValLast.
// If we run out of space in dram for each thread, the last PerfValLast might have not been recorded.
unordered_map<string, std::shared_ptr<stringstream>> extract_core_events_from_device_dram_dump(const vector<InstructionInfo*> &instructions_on_device, const map<tt_cxy_pair, vector<uint32_t>> &all_dram_events, const perf::PerfDesc &perf_desc, const buda_SocDescriptor* soc_descriptor) {
    if (instructions_on_device.empty()) {
        return {};
    }
    int device_id = instructions_on_device.at(0)->device_id;
    log_debug(tt::LogPerfPostProcess, "Extracting worker core events from dram dump of device {}", device_id);
    std::unordered_map<tt_xy_pair, vector<tt_xy_pair>> dram_core_to_workers = soc_descriptor->get_perf_dram_bank_to_workers();
    unordered_map<string, std::shared_ptr<stringstream>> device_intermed_dumps;
    // We dump one output string stream for each epoch.
    vector<std::shared_ptr<stringstream>> output_streams;
    log_debug(tt::LogPerfPostProcess, "Dumping the extracted perf events for each worker core to the following output keys:");
    for (const InstructionInfo* instr : instructions_on_device) {
        log_assert(instr->device_id == device_id, "Expected all instructions to be on the same device {}", device_id);
        log_debug(tt::LogPerfPostProcess, "    {}", instr->intermed_dump_key);
        if (device_intermed_dumps.find(instr->intermed_dump_key) == device_intermed_dumps.end()) {
            device_intermed_dumps.emplace(instr->intermed_dump_key, std::make_shared<stringstream>());
        }
        output_streams.push_back(device_intermed_dumps.at(instr->intermed_dump_key));
    }

    bool skip_to_next_dram_core = true;

    unordered_map<tt_xy_pair, vector<int>> cores_to_instr = get_cores_to_instr_idx_from_model(instructions_on_device, soc_descriptor);
    for (const auto &dram_events: all_dram_events) {
        if (dram_events.first.chip != device_id) {
            continue;
        }
        int event_idx = 0;

        log_debug(tt::LogPerfPostProcess, "Dram core id: {}", dram_events.first.str());

        core_events current_core;
        set_core_id(current_core, to_string(dram_events.first.x) + "-" + to_string(dram_events.first.y));
        tt_xy_pair dram_core_coord(current_core.core_x, current_core.core_y);

        // all dram cores dumped from dram must exist in soc_descriptor.perf_dram_bank_to_workers map.
        log_assert(dram_core_to_workers.find(dram_core_coord) != dram_core_to_workers.end(), "Could not find DRAM core");

        int num_workers_current_dram_core = dram_core_to_workers.at(dram_core_coord).size();
        // As descriped in the comments above for this function, number of events per worker is determined by
        // Total perf buffer space per bank divided by number of workers that dump their perf-events in that bank. And each event is 4B.
        uint32_t num_events_per_thread[l1_mem::address_map::PERF_NUM_THREADS];
        for (int thread_id = 0; thread_id < l1_mem::address_map::PERF_NUM_THREADS; thread_id++) {
            num_events_per_thread[thread_id] = get_num_events_per_threads_in_dram(soc_descriptor, dram_core_coord, perf_desc, thread_id);
            log_debug(tt::LogPerfPostProcess, "num_events_per_thread: {}", num_events_per_thread[thread_id]);
        }
        log_debug(tt::LogPerfPostProcess, "Number of Workers for the current dram bank: {}", num_workers_current_dram_core);

        for (int worker_idx = 0; worker_idx < num_workers_current_dram_core; worker_idx++) {
            tt_xy_pair worker_core_coord = dram_core_to_workers[dram_core_coord][worker_idx];
            vector<int> dram_all_instructions;
            if (cores_to_instr.find(worker_core_coord) != cores_to_instr.end()) {
                dram_all_instructions = cores_to_instr.at(worker_core_coord);
            }
            string worker_core_id = to_string(worker_core_coord.x) + "-" + to_string(worker_core_coord.y);
            uint num_instructions = dram_all_instructions.size();
            if (num_instructions > 0) {
                log_debug(tt::LogPerfPostProcess, "Extracting events for worker core id: {} with active instructions:", worker_core_id);
                for (int instr_idx: dram_all_instructions) {
                    log_debug(tt::LogPerfPostProcess, "    {},", instr_idx);
                    *(output_streams.at(instr_idx)) << worker_core_id << ":" << endl;
                }
            } else {
                log_debug(tt::LogPerfPostProcess, "Skipping worker core id: {} since it is not active for any of the epochs", worker_core_id);
                for (int thread_id = 0; thread_id < l1_mem::address_map::PERF_NUM_THREADS; thread_id++) {
                    event_idx += num_events_per_thread[thread_id];
                    if (thread_id < l1_mem::address_map::PERF_NUM_THREADS - 1) {
                        uint num_padded_events_between_threads = get_num_events_to_skip_between_threads(soc_descriptor, dram_core_coord, perf_desc, thread_id);
                        event_idx += num_padded_events_between_threads;
                    } else {
                        uint num_padded_events_between_cores = get_num_events_to_skip_between_workers(soc_descriptor, dram_core_coord, perf_desc);
                        event_idx += num_padded_events_between_cores;
                    }
                }
                continue;
            }
            for (int thread_idx = 0; thread_idx < postprocess::thread_names.size(); thread_idx++) {
                for (int instr_idx: dram_all_instructions) {
                    string thread_name = postprocess::thread_names[thread_idx] + ":";
                    *(output_streams.at(instr_idx)) << "    " << thread_name << endl;
                }
                uint instr_idx = 0;
                int thread_event_idx = 0;
                bool found_start_signal_epoch = false;

                while (thread_event_idx < num_events_per_thread[thread_idx]) {
                    // The first event in each epoch must be equal to PerfValFirst.
                    // This flag gets set in the beginning of epoch, if PerfValFirst is observed.
                    // And it will get reset at the end of the epoch if PerfValLast is observed.
                    thread_event_idx++;
                    if (!found_start_signal_epoch) {
                        uint32_t event_num = dram_events.second.at(event_idx);
                        event_idx++;
                        if (event_num != PerfValFirst) {
                            for (uint i = instr_idx; i < num_instructions; i++) {
                                *(output_streams.at(dram_all_instructions.at(i))) << "        " << "- 0" << endl;
                            }
                            event_idx += num_events_per_thread[thread_idx] - thread_event_idx;
                            break;
                        } else {
                            log_assert(instr_idx < num_instructions, "instr_idx out of range");
                            *(output_streams.at(dram_all_instructions.at(instr_idx))) << "        " << "- 0x" << std::hex << std::setw(8) << std::setfill('0') << event_num << endl;
                            found_start_signal_epoch = true;
                        }
                    } else {
                        uint event_num = dram_events.second.at(event_idx);
                        event_idx++;
                        log_assert(instr_idx < num_instructions, "instr_idx out of range");
                        *(output_streams.at(dram_all_instructions.at(instr_idx))) << "        " << "- 0x" << std::hex << std::setw(8) << std::setfill('0') << event_num << endl;
                        if (event_num == PerfValLast) {
                            instr_idx++;
                            if (instr_idx == num_instructions) {
                                event_idx += num_events_per_thread[thread_idx] - thread_event_idx;
                                break;
                            } else if (thread_event_idx == num_events_per_thread[thread_idx]) {
                                break;
                            } else {
                                uint smallest_aligned_event = find_beginning_of_next_l1_dump(thread_event_idx, num_events_per_thread[thread_idx], perf_desc, thread_idx);
                                log_debug(tt::LogPerfPostProcess, "Skipping to beginning of next dump by number of lines: {}", smallest_aligned_event - thread_event_idx);
                                event_idx += smallest_aligned_event - thread_event_idx;
                                thread_event_idx = smallest_aligned_event;
                            }
                            found_start_signal_epoch = false;
                        }
                    }
                }
                for (int epoch_with_no_dump = instr_idx; epoch_with_no_dump < num_instructions; epoch_with_no_dump++) {
                    if (thread_event_idx >= num_events_per_thread[thread_idx]) {
                        *(output_streams.at(dram_all_instructions.at(epoch_with_no_dump))) << "        " << "- 0x" << std::hex << std::setw(8) << std::setfill('0') << PerfOutOfMem << endl;
                    }
                }
                // Because ncrisc dumps 2KB chunks of perf buffers and also because of 32B alignment, there are paddings between threads and cores that ncrisc skips.
                if (thread_idx < postprocess::thread_names.size() - 1) {
                    uint num_padded_events = get_num_events_to_skip_between_threads(soc_descriptor, dram_core_coord, perf_desc, thread_idx);
                    log_debug(tt::LogPerfPostProcess, "Skipping {} number of padded events between worker threads", num_padded_events);
                    event_idx += num_padded_events;
                } else {
                    uint num_padded_events = get_num_events_to_skip_between_workers(soc_descriptor, dram_core_coord, perf_desc);
                    log_debug(tt::LogPerfPostProcess, "Skipping {} number of padded events between worker cores", num_padded_events);
                    event_idx += num_padded_events;
                }
            }
        }
        skip_to_next_dram_core = true;
    }
    return device_intermed_dumps;
}

void parse_intermed_dump_and_create_reports(stringstream &intermed_dump, InstructionInfo &instr, const PerfState &perf_state, bool versim) {
    ofstream output_log(instr.output_dir_path + "/output_log.txt");
    output_log << "Reading perf events from " << instr.intermed_dump_key << endl;
    output_log << "Dumping cores_to_ops_map into " << instr.output_dir_path + cores_to_ops_json_name << endl;
    const PerfDesc &perf_desc = perf_state.get_perf_desc();
    const tt_digraph &graph = perf_state.get_graph(instr.graph_name);
    const unordered_map<string, PostprocessModelDesc> &op_to_perf_model_desc = perf_state.get_all_perf_model_desc();
    const unordered_map<string, core_descriptor> &cores_to_ops = perf_state.get_core_to_desc_map(graph.my_graph_info.name);

    string current_line;
    core_events current_core;
    thread_events current_thread;
    vector<core_events> all_core_events;
    vector<string> all_core_ids;
    // If this flag is set, the parser will ignore lines until it finds a core-id.
    bool skip_to_next_core = true;
    // If this flag is set, the parser will ignore lines until it finds a thread-id.
    bool skip_to_next_thread = false;
    // If this flag is set, the parser expects PerfValFirst event.
    bool is_first_event_in_thread = true;
    string core_id_str;
    vector<uint32_t> current_thread_events;
    int event_idx_within_thread = 0;
    int line_idx = 0;

    // The parser below keeps reading all the lines in the device output.
    // This parser can operate in three different modes, which changes based on skip_to_next_core and skip_to_next_thread flags.
    // 1- skip_to_next_core = true, skip_to_next_thread = false
    //      Parser will keep ignoring lines until it finds a core-id.
    //      Then it initially sets up the new-core in process_new_core and sets skip_to_next_core = false, and skip_to_next_thread = true.
    //          There is a special case where the op-name is not found in cores_to_ops (meaning no ops are assigned to this core), in this case we won't change the flags,
    //          and the parser will ignore the entire dump for this core, and ignores lines to find the next core.
    //
    // 2- skip_to_next_core = false, skip_to_next_thread = true
    //      In this mode, parser searches for a thread-id.
    //      When it finds one, it sets up the new-thread in process_new_thread first.
    //      Sets the is_first_event_in_thread flag to indicate the next event we read must be PerfValFirst.
    //      Resets event_idx_within_thread which counts the number of events we have read in the current thread.
    //
    // 3- skip_to_next_core = false, skip_to_next_thread = false
    //      In this mode, the parser will keep pushing events into current_thread_events until either
    //          a) It sees the PerfValLast event. In this case it signals completion of thread.
    //          b) It reads a new thread-id or core-id. In this situation it processes the remaining events in vector for previous thread/core and starts working on the new thread/core.
    //
    // process_thread_end:
    //      This function wraps up one thread and adjusts the skip_to_next_core, and skip_to_next_thread signals for parser to parse either next thread or next core (in case we
    //      have processed all threads in the current core).
    //      The wrapping up of the thread includes:
    //      1- Inserting the current thread into the current core list of threads
    //      2- Pushing the current core into the vector of all the cores if all threads have been processed.
    while (std::getline(intermed_dump, current_line)) {
        line_idx++;
        // Only one of these flags must be set at any time.
        log_assert(!(skip_to_next_core && skip_to_next_thread), "cannot set skip_to_next_core and skip_to_next_thread simultaneously");
        // If skip_to_next_core flag is set, skip until finding a core-id.
        if (skip_to_next_core && !is_core_id(current_line)) {
            continue;
        }
        // If skip_to_next_thread flag is set, skip until finding a thread-id.
        if (skip_to_next_thread && !is_thread_id(current_line)) {
            continue;
        }

        // If a new core is found try to find the op-name assigned to this core in cores_to_ops map.
        // If op was found, turn skip_to_next_thread on and skip_to_next_core off to find the first thread in this core.
        // If op was not found, do not change these flags, to skip all lines until finding the next core.
        if (is_core_id(current_line)) {
            // If parser sees a core-id when skip_to_next_core flag is not set, it means the previous thread did not have the end signal.
            if (!current_thread_events.empty()) {
                perf_warning(perf_desc, tt::LogPerfPostProcess, "Last signal in thread not found");
                process_thread_end(
                    current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, false, false);
            }
            process_new_core(current_core, current_line, cores_to_ops, output_log, skip_to_next_core, skip_to_next_thread);
            core_id_str = current_line;
            core_id_str.pop_back(); // The last character in current_line for core-id is a ':'.
            continue;
        }

        // If a new thread is found, set the thread_id member of the current_thread obj and turn skip_to_next_thread off to start pushing events in current_thread_events.
        // Set the is_first_event_in_thread flag to look for PerfValFirst.
        if (is_thread_id(current_line)) {
            // If parser sees a thread-id when skip_to_next_thread flag is not set, it means the previous thread did not have the end signal.
            if (!current_thread_events.empty()) {
                perf_warning(perf_desc, tt::LogPerfPostProcess, "Last signal in thread not found");
                process_thread_end(
                    current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, false, false);
            }
            process_new_thread(current_thread, current_line, skip_to_next_thread);
            is_first_event_in_thread = true;
            event_idx_within_thread = 0;
            continue;
        }

        // The third mode of the parser. We keep pushing all events for the current thread in current_thread_events, to later process all of them.
        if (!skip_to_next_thread && !skip_to_next_core) {
            uint event_val = convert_string_line_to_hex(current_line);
            event_idx_within_thread++;
            // If is_first_event_in_thread is set the first event we see must be PerfValFirst. If not, skip this thread.
            // Hardware will always record PerfValFirst as its first thread. If we don't find that, it means this thread was not active.
            if (is_first_event_in_thread) {
                if (event_val != PerfValFirst) {
                    // If first event was not PerfValFirst skip this thread.
                    process_thread_end(
                        current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, true, event_val == PerfOutOfMem);
                } else {
                    // If first event was PerfValFirst, set is_first_event_in_thread to false to start pushing events in current_thread_events.
                    is_first_event_in_thread = false;
                }
            } else {
                if (event_val == PerfOutOfMem) {
                    process_thread_end(
                        current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, true, true);
                    continue;
                }
                // The first four events in the math thread should be skipped.
                if (current_thread.thread_type == ThreadType::MATH and event_idx_within_thread <= 4) {
                    continue;
                }
                // The first two events in the unpacker/packer/brisc threads should be skipped.
                // If event val is 0xffffffff, skip
                if ((current_thread.thread_type == ThreadType::UNPACK or current_thread.thread_type == ThreadType::PACK or current_thread.thread_type == ThreadType::BRISC) 
                    and (event_idx_within_thread <= 2 or event_val == 0xffffffff)) {
                    continue;
                }

                // If at any point after finding PerfValFirst, the parser sees a PerfValLast event value, it finishes the process for this thread.
                // Hardware will always push a PerfValLast at the end of each thread.
                // If not pushed, it means that thread has run out of memory.
                // The reverse is not always valid, meaning if we do have PerfValLast it does not indicate the hardware has not run out of memory.
                // In some cases the thread always reserves one event at the end for PerfValLast.
                if (event_val == PerfValLast) {
                    process_thread_end(
                        current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, true, false);
                } else {
                    current_thread_events.push_back(event_val);
                }
            }
        }
    }
    // If there are no more lines for parser to read and the last signal has not been found process the remaining events in the vector.
    if (!current_thread_events.empty()) {
        perf_warning(perf_desc, tt::LogPerfPostProcess, "Last signal in thread not found");
        process_thread_end(
            current_thread, current_core, all_core_events, all_core_ids, current_thread_events, core_id_str, perf_desc, skip_to_next_core, skip_to_next_thread, false, false);
    }
    json dump_events_aligned = create_postprocess_report(all_core_events, all_core_ids, instr, perf_desc, perf_state.get_device_alignment_info(), graph, op_to_perf_model_desc, true, versim);

    pair<uint64_t, uint64_t> largest_delay_start_and_end = get_epoch_q_empty_largest_delay(dump_events_aligned);

    instr.largest_wait_for_epoch_binary_cycles = largest_delay_start_and_end.second - largest_delay_start_and_end.first;

    ofstream output_file(instr.output_dir_path + "/" + postprocess_json_name);
    output_log << "Writing the perf results in " << instr.output_dir_path + "/" + postprocess_json_name << endl;
    log_debug(tt::LogPerfPostProcess, "Writing the perf results in {} /{}", instr.output_dir_path, postprocess_json_name);
    output_file << std::setw(4) << dump_events_aligned;
    output_file.flush();
    output_file.close();

    if (perf_desc.generate_original_report) {
        json dump_events = create_postprocess_report(all_core_events, all_core_ids, instr, perf_desc, perf_state.get_device_alignment_info(), graph, op_to_perf_model_desc, false, versim);
        ofstream output_file(instr.output_dir_path + "/" + postprocess_original_json_name);
        output_log << "Writing the perf results in " << instr.output_dir_path + "/" + postprocess_original_json_name << endl;
        log_debug(tt::LogPerfPostProcess, "Writing the perf results in {} /{}", instr.output_dir_path, postprocess_original_json_name);
        output_file << std::setw(4) << dump_events;
        output_file.flush();
        output_file.close();
    }

    json runtime_table = create_runtime_report(dump_events_aligned);
    ofstream output_runtime_table(instr.output_dir_path + runtime_table_json_name);
    output_runtime_table << std::setw(4) << runtime_table;
    output_runtime_table.flush();
    output_runtime_table.close();
    output_log.close();
    create_op_report(runtime_table, instr.output_dir_path, perf_desc);
    create_runtime_table(runtime_table, instr.output_dir_path);
}

// For each device, we will extract all core events its perf buffers in dram 
// then, we will create divide the raw dram perf dumps into intermediate dumps
// intermediate dumps will be created for each instruction, and contain information about events of each worker core for that instruction
// finally, we will parse the intermediate dumps and create reports (perf_postprocess.json, runtime_table.json etc.) for each instruction
void process_all_instructions_and_create_reports(
    vector<InstructionInfo> &all_instructions,
    const map<tt_cxy_pair, vector<uint32_t>> &all_dram_events,
    const PerfState &perf_state,
    const std::unordered_map<chip_id_t, buda_SocDescriptor>& sdesc_per_chip,
    bool versim
) {
    log_debug(tt::LogPerfPostProcess, "Extracting intermediate yamls from dram dump.");
    log_info(tt::LogPerfPostProcess, "Processing all instructions...");
    for (const auto &[device_id, device_start_cycle] : perf_state.get_device_alignment_info().device_id_to_start_cycle) {
        vector<int> instruction_idxs;
        vector<InstructionInfo*> device_instructions;
        for (int instr_idx = 0; instr_idx < all_instructions.size(); instr_idx++) {
            InstructionInfo &instr = all_instructions.at(instr_idx);
            if (instr.device_id != device_id) {
                continue;
            }
            instruction_idxs.push_back(instr_idx);
            device_instructions.push_back(&instr);
        }
        log_assert(sdesc_per_chip.find(device_id) != sdesc_per_chip.end(), "Could not find soc descriptor for device id {}", device_id);
        std::unordered_map<string, std::shared_ptr<stringstream>> device_intermed_dumps = extract_core_events_from_device_dram_dump(device_instructions, all_dram_events, perf_state.get_perf_desc(), &sdesc_per_chip.at(device_id));
        for (int device_instr_idx = 0; device_instr_idx < device_instructions.size(); device_instr_idx++) {
            InstructionInfo *instr = device_instructions.at(device_instr_idx);
            log_assert(instr->intermed_dump_key  != "", "All intermediate yaml files must be dumped.");
            log_assert(instr->output_dir_path    != "", "All output directory paths must have been set.");
            log_assert(device_intermed_dumps.find(instr->intermed_dump_key) != device_intermed_dumps.end(), "Could not find intermed dump for instruction {}", instr->intermed_dump_key);
            parse_intermed_dump_and_create_reports(*(device_intermed_dumps.at(instr->intermed_dump_key)), *instr, perf_state, versim);
            log_info(tt::LogPerfPostProcess, "Finished perf-postprocess for program {}, graph {}, epoch {}/{}", instr->program_name, instr->graph_name, instruction_idxs.at(device_instr_idx), all_instructions.size() - 1);
        }
    }
}

vector<EpochPerfInfo> get_epoch_info(const vector<InstructionInfo> &all_instructions_info) {

    vector<EpochPerfInfo> all_epoch_perf_info;
    for (const auto &instr: all_instructions_info) {
        tt::EpochPerfInfo epoch_info = {
            .program_name = instr.program_name,
            .graph_name = instr.graph_name,
            .program_id = instr.program_id,
            .graph_id = instr.instr_id_local,
            .global_epoch_id = instr.instr_id_global,
            .perf_output_directory = instr.output_dir_path,
            .aiclk = instr.aiclk,
            .device_id = instr.device_id,
            .input_count = instr.input_count,
            .num_epochs_current_program = instr.num_instr_current_program,
            .first_and_last_inputs_recorded = instr.first_and_last_inputs_recorded,
            .num_cycles_per_input = instr.num_cycles_per_input,
            .num_inputs_per_second = instr.num_inputs_per_second,
            .steady_state_first_and_last_inputs = instr.steady_state_first_and_last_inputs,
            .num_cycles_per_input_steady_state = instr.num_cycles_per_input_steady_state,
            .num_inputs_per_second_steady_state = instr.num_inputs_per_second_steady_state,
            .last_recorded_input_execution_cycle = instr.last_recorded_input_execution_cycle,
            .largest_wait_for_epoch_binary_cycles = instr.largest_wait_for_epoch_binary_cycles,
            .first_unpack_first_input = instr.first_unpack_first_input,
            .last_pack_first_input = instr.last_pack_first_input,
            .last_pack_last_input = instr.last_pack_last_input};
        
        all_epoch_perf_info.push_back(epoch_info);
    }
    return all_epoch_perf_info;
}

void dump_perf_graphviz(const boost::adjacency_list<boost::listS, boost::vecS, boost::bidirectionalS, tt_digraph_node_struct, tt_digraph_edge_struct> &graph, const string &path) {
    ofstream outfile;
    outfile.open(path);
    boost::write_graphviz(
        outfile,
        graph,
        [&](auto& out, auto v) { out << "[label=\"" << graph[v].name << "\"] " << "[is_queue=\"" << !graph[v].op_not_queue << "\"]"; },
        [&](auto& out, auto e) { out << "[label=\"" << graph[e].input_index << "\"]"; });
    outfile.close();
}

void generate_graphs(const vector<InstructionInfo> &all_instructions) {
    for (const InstructionInfo &instr: all_instructions) {
        log_assert(instr.output_dir_path != "", "All output directory paths must have been set.");
        string path_to_graph = instr.output_dir_path + "/" + "perf_graph_" + instr.graph_name + ".dot";
        dump_perf_graphviz(instr.netlist_graph.graph, path_to_graph);
    }
}

uint64_t convert_device_to_host_timestamp(const uint64_t &device_timestamp_rebiased, const PerfState &perf_state, const uint &device_id) {
    const tt_perf_device_alignment &device_alignment_info = perf_state.get_device_alignment_info();
    log_assert(device_alignment_info.device_id_to_host_start_cycle.find(device_id) != device_alignment_info.device_id_to_host_start_cycle.end(), "host start cycle must be populated for every device");
    log_assert(device_alignment_info.device_id_to_host_end_cycle.find(device_id) != device_alignment_info.device_id_to_host_end_cycle.end(), "host end cycle must be populated for every device");
    log_assert(device_alignment_info.device_id_to_start_cycle.find(device_id) != device_alignment_info.device_id_to_start_cycle.end(), "device start cycle must be populated for every device");
    log_assert(device_alignment_info.device_id_to_end_cycle.find(device_id) != device_alignment_info.device_id_to_end_cycle.end(), "device end cycle must be populated for every device");
    log_assert(device_alignment_info.device_id_to_host_end_cycle.at(device_id) > 0, "host end cycle must be greater than 0");
    log_assert(device_alignment_info.device_id_to_end_cycle.at(device_id) > 0, "device end cycle must be greater than 0");

    const uint64_t &host_start = device_alignment_info.device_id_to_host_start_cycle.at(device_id);
    const uint64_t &host_end = device_alignment_info.device_id_to_host_end_cycle.at(device_id);
    const uint64_t &device_start = device_alignment_info.device_id_to_start_cycle.at(device_id);
    const uint64_t &device_end = device_alignment_info.device_id_to_end_cycle.at(device_id);
    // device_timestamp_rebiased is already the actual timestamp read from the device subtracted by the device start cycle
    uint64_t converted_val = host_start + (double(host_end - host_start) * double(device_timestamp_rebiased) / double(device_end - device_start));
    return converted_val;
}

host_global_events populate_host_global_events(const thread_events& thread) {
    host_global_events global_events;
    for (const auto& event_ref: thread.events) {
        uint event_id = event_ref.first;
        HostEventProperties event_properties(event_id);
        if (event_properties.event_type == uint(HostEventType::RUN_PROGRAM)) {
            for (const auto& each_event: event_ref.second) {
                if (global_events.first_program_start == ULLONG_MAX or
                    global_events.first_program_start > each_event.first_val) {
                        global_events.first_program_start = each_event.first_val;
                    }
            }
        }
        if (event_properties.event_type == uint(HostEventType::POP_OUTPUT)) {
            for (const auto& each_event: event_ref.second) {
                if (global_events.last_output_pop_end == ULLONG_MAX or
                    global_events.last_output_pop_end < each_event.second_val) {
                        global_events.last_output_pop_end = each_event.second_val;
                    }
            }
        }
    }
    return global_events;
}

void append_epoch_runtime_to_host_profiler(const PerfState &perf_state) {
    if (!perf_state.is_postprocessor_executed()) {
        log_error("Epoch runtime can only be populated after the postprocessor has finished");
        return;
    }
    for (const EpochPerfInfo &epoch_info: perf_state.get_all_epochs_perf_info()) {
        if (epoch_info.first_and_last_inputs_recorded.first != 0) {
            log_warning(tt::LogPerfPostProcess, 
                "Not adding epoch runtime to host profiler report. Epoch runtime can only be added to host profiler report if the first and last input of each epoch has been recorded on device. Inputs recorded: first {}, last {}.",
                epoch_info.first_and_last_inputs_recorded.first, epoch_info.first_and_last_inputs_recorded.second);
            return;
        }
        if (epoch_info.first_and_last_inputs_recorded.second != epoch_info.input_count - 1) {
            log_warning(tt::LogPerfPostProcess, 
                "Not adding epoch runtime to host profiler report. Epoch runtime can only be added to host profiler report if the first and last input of each epoch has been recorded on device. Inputs recorded: first {}, last {}.",
                epoch_info.first_and_last_inputs_recorded.first, epoch_info.first_and_last_inputs_recorded.second);
            return;
        }

        uint program_id = epoch_info.program_id;
        uint local_epoch_id = epoch_info.graph_id;
        
        uint64_t event_id = get_event_id(perf::HostEventType::DEVICE_EPOCH_FIRST_UNPACK_LAST_PACK, epoch_info.device_id, local_epoch_id, program_id);
        uint64_t epoch_start_time = convert_device_to_host_timestamp(epoch_info.first_unpack_first_input, perf_state, epoch_info.device_id);
        uint64_t epoch_end_time = convert_device_to_host_timestamp(epoch_info.last_pack_last_input, perf_state, epoch_info.device_id);
        
        backend_profiler.record_loader_event(event_id, epoch_start_time);
        backend_profiler.record_loader_event(event_id, epoch_end_time);
    }
}

// perf postprocessor:
// 1 - Reads and parses the dram perf dump and extracts per-graph performance data
// 2 - Organizes graph perf data based on the program mode
// 3 - Analyses the perf dump and generates a json file containing detailed information on each
//     core and each thread separately.
// 4 - Analyses the per-thread data and calculates per-core events such as, runtime, math-util, ...
bool run_perf_postprocess(const map<tt_cxy_pair, vector<uint32_t>> &all_dram_events, PerfState &perf_state, const string& output_dir, std::shared_ptr<tt_device> device, const std::unordered_map<chip_id_t, buda_SocDescriptor> &sdesc_per_chip) {
    PROFILE_SCOPE_MS();
    const bool versim = (perf_state.get_target_device_type() == tt::TargetDevice::Versim);
    bool perf_check_passed = true;
    perf::ScopedEventProfiler profile(perf::HostEventType::PERF_POSTPROCESSOR);
    log_info(tt::LogPerfPostProcess, "Starting perf postprocessor...");
    const PerfDesc &perf_desc = perf_state.get_perf_desc();
    string perf_out_dir = get_device_perf_out_directory(output_dir, perf_desc, false);
    log_info(tt::LogPerfPostProcess, "Setting perf postprocess output directory to {}", perf_out_dir);
    log_debug(tt::LogPerfPostProcess, "All instructions executed:");
    
    // Take the instructions recorded in perf_state and create a vector of InstructionInfo which includes all instructions executed on all devices
    vector<InstructionInfo> all_instructions_info = populate_all_instructions(perf_state, perf_out_dir, device);

    // Based on the perf view mode, populate the output directory names for each instruction
    populate_output_directory_paths(all_instructions_info, perf_desc, output_dir);

    // Process all dram events, extract events and timestamps and assign them to each instruction, and finally create reports for each instruction
    process_all_instructions_and_create_reports(all_instructions_info, all_dram_events, perf_state, sdesc_per_chip, versim);

    // Generate graph dumps of instructions in the same directory as the postprocess and runtime json files
    generate_graphs(all_instructions_info);

    perf_state.update_all_epochs_info(get_epoch_info(all_instructions_info));

    if (perf_desc.dump_perf_vcd) {
        gen_vcd_from_perf_dump(all_instructions_info, perf_out_dir, perf_desc);
    }
    perf_state.set_postprocessor_executed();
    log_info(tt::LogPerfPostProcess, "Finished perf postprocessor successfully");
    print_perf_info_all_epochs(output_dir, perf_state, true);
    if (perf_desc.append_device_runtime_to_host_report) {
        append_epoch_runtime_to_host_profiler(perf_state);
    }
    if (!perf_desc.comparison_configs.empty()) {
        perf_check_passed = postprocess::check_performance(all_instructions_info, perf_desc);
    }
    return perf_check_passed;
}

void process_host_profiler_entry(uint64_t event_id, uint64_t event_val, thread_events &current_thread, uint process_id, uint thread_id, const vector<string> &all_labels) {
    auto event_ref = current_thread.events.find(event_id);
    device_perf_event current_event = {.id = event_id, .first_val = event_val};
    current_event.description = decode_host_event_name(event_id, process_id, thread_id, all_labels);
    if (event_ref == current_thread.events.end()) {
        
        current_thread.events.insert({event_id, {current_event}});
    } else {
        if (event_ref->second.back().second_val == ULLONG_MAX) {
            event_ref->second.back().second_val = event_val;
        } else {
            event_ref->second.push_back(current_event);
        }
    }
}

void run_host_perf_postprocess(
    const vector<uint64_t> &target_buffer,
    const vector<string> &all_labels,
    const string &test_output_dir,
    const string &perf_output_dir,
    const pid_t &process_id,
    const bool &dump_device_alignment,
    const int &total_input_count) {
        
    uint thread_id = get_thread_id();
    string postprocess_report_path = get_host_profiler_report_path(
            test_output_dir, perf_output_dir, uint(process_id), thread_id, false, dump_device_alignment);
    // In order to avoid running postprocessor a second time from finish_child_process is called from pybuda
    // after runtime.finish() api, only run postprocessor if the report file has not already been generated
    if (fs::exists(postprocess_report_path)) {
        return;
    }
    if (dump_device_alignment) {
        log_debug(tt::LogPerfPostProcess, "Starting device alignment postprocessor for process_id {} thread_id {}", uint(process_id), thread_id);
    } else {
        log_info(tt::LogPerfPostProcess, "Starting backend profiler postprocessor for process_id {} thread_id {}", uint(process_id), thread_id);
    }
    int is_event_id = true;
    thread_events current_thread;

    log_assert(target_buffer.size() % 2 == 0, "For each event, we must have recorded one event-id and one value");
    for (int i = 0; i < target_buffer.size(); i += 2) {
        uint64_t event_id = target_buffer.at(i);
        uint64_t event_val = target_buffer.at(i+1);
        process_host_profiler_entry(event_id, event_val, current_thread, uint(process_id), thread_id, all_labels); 
    }
    check_end_time_recorded_host(current_thread);
    host_global_events global_events = populate_host_global_events(current_thread);
    json postprocess_report = create_host_postprocess_report(current_thread, global_events, postprocess_report_path, total_input_count, dump_device_alignment);
    string host_summary_json_path = get_host_perf_out_directory(test_output_dir, perf_output_dir, false) + "/" + host_summary_json_name;
    create_host_summary_postprocess_report(host_summary_json_path, postprocess_report);
    if (dump_device_alignment) {
        log_debug(tt::LogPerfPostProcess, "Finished device alignment postprocessor for process {}, thread {}.", process_id, thread_id);
    } else {
        log_info(tt::LogPerfPostProcess, "Finished backend profiler postprocessor for process {}, thread {}.", process_id, thread_id);
    }
}

thread_events run_postprocessor_single_thread_epoch(PerfDumpHeader header, vector<uint32_t> perf_data) {
    // uint64_t start = perf::get_timestamp();

    // log_debug(tt::LogPerfPostProcess, "Starting perf postprocess on a single perf thread dump with header:");
    // log_debug(tt::LogPerfPostProcess, "{}", get_perf_dump_header_ss(header).str());
    thread_events current_thread;
    current_thread.thread_type = ThreadType(header.thread_id);
    current_thread.header = header;
    log_assert(perf_data.at(0) == valid_thread_dump_start_id, "Each thread dump must start with value {}", valid_thread_dump_start_id);
    log_assert(perf_data.back() == thread_dump_end_id, "Each thread dump must end with value {}", thread_dump_end_id);

    perf_data.erase(perf_data.begin()); // erase the valid thread start signal
    perf_data.erase(perf_data.begin()); // erase the header
    perf_data.pop_back(); // erase the valid thread end signal


    // log_debug(tt::LogPerfPostProcess, "run_postprocessor_single_thread_epoch: Final thread dump: {}", perf::get_thread_dump_partial_ss(perf_data).str());
    process_thread_main(perf_data, current_thread, true);
    if (header.thread_id == 3) {
        combine_ncrisc_events(current_thread);
    }
    log_debug(tt::LogPerfPostProcess, "Finished perf postprocess on the single perf thread dump.");
    
    // uint64_t end = perf::get_timestamp();
    // log_info(tt::LogPerfInfra, "Total runtime process_thread_main = {}", end - start);

    return current_thread;
}

const void PerfHostScratchQueue::print() const {
    log_debug(tt::LogPerfInfra, "       Single channel perf dump queue on host mmio region:");
    std::stringstream ss;
    ss << "         base_ptr = " << base_ptr << endl;
    ss << "         num_slots = " << num_slots << endl;
    ss << "         slot_size_bytes = " << slot_size_bytes << endl;
    ss << "         cores with host ptrs = " << core_with_host_ptrs.str();
    log_debug(tt::LogPerfInfra, "{}", ss.str());
}

PerfHostScratchBuffer::PerfHostScratchBuffer(
    const std::unordered_map<chip_id_t, buda_SocDescriptor> chip_id_to_sdesc,
    std::unordered_map<uint, set<uint> > mmio_device_to_mapped_devices,
    PerfHostPostprocessQueue *postprocessor_queue,
    std::unordered_map<uint, uint32_t*> mmio_device_to_perf_scratch_buffer_base_addr,
    uint32_t single_thread_dump_size,
    std::function<void(tt_cxy_pair, vector<uint32_t>)> update_host_queue_ptr):
        num_devices(chip_id_to_sdesc.size()),
        mmio_device_to_mapped_devices(mmio_device_to_mapped_devices),
        postprocessor_queue(postprocessor_queue),
        mmio_device_to_perf_scratch_buffer_base_addr(mmio_device_to_perf_scratch_buffer_base_addr),
        single_thread_dump_size(single_thread_dump_size),
        update_host_queue_ptr(update_host_queue_ptr) {
    
    check_valid_config();
    if (testing_without_silicon) {
        log_warning(tt::LogPerfInfra, "Running concurrent perf trace with testing_without_silicon mode enabled");
    }
    log_assert(chip_id_to_sdesc.size() > 0, "Host scratch buffer must be initialized with at least one device");
    log_assert(chip_id_to_sdesc.size() <= 64, "Currently the maximum number of chips supported for concurrent perf dump is 64");
    
    std::unordered_map<uint, uint> mmio_device_to_dram_channel_id;
    uint global_queue_id = 0;
    slot_size_bytes = host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP * single_thread_dump_size;

    for (const auto &mmio_it: mmio_device_to_mapped_devices) {
        initialize_for_new_mmio_device(mmio_it.first);
    }

    for (const auto& mmio_it: mmio_device_to_mapped_devices) {
        uint mapped_chip_id = mmio_it.first;
        log_assert(mmio_device_to_perf_scratch_buffer_base_addr.find(mapped_chip_id) != mmio_device_to_perf_scratch_buffer_base_addr.end(),
                "Host perf scratch buffer base address is not populated for device {}", mapped_chip_id);
        for (uint device_id: mmio_it.second) {
            const buda_SocDescriptor &sdesc = chip_id_to_sdesc.at(device_id);

            mmio_device_to_total_num_dram_channels.at(mapped_chip_id) += sdesc.dram_cores.size();
            if(mmio_device_to_dram_channel_id.find(mapped_chip_id) == mmio_device_to_dram_channel_id.end()) {
                mmio_device_to_dram_channel_id.insert({mapped_chip_id, 0});
            }
            
            log_warning(tt::LogPerfInfra, "Not all dram cores have been used for perf dump");
            // log_assert(sdesc.get_perf_dram_bank_to_workers().size() == sdesc.dram_cores.size(), "All dram cores must be used for perf dump");
            for (const auto& dram_to_worker: sdesc.get_perf_dram_bank_to_workers()) {
                log_assert(dram_to_worker.second.size() > 0, "At least one worker core must have been assigned to each dram channel for perf dump");
                tt_cxy_pair core_with_host_ptrs = tt_cxy_pair(device_id, dram_to_worker.second.at(0));
                log_debug(tt::LogPerfInfra, "ptr core for device {}, dram {}, is {}", device_id, dram_to_worker.first.str(), core_with_host_ptrs.str());
                
                uint32_t* base_addr = mmio_device_to_perf_scratch_buffer_base_addr.at(mapped_chip_id) + mmio_device_to_dram_channel_id.at(mapped_chip_id) * (get_queue_size_bytes() / sizeof(uint32_t));
                mmio_device_to_device_dump_queues.at(mapped_chip_id).push_back(PerfHostScratchQueue(base_addr, get_queue_num_slots(), slot_size_bytes, device_id, mapped_chip_id,
                    mmio_device_to_dram_channel_id.at(mapped_chip_id), global_queue_id, core_with_host_ptrs));
                mmio_device_to_dram_channel_id.at(mapped_chip_id)++;
                global_queue_id++;
            }
        }
    }
    
    num_mmio_devices = mmio_device_to_mapped_devices.size();

    log_assert(num_mmio_devices == mmio_device_to_total_num_dram_channels.size(), "mmio maps not constructed properly. Expected num mmio {}, observed {}", num_mmio_devices, mmio_device_to_total_num_dram_channels.size());
    log_assert(num_mmio_devices == mmio_device_to_device_dump_queues.size(), "mmio maps not constructed properly. Expected num mmio {}, observed {}", num_mmio_devices, mmio_device_to_device_dump_queues.size());
    for (const auto& mmio_it: mmio_device_to_total_num_dram_channels) {
        log_assert(mmio_it.second <= host_mem::address_map::NUM_HOST_PERF_QUEUES,
                    "For real-time perf dump, the total number of dram channels in test cannot exceed {}",
                    host_mem::address_map::NUM_HOST_PERF_QUEUES);
    }
    
    log_info(tt::LogPerfInfra, "PerfHostScratchBuffer: Finished setting up the PerfHostScratchBuffer with the following parameters:");
    print();
}

void PerfHostScratchBuffer::check_all_queues_for_new_dump() {
    for (auto &queue_it: mmio_device_to_device_dump_queues) {
        for (PerfHostScratchQueue &queue: queue_it.second) {
            while (queue.check_new_entry()) {
                uint64_t copy_start = perf::get_timestamp();
                const uint32_t* current_entry_addr = queue.get_current_entry_addr();
                log_debug(tt::LogPerfInfra, "PerfHostScratchBuffer: Found a new entry in host scratch global_queue_id {}, local_queue_id {}, device_id {}, mmio_device_id {}, at rd_ptr: {}, wr_ptr: {}, addr: {}",
                            queue.global_queue_id, queue.local_queue_id, queue.device_id, queue.mmio_device_id, queue.ptr.get_rd_ptr(), queue.ptr.get_wr_ptr(), (void*)current_entry_addr);
                total_num_traces_received++;
                queue.total_num_traces_received++;
                
                queue.update_wr_ptr();
                postprocessor_queue->push_new_device_perf_dump(current_entry_addr);
                queue.free_last_queue_entry();
                uint64_t ptr_update_start = perf::get_timestamp();
                if (!testing_without_silicon) {
                    log_debug(tt::LogPerfInfra, "PerfHostScratchBuffer: Updating rd ptr {}, for core {}", queue.ptr.rd_ptr, queue.core_with_host_ptrs.str());
                    vector<uint32_t> rd_ptr_vec;
                    rd_ptr_vec.push_back(queue.ptr.rd_ptr);
                    update_host_queue_ptr(queue.core_with_host_ptrs, rd_ptr_vec);
                }
                log_debug(tt::LogPerfInfra, "PerfHostScratchBuffer: Pushed new entry to postprocessor queue. Updated global_queue_id {}, local_queue_id {}, device_id {}, mmio_device_id {}, at rd_ptr: {}, wr_ptr: {}, addr: {}",
                            queue.global_queue_id, queue.local_queue_id, queue.device_id, queue.mmio_device_id, queue.ptr.get_rd_ptr(), queue.ptr.get_wr_ptr(), (void*)current_entry_addr);

                uint64_t copy_end = perf::get_timestamp();
                total_time_copying_perf_traces += copy_end - copy_start;
                total_time_updating_header += copy_end - ptr_update_start;
            }
        }
    }
}

void PerfHostScratchBuffer::spin_on_all_queues() {
    log_info(tt::LogPerfInfra, "Starting host scratch buffer perf trace query on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    while (active) {
        check_all_queues_for_new_dump();
    }
    // Query one last time before exiting
    check_all_queues_for_new_dump();
    log_info(tt::LogPerfInfra, "Stopping host scratch buffer perf trace query on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
}

PerfHostScratchBuffer::~PerfHostScratchBuffer() {
    if (active) {
        log_assert(host_scratch_buffer_thread.joinable(), "Perf Postprocessor queue query must be active if active is true");
        active = false;
        host_scratch_buffer_thread.join();
    } else {
        log_assert(!host_scratch_buffer_thread.joinable(), "Perf Postprocessor queue query must not be active if active is false");
    }
    // delete [] perf_scratch_buffer_base_addr;
    // delete postprocessor_queue;
}

void PerfHostScratchBuffer::start_device_trace_poll() {
    log_assert(!active, "perf host scratch buffer query should not be enabled more than once. pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    active = true;
    host_scratch_buffer_thread = std::thread([&] {
        try {
            spin_on_all_queues();
        } catch (std::exception &e) {
            std::rethrow_exception(std::current_exception());
        }
    });
    log_assert(postprocessor_queue, "postprocessor queue is uninitialized");
    try {
        postprocessor_queue->start_runtime_postprocessor();
    } catch (std::exception &e) {
        std::rethrow_exception(std::current_exception());
    }
}

void PerfHostScratchBuffer::stop_device_trace_poll() {
    log_assert(active, "stop_device_trace_poll is called on a thread that was not querying the perf host scratch buffer on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    active = false;
    log_info(tt::LogPerfInfra, "Waiting for performance host scratch buffer query thread to finish on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    host_scratch_buffer_thread.join();
    log_info(tt::LogPerfInfra, "Finished waiting for performance host scratch buffer query thread on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    log_assert(postprocessor_queue, "postprocessor queue is uninitialized");
    postprocessor_queue->stop_runtime_postprocessor();
    print_final_summary();
}

const void PerfHostScratchBuffer::print() const {
    log_info(tt::LogPerfInfra, "PerfHostScratchBuffer: Host performance scratch buffer info: ");
    std::stringstream ss;
    ss << endl;
    ss << "testing_without_silicon = " << testing_without_silicon << endl;
    ss << "total_buffer_size = " << total_size << endl;
    ss << "num_devices = " << num_devices << endl;
    ss << "num_mmio_devices = " << num_mmio_devices << endl;
    ss << "total_num_queues each mmio chip = " << get_num_queues_each_mmio_device() << endl;
    ss << "num_slots_for_each_channel_queue = " << get_queue_num_slots() << endl;
    ss << "channel_queue_size_bytes = " << get_queue_size_bytes() << endl;
    ss << "each_device_dump_size_bytes = " << slot_size_bytes << endl;
    ss << endl;
    for (uint i = 0; i < num_mmio_devices; i++) {
        ss << "mmio device id: " << i << ":" << endl;
        ss << "     devices mapped to this mmio = ";
        for (const auto& device_id: mmio_device_to_mapped_devices.at(i)) {
            ss << device_id << ", ";
        }
        ss << endl;
        ss << "     total_num_dram_channels = " << mmio_device_to_total_num_dram_channels.at(i) << endl;
        for (const auto &queue: mmio_device_to_device_dump_queues.at(i)) {
            queue.print();
        }
    }
    log_info(tt::LogPerfInfra, "{}", ss.str());
}

const void PerfHostScratchBuffer::print_final_summary() const {
    log_info(tt::LogPerfInfra, "Host scratch buffer final summary:");
    log_info(tt::LogPerfInfra, "Total time stalled on postprocessor = {} ns", total_time_stalled_on_postprocessor);
    log_info(tt::LogPerfInfra, "Total number of perf traces received = {}", total_num_traces_received);
    log_info(tt::LogPerfInfra, "Total time copying perf traces = {} ns", total_time_copying_perf_traces);
    log_info(tt::LogPerfInfra, "Total size of perf traces copied = {} B", total_num_traces_received * single_thread_dump_size);
    log_info(tt::LogPerfInfra, "Total bw = {} GB/s", (total_num_traces_received * single_thread_dump_size) / float(total_time_copying_perf_traces));
    log_info(tt::LogPerfInfra, "Total bw excluding queue read pointer update = {} GB/s", (total_num_traces_received * single_thread_dump_size) / float(total_time_copying_perf_traces - total_time_updating_header));
    log_info(tt::LogPerfInfra, "Total time updating queue read pointers on device = {}", total_time_updating_header);
    // log_info(tt::LogPerfInfra, "Total time doing memcpy = {} ns", postprocessor_queue->total_time_memcpy);
    // log_info(tt::LogPerfInfra, "Total bw for memcpy = {} GB/s", (total_num_traces_received * single_thread_dump_size) / float(postprocessor_queue->total_time_memcpy));
    for (const auto& mmio_it: mmio_device_to_device_dump_queues) {
        for (const auto& queue: mmio_it.second) {
            log_debug(tt::LogPerfInfra, "Total number of perf traces received from global_queue_id {}, local_queue_id {}, device_id {}, mmio_device_id {}, with first core {} = {}", 
                        queue.global_queue_id, queue.local_queue_id, queue.device_id, queue.mmio_device_id, queue.core_with_host_ptrs.str(), queue.total_num_traces_received);
        }
    }
    postprocessor_queue->print_final_summary();
}

void PerfHostGenerateReport::generate_all_reports() {    
    log_info(tt::LogPerfInfra, "Starting to generate postprocess reports for all epochs on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    while (!q.empty()) {
        log_debug(tt::LogPerfInfra, "Starting to generate reports for new epoch events at rd_ptr {}, wr_ptr {}", q.get_rd_ptr(), q.get_wr_ptr());

        std::shared_ptr<epoch_events> new_epoch_event = q.front();
        create_postprocess_report_concurrent(new_epoch_event);
        q.check_not_empty_and_pop();
        
        log_debug(tt::LogPerfInfra, "Finished generating reports for new epoch events at rd_ptr {}, wr_ptr {}", q.get_rd_ptr(), q.get_wr_ptr());
    }
    
    vector<EpochPerfInfo> all_epoch_info = get_epoch_info(all_instruction_info);
    push_all_epoch_perf_info(all_epoch_info);
    const PerfDesc &perf_desc = perf_state->get_perf_desc();
    string perf_output_path = get_perf_out_directory(perf_state->get_test_output_dir(), perf_desc.override_perf_output_dir, false);
    string output_path_csv = perf_output_path + perf_info_all_epochs_csv_name;
    string output_path_yaml = perf_output_path + perf_info_all_epochs_yaml_name;

    print_epoch_perf_table(all_epoch_info, output_path_csv, perf_desc.measure_steady_state_perf);
    create_all_epochs_perf_info_report(all_epoch_info, perf_state->get_total_input_count(), output_path_yaml);
}

bool PerfHostGenerateReport::run_concurrent_performance_check() {
    log_assert(perf_state->is_postprocessor_executed(), "Postprocessor must have been executed before the concurrent_performance_check pass");
    bool perf_check_passed = true;
    const PerfDesc perf_desc = perf_state->get_perf_desc();
    if (!perf_desc.comparison_configs.empty()) {
        perf_check_passed = postprocess::check_performance(all_instruction_info, perf_desc);
    }
    return perf_check_passed;
}

void PerfHostGenerateReport::push_new_epoch_events(std::shared_ptr<epoch_events> epoch_event) {
    // if (q.full()) {
    //     uint64_t wait_start = perf::get_timestamp();
    //     log_warning(tt::LogPerfInfra, "Waiting for free space inside the report generator report");
    //     while(q.full()) {}
    //     uint64_t wait_end = perf::get_timestamp();
    //     log_warning(tt::LogPerfInfra, "Finished waiting for free space inside the report generator report after {} ns", wait_end - wait_start);
    // }
    q.push(epoch_event);
}


// Prepares all data structures to use the old create_postprocess_report for the concurrent perf trace
void PerfHostGenerateReport::create_postprocess_report_concurrent(const std::shared_ptr<epoch_events> epoch_event) {
    // FIXME: Add support for device alignment
    bool align_devices = true;

    // uint64_t start = perf::get_timestamp();

    const uint &global_epoch_id = epoch_event->get_global_epoch_id();
    const PerfDesc &perf_desc = perf_state->get_perf_desc();

    const instruction_info_wrapper &instr_wrap = epoch_event->instr_wrap;
    const string &graph_name = instr_wrap.instr->graph_name;
    const tt_digraph &graph = epoch_event->graph_wrapper->graph;
    const uint target_device = graph.my_graph_info.target_device;
    const uint &aiclk = epoch_event->aiclk;
    log_assert(test_output_dir != "", "test_output_dir is uninitialized");
    const string &perf_output_dir = get_device_perf_out_directory(test_output_dir, perf_desc, false);
    log_debug(tt::LogPerfInfra, "Creating postprocess report for epoch with program_name {} graph_name {} local_epoch_id {} global_epoch_id {}",
            instr_wrap.program_name, graph_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id);
    
    InstructionInfo instr(
        int(instr_wrap.program_id), instr_wrap.local_epoch_id, instr_wrap.global_epoch_id, epoch_event->num_epochs_current_program, instr_wrap.program_name,
        graph_name, target_device, graph, perf_output_dir, aiclk, graph.my_graph_info.input_count);
    instr.set_output_dir_path(perf_output_dir);


    vector<string> all_core_ids;
    vector<core_events> all_cores;
    for (const auto &core_it: epoch_event->all_core_events) {
        string core_id_str = to_string(core_it.first.x) + "-" + to_string(core_it.first.y);
        all_core_ids.push_back(core_id_str + "-" + core_it.second.descriptor.op_name);
        all_cores.push_back(core_it.second);
    }
    fs::create_directories(instr.output_dir_path);

    bool versim = (perf_state->get_target_device_type() == tt::TargetDevice::Versim);
    json postprocess_report = create_postprocess_report(all_cores, all_core_ids, instr, perf_desc, epoch_event->device_alignment_info, epoch_event->graph_wrapper->graph, epoch_event->op_to_perf_model_desc, align_devices, versim);
    
    string output_file_name = instr.output_dir_path + postprocess_json_name;
    log_assert(!fs::exists(output_file_name), "Postprocessor report already exists under path {}", output_file_name);
    write_json_report_to_file(postprocess_report, output_file_name);
    log_info(tt::LogPerfInfra, "Finished writing the postprocess report for epoch with program_name {} graph_name {} local_epoch_id {} global_epoch_id {} under path {}",
            instr_wrap.program_name, graph_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id, output_file_name);

    const json &all_cores_config = get_core_config_json(epoch_event->graph_wrapper->core_to_descriptor);
    string cores_to_ops_path = instr.output_dir_path + cores_to_ops_json_name;
    log_debug(tt::LogPerfInfra, "Creating cores_to_ops report under {}", cores_to_ops_path);    
    write_json_report_to_file(all_cores_config, cores_to_ops_path);

    const json &runtime_report = create_runtime_report(postprocess_report);
    string runtime_report_path = instr.output_dir_path + runtime_table_json_name;
    log_debug(tt::LogPerfInfra, "Creating runtime table report under {}", runtime_report_path);
    write_json_report_to_file(runtime_report, runtime_report_path);

    log_debug(tt::LogPerfInfra, "Creating op table report under {}", instr.output_dir_path + op_table_json_name);
    create_op_report(runtime_report, instr.output_dir_path, perf_desc);

    create_runtime_table(runtime_report, instr.output_dir_path);

    all_instruction_info.push_back(instr);
}

void PerfHostPostprocessQueue::start_runtime_postprocessor() {
    log_assert(!active, "start_runtime_postprocessor should not be enabled more than once. pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    active = true;
    postprocess_queue_thread = std::thread([&] {
        try {
            log_info(tt::LogPerfInfra, "Starting polling for perf postprocessor queue new perf traces on pid {}, thread_id {}", uint(getpid()), perf::get_thread_id());
            while (active) {
                process_new_device_dump();
            }
            process_new_device_dump();
            log_info(tt::LogPerfInfra, "Finished query for perf postprocessor queue on pid {}, thread_id {}", uint(getpid()), perf::get_thread_id());
        } catch (std::exception &e) {
            std::rethrow_exception(std::current_exception());
        }
    });
}

void PerfHostPostprocessQueue::stop_runtime_postprocessor() {
    log_assert(active, "stop_runtime_postprocessor is called on a thread that was not querying the perf postprocessor queue on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    active = false;
    log_info(tt::LogPerfInfra, "Waiting for perf postprocessor thread to finish on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
    postprocess_queue_thread.join();
    log_info(tt::LogPerfInfra, "Finished waiting for perf postprocessor query thread on pid {} thread_id {}", uint(getpid()), perf::get_thread_id());
}

PerfHostPostprocessQueue::~PerfHostPostprocessQueue() {
    if (active) {
        log_assert(postprocess_queue_thread.joinable(), "runtime_postprocessor must be active if active is true");
        active = false;
        postprocess_queue_thread.join();
    } else {
        log_assert(!postprocess_queue_thread.joinable(),  "runtime_postprocessor must not be active if active is false");
    }
}

void PerfHostPostprocessQueue::process_new_device_dump() {

    while (!q.empty()) {

        log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Starting to process new perf dump from postprocessor-queue at rd_ptr {}, wr_ptr {}.", q.get_rd_ptr(), q.get_wr_ptr());
        
        // First word in device dump is a trace-valid signal
        uint rd_ptr = 0;
        std::shared_ptr<uint32_t> first_word_ptr = q.front();
        log_assert(first_word_ptr.get()[0] == perf::valid_thread_dump_start_id, "Each device dump must start with value {}", perf::valid_thread_dump_start_id);
        
        for (uint thread_idx = 0; thread_idx < host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP; thread_idx++) {

            // log_debug(tt::LogPerfPostProcess, "PerfHostPostprocessQueue: Thread dump in postprocessor queue:");
            // log_debug(tt::LogPerfPostProcess, "{}", get_thread_dump_partial_ss(first_word_ptr + rd_ptr, single_thread_dump_size).str());

            PerfDumpHeaderWrap header_wrap(first_word_ptr.get() + rd_ptr + 1);
            PerfDumpHeader header = header_wrap.header;
            total_num_threads_received++;
            total_num_valid_threads_received++;
            // log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Found a new thread dump with header: ");
            // log_debug(tt::LogPerfInfra, "{}", get_perf_dump_header_ss(header).str());
            log_assert(header.thread_id < thread_names.size(), "Invalid thread {}. Only thread ids 0 to 4 are valid", uint(header.thread_id));
            uint thread_size = (header.thread_id == 1) ? l1_mem::address_map::MATH_PERF_BUF_SIZE :
                                (header.thread_id == 4) ? l1_mem::address_map::BRISC_PERF_BUF_SIZE : perf_state->get_perf_desc().get_perf_buf_size(header.thread_id) / 2;
            vector<uint32_t> new_vec(first_word_ptr.get() + rd_ptr, first_word_ptr.get() + rd_ptr + thread_size / sizeof(uint32_t));
            if (partial_thread_dumps.find(header_wrap) == partial_thread_dumps.end()) {
                log_debug(tt::LogPerfInfra, "First partial thread for the current header. Total number of events including the new one = {}", new_vec.size());
                partial_thread_dumps.insert({header_wrap, new_vec});
            } else {
                log_debug(tt::LogPerfInfra, "Appending the new partial thread. Total number of events for the current header = {}", partial_thread_dumps.at(header_wrap).size());
                // Remove the valid signal and the header
                new_vec.erase(new_vec.begin(), new_vec.begin() + 2);
                partial_thread_dumps.at(header_wrap).insert(partial_thread_dumps.at(header_wrap).end(), new_vec.begin(), new_vec.end());
            }
            q.check_not_empty_and_pop();

            tt_cxy_pair current_core_coord(header.chip_id, header.x, header.y);
            if (core_coord_to_num_dumps.find(current_core_coord) == core_coord_to_num_dumps.end()) {
                core_coord_to_num_dumps.insert({current_core_coord, 0});
            }
            core_coord_to_num_dumps.at(current_core_coord)++;

            if (new_vec.back() == thread_dump_end_id || header.thread_id == 1) {
                total_num_full_threads_received++;
                // log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Found the end signal for thread with header:");
                // log_debug(tt::LogPerfInfra, "{}", get_perf_dump_header_ss(header).str());
                thread_events current_thread = run_postprocessor_single_thread_epoch(header, partial_thread_dumps.at(header_wrap));
                perf::PerfDumpCoreHeaderWrapper core_header(header);
                // log_debug(tt::LogPerfInfra, "Current core header: x = {}, y = {}, chip_id = {}, epoch_id = {}", 
                //             uint(core_header.core_header.x), uint(core_header.core_header.y), uint(core_header.core_header.chip_id), uint(core_header.core_header.epoch_id));
                // for (auto header_it: partial_core_events) {
                //     log_debug(tt::LogPerfInfra, "previous core header: x = {}, y = {}, chip_id = {}, epoch_id = {}", 
                //                 uint(header_it.first.core_header.x), uint(header_it.first.core_header.y), uint(header_it.first.core_header.chip_id), uint(header_it.first.core_header.epoch_id));
                // }
                if (partial_core_events.find(core_header) == partial_core_events.end()) {
                    log_debug(tt::LogPerfInfra, "Inserting new core_event into the partial_core_events map for core {}. Total number of cores in partials {}",
                                tt_xy_pair(core_header.core_header.x, core_header.core_header.y).str(), partial_core_events.size());
                    core_events current_core;
                    current_core.core_x = core_header.core_header.x;
                    current_core.core_y = core_header.core_header.y;
                    partial_core_events.insert({core_header, current_core});
                }
                const string &thread_name = thread_names.at(header.thread_id);
                core_events &current_core = partial_core_events.at(core_header);
                log_assert(current_core.threads.find(thread_name) == current_core.threads.end(),
                            "For the following core/epoch, the thread_events already exist: {}", get_perf_dump_header_ss(header).str());
                current_core.threads.insert({thread_name, current_thread});
                log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Inserting thread name {} for core {} chip {}. Total number of threads processed so far for this core = {}",
                            thread_name, tt_xy_pair(current_core.core_x, current_core.core_y).str(), uint(header.chip_id), current_core.threads.size());
                bool all_threads_collected = current_core.threads.size() == thread_names.size();
                if (all_threads_collected) {
                    total_num_full_core_received++;
                    auto instr_pair = perf_state->get_global_epoch_idx_and_instr_for_core(header);
                    const instruction_info_wrapper &instr_wrap = instr_pair.second;
                    const uint32_t global_epoch_id = instr_pair.first;
                    const string &graph_name = instr_wrap.instr->graph_name;
                    current_core.descriptor = perf_state->get_core_desc(graph_name, tt_xy_pair(header.x, header.y));                    

                    tt_xy_pair core_pair(header.x, header.y);
                    // log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: All thread evnets for core {} chip {} under graph_name {} and program_name {} with local_epoch_id {} and global_epoch_id {} have been collected. Starting core_event generation.",
                    //             core_pair.str(), uint(header.chip_id), graph_name, instr_wrap.program_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id);
                    
                    // // set the all_outer_loop_events, and math_activity parameter for current_core
                    // log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Starting to calculate first to last outer loop events for core {} chip {} under graph_name {} and program_name {} with local_epoch_id {} and global_epoch_id {}.",
                    //             core_pair.str(), uint(header.chip_id), graph_name, instr_wrap.program_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id);
                    calculate_first_to_last_outer_loop_cycles(current_core);

                    set_unpack_pack_num_tiles(current_core);
                    // log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Starting to set out_of_memory flags for core {} chip {} under graph_name {} and program_name {} with local_epoch_id {} and global_epoch_id {}.",
                    //             core_pair.str(), uint(header.chip_id), graph_name, instr_wrap.program_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id);
                    calculate_brisc_bw_info(current_core);
                    current_core.check_and_set_out_of_memory();

                    if (global_epoch_id_to_epoch_events.find(global_epoch_id) == global_epoch_id_to_epoch_events.end()) {
                        const uint &target_device = perf_state->get_graph(graph_name).my_graph_info.target_device;
                        const uint &aiclk = perf_state->get_aiclk(target_device);
                        uint num_epochs_this_program = 0;
                        num_epochs_this_program = perf_state->get_num_instructions_executed(instr_wrap.program_id);
                        std::shared_ptr<epoch_events> new_epoch_event = std::make_shared<epoch_events>(
                            instr_wrap,
                            perf_state->get_graph_wrapper(graph_name),
                            perf_state->get_all_perf_model_desc(),
                            perf_state->get_device_alignment_info(),
                            aiclk,
                            num_epochs_this_program
                        );
                        global_epoch_id_to_epoch_events.insert({
                            global_epoch_id,
                            new_epoch_event});
                    }

                    std::shared_ptr<epoch_events> current_epoch_event = global_epoch_id_to_epoch_events.at(global_epoch_id);
                    log_assert(current_epoch_event->all_core_events.find(core_pair) == current_epoch_event->all_core_events.end(),
                        "The core_events for core {} has already been processed for global epoch id {}", core_pair.str(), global_epoch_id);
                    
                    current_epoch_event->all_core_events.insert({
                        core_pair, current_core});
                    
                    partial_core_events.erase(core_header);
                    
                    if (current_epoch_event->is_fully_populated()) {
                        total_num_full_epoch_received++;
                        report_generator->push_new_epoch_events(current_epoch_event);
                        log_info(tt::LogPerfInfra, "Finished postprocessor for epoch with program_name {} graph_name {} local_epoch_id {} global_epoch_id {}",
                            instr_wrap.program_name, graph_name, instr_wrap.local_epoch_id, instr_wrap.global_epoch_id);

                        global_epoch_id_to_epoch_events.erase(global_epoch_id);
                    }
                    // current_core.descriptor
                }
                partial_thread_dumps.erase(header_wrap);
            }

            rd_ptr += single_thread_dump_size / sizeof(uint32_t);
            log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Finished appending the partial thread perf dump at rd_ptr {}, wr_ptr {} for header: {}", q.get_rd_ptr(), q.get_wr_ptr(), get_perf_dump_header_ss(header).str());
        }
    }
}

const void PerfHostPostprocessQueue::print_final_summary() const {
    log_debug(tt::LogPerfInfra, "Final postprocessor queue summary:");
    log_debug(tt::LogPerfInfra, "Total number of perf traces received = {}", total_num_threads_received);
    log_debug(tt::LogPerfInfra, "Total number of valid perf traces received = {}", total_num_valid_threads_received);
    uint num_traces_check = perf_state->get_perf_desc().check_total_num_traces;
    if (num_traces_check > 0) {
        log_assert(total_num_valid_threads_received == num_traces_check, "Total number of traces received {} is not equal to the target value {}",
                    total_num_valid_threads_received, num_traces_check);
    }
    log_debug(tt::LogPerfInfra, "Total number of full thread traces received = {}", total_num_full_threads_received);
    log_debug(tt::LogPerfInfra, "Total number of full core traces received = {}", total_num_full_core_received);
    log_debug(tt::LogPerfInfra, "Total number of full epoch traces received = {}", total_num_full_epoch_received);
    for (const auto& core_it: core_coord_to_num_dumps) {
        log_debug(tt::LogPerfInfra, "Number of perf traces received from core {} = {}", core_it.first.str(), core_it.second);
    }
}

void run_performance_analyzer(const PerfState &device_perf_state, const string &test_output_dir) {
    log_info(tt::LogPerfInfra, "Running Performance Analyzer");
    string script_path;
    const string budabackend_root = buda_home();
    log_assert(budabackend_root != "", "Performance Analyzer failed to read the budabackend root. BUDA_HOME env var is not set.");
    script_path = budabackend_root + "/perf_lib/perf_analysis.py ";
    string analyzer_cmd = script_path + "--analysis-mode overlay_decouple --skip-test-run ";
    analyzer_cmd += "--input-test-perf-results ";
    const string &perf_output_dir = get_perf_out_directory(test_output_dir, device_perf_state.get_perf_desc().override_perf_output_dir, false);
    analyzer_cmd += perf_output_dir;
    log_info(tt::LogPerfInfra, "Performance Analyzer command: {}", analyzer_cmd);
    log_info(tt::LogPerfInfra, "Reading the performance results from {}", perf_output_dir);
    const string analyzer_output_path = perf_output_dir + "analyzer_results/";
    log_info(tt::LogPerfInfra, "Writing the analyzer results to {}", perf_output_dir);
    const string &analyzer_log_path = perf_output_dir + "analyzer.log";
    const string &analyzer_error_path = perf_output_dir + "analyzer.err";
    if (fs::exists(analyzer_output_path)) {
        fs::remove_all(analyzer_output_path);
    }
    fs::create_directory(analyzer_output_path);
    auto result = tt::run_command(analyzer_cmd, analyzer_log_path, analyzer_error_path);
    if (result.success) {
        log_info(tt::LogPerfInfra, "Performance analyzer finished successfully");
    } 
    else {
        log_error("Error log: {}", result.message);
        log_fatal("Performance analyzer failed. Full log under {}", analyzer_error_path);
    }
}

}
