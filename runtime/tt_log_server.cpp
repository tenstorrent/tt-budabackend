// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <sstream>
#include <functional>
#include <experimental/filesystem>

#include "tt_log_server.hpp"

const std::unordered_map<tt_log_server::ERiscThread, tt_log_server::RiscThreadConfig>
    tt_log_server::RISC_THREAD_CONFIG = {
        {tt_log_server::ERiscThread::Trisc0, {
            tt::LogTrisc0,
            l1_mem::address_map::TRISC0_TT_LOG_MAILBOX_BASE,
            l1_mem::address_map::TRISC_TT_LOG_MAILBOX_SIZE,
            l1_mem::address_map::TRISC0_DEBUG_BUFFER_BASE,
            l1_mem::address_map::DEBUG_BUFFER_SIZE
        }},
        {tt_log_server::ERiscThread::Trisc1, {
            tt::LogTrisc1,
            l1_mem::address_map::TRISC1_TT_LOG_MAILBOX_BASE,
            l1_mem::address_map::TRISC_TT_LOG_MAILBOX_SIZE,
            l1_mem::address_map::TRISC1_DEBUG_BUFFER_BASE,
            l1_mem::address_map::DEBUG_BUFFER_SIZE
        }},
        {tt_log_server::ERiscThread::Trisc2, {
            tt::LogTrisc2,
            l1_mem::address_map::TRISC2_TT_LOG_MAILBOX_BASE,
            l1_mem::address_map::TRISC_TT_LOG_MAILBOX_SIZE,
            l1_mem::address_map::TRISC2_DEBUG_BUFFER_BASE,
            l1_mem::address_map::DEBUG_BUFFER_SIZE
        }},
        {tt_log_server::ERiscThread::Firmware, {
            tt::LogFirmware,
            l1_mem::address_map::FW_MAILBOX_BASE,
            l1_mem::address_map::FW_MAILBOX_BUF_SIZE,
            0, // not defined
            0 // not defined
        }}
    };

tt_log_server::tt_log_server(tt_cluster *cluster, std::string output_dir, std::set<int> target_device_ids)
: target_devices(target_device_ids)
, cluster(cluster)
, output_dir(output_dir)
, state(tt_log_server_state::Idle)
, do_pause(false)
, debug_dump_enabled(false) {}

void tt_log_server::start() {
    if (state == tt_log_server_state::Running) {
        return;  // server already running, exit
    }
    build_reverse_hash_maps();
    if (debug_dump_enabled) {
        clear_debug_dump_buffers();
    }

    if (monitored_mailboxes.empty()) {
        log_debug(tt::LogRuntime, "Skipping log server, no mailboxes to monitor");
        return;  // no messages to monitor, exit
    }

    clear_mailboxes();

    state = tt_log_server_state::Running;
    background_threads.emplace_back(std::thread(&tt_log_server::monitor_mailboxes, this));
    log_debug(tt::LogRuntime, "Started log server");
}

void tt_log_server::stop() {
    if (state == tt_log_server_state::Idle) {
        return;  // server already stopped, exit
    }
    state = tt_log_server_state::PendingTerminate;
    for (auto &thread : background_threads) {
        thread.join();
    }
    background_threads.clear();
    state = tt_log_server_state::Idle;
    log_debug(tt::LogRuntime, "Stopped log server");
}

// returns number of items added
void tt_log_server::update_reverse_hash_maps(const std::string& filename,
                                             ERiscThread risc_thread,
                                             bool& has_mailbox_macro,
                                             bool& has_debug_dump_macro) {
    int count = 0;

    has_mailbox_macro = false;
    has_debug_dump_macro = false;

    if (fs::exists(filename)) {
        std::ifstream file(filename);
        log_assert(file.is_open(), "{} - Cannot open {}. errno: {}", __FUNCTION__, filename, std::strerror(errno));
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string filename, file_hash, msg_hash, line_num, macro_type, message;
            iss >> filename >> file_hash >> line_num >> msg_hash >> macro_type >> std::ws;
            std::getline(iss, message);
            uint32_t val = std::stoul(msg_hash, nullptr, 16);
            hashed_messages[risc_thread][val] = filename + ":" + line_num + ": " + message;
            hashed_macro_types[risc_thread][val] = macro_type;
            count++;
            if (macro_type == DEBUG_DUMP_ASSERT || macro_type == DEBUG_DUMP_LOG) {
                has_debug_dump_macro = true;
            } else {
                has_mailbox_macro = true;
            }
            log_trace(tt::LogRuntime, "Loaded message: {} -> {}, {}",
                val, hashed_macro_types[risc_thread][val], hashed_messages[risc_thread][val]);
        }
    }
    log_trace(tt::LogRuntime, "Loaded {} messages from file {}", count, filename);
}

