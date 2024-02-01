// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime_utils.hpp"
#include "loader/tt_cluster.hpp"

#include "l1_address_map.h"

using FormatterMap = std::unordered_map<std::string, std::function<std::string(uint32_t)>>;

/**
 * Log Server
 * 
 * Provides host side logging for firmware emitted TT_LOG messages
 * It is the receiver of the FW_TEST_MAILBOX and is responsible for clearing the mailbox
 */
class tt_log_server
{
  public:

    /**
     * @brief List of devices to be monitored by the log server
     * Current implementation serialzes all devices read/write to the mailbox
     */
    std::set<int> target_devices;

    /**
     * @brief List of clients to be monitored by the log server
     * Each client is a firmware target that can emit TT_LOG messages
     */
    std::vector<std::string> target_clients = {"brisc", "ncrisc"};

    /**
     * @brief Construct a new tt log server object
     */
    tt_log_server(tt_cluster *cluster, std::string output_dir, std::set<int> target_device_ids);

    /**
     * @brief Start the log server
     * If already started this will do nothing
     */
    void start();

    /**
     * @brief Stop the log server
     * If already stopped this will do nothing
     */
    void stop();

    /**
     * @brief Read all debug buffers and dump to file
     */
    void dump_debug();

  private:
    enum class tt_log_server_state {
        Idle = 0,
        Running = 1,
        PendingTerminate = 2,
    };

    FormatterMap formatter_map = {
        // Format {} as decimal values
        {"{}", [](uint32_t val) {
            return std::to_string(val);
        }},
        // Format {:x} as hex values
        {"{:x}", [](uint32_t val) {
            std::ostringstream oss;
            oss << std::hex << val;
            return oss.str();
        }},
    };

    const std::string LOGGER_PRE = "\033[1;35m";
    const std::string LOGGER_POST = "\033[0m";
    const std::string LOGGER_PAUSE = "TT_PAUSE";
    const std::string LOGGER_ASSERT = "TT_RISC_ASSERT";
    const std::string DEBUG_DUMP_ASSERT = "TT_DUMP_ASSERT";
    const std::string DEBUG_DUMP_LOG = "TT_DUMP_LOG";

    static constexpr uint32_t MAILBOX_FLAG = 0xC0FFEE;

    tt_cluster *cluster;
    std::string output_dir;
    tt_log_server_state state;

    // Background thread for monitoring device status
    std::vector<std::thread> background_threads;

    enum ERiscThread {
        Trisc0 = 0,
        Trisc1 = 1,
        Trisc2 = 2,
        Firmware = 3,
    };

    // Reverse hash maps for looking up info from hash
    std::unordered_map<ERiscThread, std::unordered_map<uint32_t, std::string>> hashed_messages;
    std::unordered_map<ERiscThread, std::unordered_map<uint32_t, std::string>> hashed_macro_types;

    struct RiscThreadConfig {
        LogType log_type;
        uint32_t mailbox_base;
        uint32_t mailbox_size;
        uint32_t debug_dump_base;
        uint32_t debug_dump_size;
    };

    static const std::unordered_map<ERiscThread, RiscThreadConfig> RISC_THREAD_CONFIG;

    // used for tracking which mailbox needs to be monitored
    std::unordered_map<tt_cxy_pair, std::unordered_set<ERiscThread>> monitored_mailboxes;

    // used for tracking which mailbox has been populated and needs to clear
    std::unordered_map<tt_cxy_pair, std::unordered_set<ERiscThread>> logged_mailboxes;
    bool do_pause;

    bool debug_dump_enabled;

    // Go through all fwlog files and builds reverse hash maps
    void build_reverse_hash_maps();

    // Updates hashed_messages and hashed_macro_types from filename.
    void update_reverse_hash_maps(const std::string& filename,
                                  ERiscThread risc_thread,
                                  bool& has_mailbox_macro,
                                  bool& has_debug_dump_macro);

    void clear_reverse_hash_maps();

    // Enables monitoring mailbox_type on all cores
    // by updating monitored_mailboxes map
    void enable_mailbox_monitoring(ERiscThread risc_thread);

    // Monitors all mailboxes that are in the monitored_mailboxes map.
    void monitor_mailboxes();

    // Monitors mailbox type on specific core.
    void monitor_mailbox(const tt_cxy_pair& core, ERiscThread risc_thread);

    // Clears first two words of mailbox
    void clear_mailbox(const tt_cxy_pair& core, ERiscThread risc_thread);

    // All debug buffers are cleared
    void clear_debug_dump_buffers();

    // All mailboxes are cleared
    void clear_mailboxes();

    // Implements custom formatters for logged messages
    std::string format_message(const std::string& message, std::vector<uint32_t>& values);

    bool get_message_and_macro_type(ERiscThread risc_thread,
                                    std::vector<uint32_t>& values,
                                    std::string& message,
                                    std::string& macro_type);
};
