// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <experimental/filesystem>
#include "runtime/runtime_utils.hpp"
#include "epoch_loader.hpp"
#include "common/model/tt_core.hpp"
#include "epoch_q.h"
#include "third_party/json/json.hpp"

enum entry_type {
    BinaryAlloc,
    CommandQueuePush,
    QUpdateAlloc,
};

static const std::map<entry_type, string> dram_entry_str = {
    {entry_type::BinaryAlloc, "Epoch-binary-blob allocation"},
    {entry_type::CommandQueuePush, "Command queue push"},
    {entry_type::QUpdateAlloc, "Queue-update-blob allocation"},
};

class tt_dram_profiler {
private:
    bool profiler_en = false;
    int dram_profiler_entry = 0;
    map<tt_cxy_pair, uint> core_to_command_entry;

    uint get_and_update_command_entry_idx(tt_cxy_pair core);
public:
    nlohmann::json dram_profiler_report;
    tt_dram_profiler();
    ~tt_dram_profiler();
    void record_initial_state(std::map<chip_id_t, std::vector<tt_chan_alloc_struct>> chan_struct_map, std::unordered_set<chip_id_t> chip_ids);
    void create_worker_to_command_queue_map(vector<tt_epoch_queue *> queues, int chip_id);
    void add_report_for_binary_alloc_entry(tt_epoch_control* ctrl, int device_id, int dram_channel, int dram_subchannel, int start_addr, int size_bytes, tt_xy_pair worker, string epoch_name);
    void add_report_for_command_queue_entry(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> hex);
    void add_report_for_q_update_blob(tt_epoch_control* ctrl, tt_hex *update_hex, string queue_name, tt_xy_pair worker, uint64_t start_addr);
    void print_loader_json_report(string output_file_name = "loader_report.json");
};

void print_binary_cache_report(const std::string &output_report, tt_epoch_binary_cache& program_cache, std::vector<tt_epoch_binary_cache>& io_queue_update_cache, buda_SocDescriptor& sdesc, chip_id_t device);
void register_early_exit_function(void (*f)(int));
void restore_handler_and_raise(int sig);

// Path manipulation for dealing with symlinks for binaries
inline std::string get_abs_base_path(fs::path path) {
    return fs::canonical(path).string();
}

// Convert op_base_path to unique_op_index.
inline uint32_t get_unique_op_index(tt_epoch_control* ctrl, std::string op_base_path) {
    log_assert(ctrl->op_path_to_unique_op_idx.find(op_base_path) != ctrl->op_path_to_unique_op_idx.end(), "{} - Couldn't find {} in map", __FUNCTION__, op_base_path);
    return ctrl->op_path_to_unique_op_idx.at(op_base_path);
}