// Enables monitoring of specific mailbox type for all cores.
void tt_log_server::enable_mailbox_monitoring(ERiscThread risc_thread) {
    for (const int device : target_devices) {
        buda_soc_description &sdesc = cluster->get_soc_desc(device);
        // check if any core mailbox has a flag
        for (const tt_xy_pair &core : sdesc.workers) {
            monitored_mailboxes[tt_cxy_pair(device, core)].insert(risc_thread);
        }
    }
}

void tt_log_server::build_reverse_hash_maps()
{
    std::unordered_set<ERiscThread> mailbox_risc_threads;
    bool has_mailbox_macro = false;
    bool has_debug_dump_macro = false;

    for (auto &client : target_clients) {
        std::string filename = output_dir + "/" + client + "/" + client + ".fwlog";
        update_reverse_hash_maps(filename, ERiscThread::Firmware, has_mailbox_macro, has_debug_dump_macro); 
        log_assert(!has_debug_dump_macro, "Debug dump macro should not be in client fwlog file {}", filename);
        if (has_mailbox_macro) {
            mailbox_risc_threads.insert(ERiscThread::Firmware);
        }
    }

    // Currently we are iterating through all ckernel.fwlog files and loading them into the same hash map
    for (const auto& entry : fs::recursive_directory_iterator(output_dir)) {
        if (fs::is_regular_file(entry) && entry.path().filename() == "ckernel.fwlog") {
            ERiscThread risc_thread;
            std::string filename = entry.path().parent_path().filename();
            if (filename == "tensix_thread0") {
                risc_thread = ERiscThread::Trisc0;
            } else if (filename == "tensix_thread1") {
                risc_thread = ERiscThread::Trisc1;
            } else if (filename == "tensix_thread2") {
                risc_thread = ERiscThread::Trisc2;
            } else {
                log_debug(tt::LogRuntime, "Unexpected ckernel.fwlog file {}", filename);
                continue;
            }

            update_reverse_hash_maps(entry.path(), risc_thread, has_mailbox_macro, has_debug_dump_macro);
            debug_dump_enabled |= has_debug_dump_macro;
            if (has_mailbox_macro) {
                mailbox_risc_threads.insert(risc_thread);
            }
        }
    }

    for (auto risc_thread : mailbox_risc_threads) {
        enable_mailbox_monitoring(risc_thread);
    }
}

void tt_log_server::clear_reverse_hash_maps()
{
    hashed_messages.clear();
    hashed_macro_types.clear();
}

