// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"
#include <csignal>

static constexpr bool enable_dump_after_every_entry = false;
using json = nlohmann::json;

string get_filled_entry_idx(uint entry_idx) {
    std::stringstream ss;
    ss << std::setw(9) << std::setfill('0') << to_string(entry_idx);
    return ss.str();
}

tt_dram_profiler::tt_dram_profiler() {
    profiler_en = tt::parse_env("TT_BACKEND_DRAM_PROFILER_EN", profiler_en);
    if (profiler_en) log_info(tt::LogLoader, "Enabling Dram profiler...");
}

tt_dram_profiler::~tt_dram_profiler() {
    dram_profiler_report.clear();
}

template<typename T>
string get_hex_from_int(T value) {
    std::stringstream iss;
    iss << "0x" << std::hex << value;
    return iss.str();
}

uint tt_dram_profiler::get_and_update_command_entry_idx(tt_cxy_pair core) {
    if (core_to_command_entry.find(core) == core_to_command_entry.end()) {
        core_to_command_entry.insert({core, 0});
        return 0;
    } else {
        uint current_index = core_to_command_entry.at(core);
        core_to_command_entry.at(core) = ++current_index;
        return current_index;
    }
}

void tt_dram_profiler::record_initial_state(std::map<chip_id_t, std::vector<tt_chan_alloc_struct>> chan_struct_map, std::unordered_set<chip_id_t> chip_ids) {
    if (profiler_en) {
        for (auto chip_id: chip_ids) {
            dram_profiler_report["chip-" + to_string(chip_id)] = {};
        }
        for (auto chip_to_alloc_vec: chan_struct_map) {
            json chip_report;
            int chan_index = 0;
            for (auto chan_alloc: chip_to_alloc_vec.second) {
                json report_single_chan;
                report_single_chan["top_of_reserved"] = get_hex_from_int(chan_alloc.top_of_reserved);
                report_single_chan["top_of_epoch_command_queue"] = get_hex_from_int(chan_alloc.top_of_epoch0_start_table);
                report_single_chan["top_of_epoch_binaries_queue"] = get_hex_from_int(chan_alloc.top_of_binaries);
                report_single_chan["dram_bank_size"] = chan_alloc.top_of_chan;
                report_single_chan["top_of_queue_update_blob"] = get_hex_from_int(chan_alloc.top_of_q_update_blobs);
                report_single_chan["epoch_q_slot_size"] = chan_alloc.epoch_q_slot_size;
                chip_report["channel-" + to_string(chan_index)] = report_single_chan;
                chan_index++;
            }
            dram_profiler_report["chip-" + to_string(chip_to_alloc_vec.first)]["initial-state"] = chip_report;
        }
        if (enable_dump_after_every_entry) {
            print_loader_json_report();
        }
    }
}

void tt_dram_profiler::create_worker_to_command_queue_map(vector<tt_epoch_queue *> queues, int chip_id) {
    if (profiler_en) {
        json all_queues;
        for (auto queue: queues) {
            json queue_report;
            queue_report["worker"] = queue->associated_routing_core.str();
            queue_report["active"] = queue->is_active();
            queue_report["num_slots"] = queue->get_num_slots();
            queue_report["size_bytes"] = queue->size_bytes();
            queue_report["dram_channel"] = queue->get_dram_chan();
            queue_report["dram_subchannel"] = queue->get_dram_subchannel();
            log_assert(chip_id == queue->d_chip_id, "Chip Id mismatch");
            queue_report["chip_id"] = queue->d_chip_id;
            queue_report["dram_address"] = get_hex_from_int(queue->d_addr);
            tt_cxy_pair core_coord(queue->d_chip_id, queue->associated_routing_core);
            all_queues[core_coord.str()] = queue_report;
        }
        dram_profiler_report["chip-" + to_string(chip_id)]["command_queue_worker_map"] = all_queues;
        if (enable_dump_after_every_entry) {
            print_loader_json_report();
        }
    }
}

void tt_dram_profiler::add_report_for_binary_alloc_entry(tt_epoch_control* ctrl, int device_id, int dram_channel, int dram_subchannel, int start_addr, int size_bytes, tt_xy_pair worker, string epoch_name) {
    if (profiler_en) {
        string entry_str = dram_entry_str.at(entry_type::BinaryAlloc);
        uint binary_wr_ptr = ctrl->bin_q_ptrs.wr_ptr;
        json entry;
        // entry["type"] = entry_str;
        entry["epoch-name"] = epoch_name;
        entry["worker"] = worker.str();
        entry["dram_channel"] = dram_channel;
        entry["dram_subchannel"] = dram_subchannel;
        entry["wr_ptr"] = binary_wr_ptr;
        entry["start_addr"] = get_hex_from_int(start_addr);
        entry["size_bytes"] = size_bytes;

        dram_profiler_report["chip-" + to_string(device_id)][entry_str]["entry-" + get_filled_entry_idx(dram_profiler_entry)] = entry;
        dram_profiler_entry++;
        if (enable_dump_after_every_entry) {
            print_loader_json_report();
        }
    }
}