void tt_log_server::monitor_mailboxes()
{
    // forever loop checking for flag in fw mailbox
    while (true) {
        // check if we should terminate the loop
        if (state == tt_log_server_state::PendingTerminate) {
            break;
        }

        for (const auto& [core, risc_threads] : monitored_mailboxes) {
            for (const auto& risc_thread : risc_threads) {
                monitor_mailbox(core, risc_thread);
            }
        }

        // unpause if any cores were paused and clear mailbox flag
        if (do_pause) {
            tt::pause(LOGGER_PAUSE);
            do_pause = false;
        }

        for (const auto& [core, risc_threads] : logged_mailboxes) {
            for (const auto& risc_thread : risc_threads) {
                clear_mailbox(core, risc_thread);
            }
        }

        logged_mailboxes.clear();
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void tt_log_server::monitor_mailbox(const tt_cxy_pair& core, ERiscThread risc_thread) {
    std::vector<uint32_t> rd_vec;
    const RiscThreadConfig &thread_config = RISC_THREAD_CONFIG.at(risc_thread);
    cluster->read_dram_vec(rd_vec, core, thread_config.mailbox_base, thread_config.mailbox_size);

    std::string message;
    std::string macro_type;
    if (get_message_and_macro_type(risc_thread, rd_vec, message, macro_type)) {
        logged_mailboxes[core].insert(risc_thread);
        std::ostringstream ss;
        ss << LOGGER_PRE << "Core " << std::left << std::setw(12) << core.str() << message << LOGGER_POST;

        // check if a message source requires custom handling
        if (macro_type == LOGGER_ASSERT) {
            log_fatal("{}", ss.str());
        } else if (macro_type == LOGGER_PAUSE) {
            do_pause = true;
            log_info(thread_config.log_type, "{}", ss.str());
        } else {
            log_info(thread_config.log_type, "{}", ss.str());
        }
    }
}

// Clears first two words of mailbox
void tt_log_server::clear_mailbox(const tt_cxy_pair& core, ERiscThread risc_thread) {
    std::vector<uint32_t> clear_mailbox = {0, 0};
    cluster->write_dram_vec(clear_mailbox, core, RISC_THREAD_CONFIG.at(risc_thread).mailbox_base);
}

void tt_log_server::clear_debug_dump_buffers() {
    for (const int device : target_devices) {
        for (const tt_xy_pair &core : cluster->get_soc_desc(device).workers) {
            tt_cxy_pair cxy(device, core);
            for (const auto& [risc_thread, config] : RISC_THREAD_CONFIG) {
                if (config.debug_dump_size == 0) continue;
                std::vector<uint32_t> clear_buffer(config.debug_dump_size, 0);
                cluster->write_dram_vec(clear_buffer, cxy, config.debug_dump_base);
            }
        }
    }
}

void tt_log_server::clear_mailboxes() {
    for (const auto& [core, mailbox_types] : monitored_mailboxes) {
        for (const auto& risc_thread : mailbox_types) {
            clear_mailbox(core, risc_thread);
        }
    }
}

// Reads all debug buffers and prints messages
// that are stored in them
void tt_log_server::dump_debug() {
    if (!debug_dump_enabled) return;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&MAILBOX_FLAG);
    std::vector<uint8_t> mailbox_flag(ptr, ptr + sizeof(uint32_t));

    bool is_assert = false;

    for (const int device : target_devices) {
        buda_soc_description &sdesc = cluster->get_soc_desc(device);
        for (const tt_xy_pair &core : sdesc.workers) {
            tt_cxy_pair cxy(device, core);
            std::vector<uint32_t> rd_vec;
            for (const auto& [risc_thread, config] : RISC_THREAD_CONFIG) {
                if (config.debug_dump_size == 0) continue;
                cluster->read_dram_vec(rd_vec, cxy, config.debug_dump_base, config.debug_dump_size);
                auto it = std::find(rd_vec.begin(), rd_vec.end(), MAILBOX_FLAG);
                while (it != rd_vec.end()) {
                    std::vector<uint32_t> new_v(it, (it + 16 > rd_vec.end()) ? rd_vec.end() : it + 16);
                    std::string message;
                    std::string macro_type;
                    get_message_and_macro_type(risc_thread, new_v, message, macro_type);
                    is_assert |= (macro_type == DEBUG_DUMP_ASSERT);

                    std::ostringstream ss;
                    ss << LOGGER_PRE << "Core " << std::left << std::setw(12) << core.str() << message << LOGGER_POST;

                    // check if a message source requires custom handling
                    log_info(config.log_type, "{}", ss.str());
                    it = std::find(it + 1, rd_vec.end(), MAILBOX_FLAG);
                }
            }
        }
    }

    if (is_assert) {
        log_fatal("TT_DUMP_ASSERT detected");
    }
}

bool tt_log_server::get_message_and_macro_type(ERiscThread risc_thread,
                                               std::vector<uint32_t>& values,
                                               std::string& message,
                                               std::string& macro_type) {
    uint32_t rd_flag = values[0];
    uint32_t rd_macro = values[1];
    values.erase(values.begin(), values.begin() + 2);

    if (rd_flag == MAILBOX_FLAG) {
        message = "undefined message";
        macro_type = "UNKOWN_MACRO";

        if (hashed_macro_types[risc_thread].count(rd_macro)) {
            message = format_message(hashed_messages[risc_thread].at(rd_macro), values);
            macro_type = hashed_macro_types[risc_thread].at(rd_macro);
        }
    }

    return rd_flag == MAILBOX_FLAG;
}

std::string tt_log_server::format_message(const std::string& message, std::vector<uint32_t>& values)
{
    size_t index = 0;
    std::string placeholder;
    std::string result = message;

    while (!values.empty()) {
        index = 0;
        // find the next placeholder string
        for (const auto& f : formatter_map) {
            size_t find_index = result.find(f.first);
            if (index == 0 or find_index < index) {
                index = find_index;
                placeholder = f.first;
            }
        }
        // if no placeholder string found, exit
        if (index == std::string::npos) break;

        // replace placeholder string with formatted value
        const auto &formatter = formatter_map.at(placeholder);
        result.replace(index, placeholder.length(), formatter(values.front()));
        values.erase(values.begin());
    }
    return result;
}