void tt_dram_profiler::add_report_for_command_queue_entry(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> hex) {
    if (profiler_en) {
        string entry_str = dram_entry_str.at(entry_type::CommandQueuePush);
        json entry;
        int command_type = ((hex -> hex_vec[1] >> 28)&0xf);
        tt_xy_pair worker_core = hex -> associated_routing_core;
        chip_id_t chip_id = q_ptr -> d_chip_id;

        // entry["type"] = entry_str;
        entry["epoch-name"] = hex -> name;
        entry["epoch-cmd-type"] = command_type;
        entry["worker"] = hex -> associated_routing_core.str();
        entry["chip_id"] = chip_id;
        entry["dram_channel"] = q_ptr->get_dram_chan();
        entry["dram_subchannel"] = q_ptr->get_dram_subchannel();
        entry["dram_address"] = get_hex_from_int(q_ptr->d_addr);
        entry["wr_ptr"] = q_ptr->get_wr_ptr();

        int num_words = hex -> hex_vec.size();
        entry["num_words"] = num_words;

        for (int word_idx = 0; word_idx < num_words; word_idx++) {
            entry["word-" + to_string(word_idx)] = get_hex_from_int(hex -> hex_vec.at(word_idx));
        }

        uint current_entry_idx = get_and_update_command_entry_idx(tt_cxy_pair(chip_id, worker_core));
        dram_profiler_report["chip-" + to_string(chip_id)][entry_str][worker_core.str()]["entry-" + get_filled_entry_idx(current_entry_idx)] = entry;
        if (enable_dump_after_every_entry) {
            print_loader_json_report();
        }
    }
}

void tt_dram_profiler::add_report_for_q_update_blob(tt_epoch_control* ctrl, tt_hex *update_hex, string queue_name, tt_xy_pair worker, uint64_t start_addr) {
    if (profiler_en) {
        string entry_str = dram_entry_str.at(entry_type::QUpdateAlloc);
        uint dram_channel = update_hex->get_dram_chan();
        int queue_index = worker.y*ctrl->grid_shape[1] + worker.x;
        uint binary_wr_ptr = ctrl->io_q_update_ptrs.at(queue_index).wr_ptr;
        json entry;
        // entry["type"] = entry_str;
        entry["epoch-name"] = "queue-update for " + queue_name;
        entry["worker"] = worker.str();
        entry["dram_channel"] = dram_channel;
        entry["dram_subchannel"] = update_hex->get_dram_subchannel();
        entry["wr_ptr"] = binary_wr_ptr;
        entry["start_addr"] = get_hex_from_int(start_addr);
        entry["size_bytes"] = epoch_queue::get_queue_update_blob_size_bytes();

        int num_words = update_hex->hex_vec.size();
        entry["num_words"] = num_words;

        for (int word_idx = 0; word_idx < num_words; word_idx++) {
            entry["word-" + to_string(word_idx)] = get_hex_from_int(update_hex->hex_vec.at(word_idx));
        }

        dram_profiler_report["chip-" + to_string(update_hex->d_chip_id)][entry_str]["entry-" + get_filled_entry_idx(dram_profiler_entry)] = entry;
        dram_profiler_entry++;
        if (enable_dump_after_every_entry) {
            print_loader_json_report();
        }
    }
}

void tt_dram_profiler::print_loader_json_report(string output_file_name) {
    if (profiler_en) {
        std::ofstream output_file(output_file_name);
        output_file << std::setw(4) << dram_profiler_report;
        output_file.flush();
        output_file.close();
    }
}

void print_binary_cache_report(const std::string &output_report, tt_epoch_binary_cache& program_cache, std::vector<tt_epoch_binary_cache>& io_queue_update_cache, buda_SocDescriptor& sdesc, chip_id_t device) {
    json cache_report;
    std::string device_str = "chip-" + to_string(device);
    cache_report[device_str]["program_binary_cache"]["cache-misses"] = program_cache.profiler.cache_misses;
    cache_report[device_str]["program_binary_cache"]["cache-accesses"] = program_cache.profiler.num_cache_accesses;
    cache_report[device_str]["program_binary_cache"]["unique-cache-keys"] = program_cache.profiler.total_unique_cache_keys;
    cache_report[device_str]["program_binary_cache"]["num-preloads"] = program_cache.profiler.num_preloads;

    for (int i = 0; i < io_queue_update_cache.size(); i++) {
        if(std::find(sdesc.workers.begin(), sdesc.workers.end(), tt_xy_pair(i % epoch_queue::GridSizeRow, i / epoch_queue::GridSizeRow)) != sdesc.workers.end()) {
            std::string cache_name = "io_queue_update_cache_" + to_string(i);
            cache_report[device_str][cache_name]["cache-misses"] = io_queue_update_cache[i].profiler.cache_misses;
            cache_report[device_str][cache_name]["cache-accesses"] = io_queue_update_cache[i].profiler.num_cache_accesses;
            cache_report[device_str][cache_name]["unique-cache-keys"] = io_queue_update_cache[i].profiler.total_unique_cache_keys;
            cache_report[device_str][cache_name]["num-preloads"] = io_queue_update_cache[i].profiler.num_preloads;
        }
    }
    std::ofstream output_file(output_report);
    output_file << std::setw(4) << cache_report;
    output_file.flush();
    output_file.close();
}

void register_early_exit_function(void (*f)(int)) {
    log_assert(std::signal(SIGABRT, f) != SIG_ERR, "Could not setup exit handler.");
    log_assert(std::signal(SIGINT, f) != SIG_ERR, "Could not setup exit handler.");
    log_assert(std::signal(SIGSEGV, f) != SIG_ERR, "Could not setup exit handler.");
    log_assert(std::signal(SIGTERM, f) != SIG_ERR, "Could not setup exit handler.");
}

void restore_handler_and_raise(int sig) {
    std::signal(sig, sig == SIGSEGV ? tt::assert::segfault_handler : SIG_DFL); // reset to default handler
    std::raise(sig);           // raise the signal
}
