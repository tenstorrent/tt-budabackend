// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <experimental/filesystem>
#include "runtime/runtime_utils.hpp"
#include "epoch_loader.hpp"
#include "common/model/tt_core.hpp"
#include "dram_address_map.h"
#include "utils.hpp"
#include "epoch_utils.hpp"
#include "utils/scoped_timer.hpp"


namespace fs = std::experimental::filesystem;
extern perf::tt_backend_perf backend_profiler;
static tt_dram_profiler dram_profiler;
map<chip_id_t, std::vector<tt_chan_alloc_struct>> chan_struct_map;
// ---------------------------------------------------------------------------
// Epoch DRAM Manager
// ---------------------------------------------------------------------------
tt_epoch_dram_manager::tt_epoch_dram_manager(chip_id_t chip, const buda_soc_description &soc_desc) : associated_chip(chip), sdesc(soc_desc)
{
    // Refer to dram.cpp for default offsets
    tt::ARCH arch = sdesc.arch;
    const std::unordered_map<tt_xy_pair, std::tuple<int, int>> &dram_chan_map = sdesc.dram_core_channel_map;

    int num_chans = sdesc.get_dram_chan_map().size();
    uint64_t epoch0_start_table_size_bytes = epoch_queue::GridSizeRow*epoch_queue::GridSizeCol*epoch_queue::get_epoch_table_entry_size_bytes();
    uint64_t chan_capacity_bytes = sdesc.dram_bank_size;

    std::vector<uint16_t> t6_cores_per_dram_channel =
        get_num_cores_per_dram_channel(arch, sdesc.workers, dram_chan_map, num_chans);
    std::vector<uint16_t> eth_cores_per_dram_channel =
        get_num_cores_per_dram_channel(arch, sdesc.ethernet_cores, dram_chan_map, num_chans);

    for(int i=0;i<num_chans;++i)
    {
        uint64_t top_of_binaries = get_top_of_binaries(t6_cores_per_dram_channel[i], eth_cores_per_dram_channel[i]);
        uint64_t top_of_q_update_blobs = get_top_of_q_update_blobs(t6_cores_per_dram_channel[i], eth_cores_per_dram_channel[i]);
        chan_struct_vec.push_back(tt_chan_alloc_struct(
            epoch_queue::get_epoch_table_dram_addr(),
            get_top_of_epoch0_start_table(),
            top_of_binaries,
            chan_capacity_bytes,
            get_bin_size_q_slot(t6_cores_per_dram_channel[i], eth_cores_per_dram_channel[i]),
            top_of_q_update_blobs));
        log_debug(tt::LogLoader, "DRAM channel {}: {} T6 cores, {} ETH cores, {} bytes per channel, reserved bottom {} bytes for backend usage",
            i, t6_cores_per_dram_channel[i], eth_cores_per_dram_channel[i], chan_capacity_bytes, top_of_q_update_blobs);
    }
    chan_struct_map.insert({associated_chip, chan_struct_vec});

    for (tt_xy_pair worker: sdesc.workers) {
        auto [dram_channel, dram_subchannel] = get_dram_chan(
            sdesc.arch,
            worker, sdesc.dram_core_channel_map);

        if (dram_channel_to_worker.find(dram_channel) == dram_channel_to_worker.end()) {
            dram_channel_to_worker.insert({dram_channel, {}});
        }
        dram_channel_to_worker.at(dram_channel).push_back(worker);
    }
    for (tt_xy_pair ethernet: sdesc.ethernet_cores) {
        auto [dram_channel, dram_subchannel] = get_dram_chan(
            sdesc.arch,
            ethernet, sdesc.dram_core_channel_map);

        if (dram_channel_to_worker.find(dram_channel) == dram_channel_to_worker.end()) {
            dram_channel_to_worker.insert({dram_channel, {}});
        }
        dram_channel_to_worker.at(dram_channel).push_back(ethernet);
    }
}

uint tt_epoch_dram_manager::get_worker_idx_in_channel(tt_xy_pair worker, int dram_channel) {
    int worker_idx = -1;
    for (int i = 0; i < dram_channel_to_worker.at(dram_channel).size(); i++) {
        if (dram_channel_to_worker.at(dram_channel).at(i) == worker) {
            worker_idx = i;
            break;
        }
    }
    log_assert(worker_idx >= 0, "All worker and ethernet cores must exist in dram_channel_to_worker");
    return worker_idx;
}


std::tuple<int, int> tt_epoch_dram_manager::get_dram_chan(
    tt::ARCH arch, tt_xy_pair physical_core, const std::unordered_map<tt_xy_pair, std::tuple<int, int>> &dram_chan_map) {
    tt_xy_pair dram_core;
    if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        switch (physical_core.y) {
            case 0:
            case 11:
            case 1:
            case 7:
            case 5:
            case 6:
                if (physical_core.x >= 5)
                    dram_core.x = 5;
                else
                    dram_core.x = 0;
                break;

            default: dram_core.x = 5; break;
        }
        dram_core.y = physical_core.y;
    } else if (arch == tt::ARCH::GRAYSKULL) {
        switch (physical_core.x) {
            case 1:
            case 2:
            case 3: dram_core.x = 1; break;
            case 4:
            case 5:
            case 6: dram_core.x = 4; break;
            case 7:
            case 8:
            case 9: dram_core.x = 7; break;
            case 10:
            case 11:
            case 12: dram_core.x = 10; break;
            default: dram_core.x = -1; break;
        }
        if (physical_core.y <= 5) {
            dram_core.y = 0;
        } else {
            dram_core.y = 6;
        }
    } else {
        log_fatal("Unsupported architecture");
    }
    log_assert(dram_chan_map.find(dram_core) != dram_chan_map.end(), "Dram core {} not found in soc descriptor", dram_core.str());
    return dram_chan_map.at(dram_core);
}

std::vector<uint16_t> tt_epoch_dram_manager::get_num_cores_per_dram_channel(
    tt::ARCH arch, const std::vector<tt_xy_pair> &cores, const std::unordered_map<tt_xy_pair, std::tuple<int, int>> &dram_chan_map, int dram_chan_cnt) {
    std::vector<uint16_t> cores_per_dram_channel(dram_chan_cnt);
    // TODO: currently there isn't full support for subchan across the stack
    // Update this func to return a per subchan count
    for (const tt_xy_pair &core : cores) {
        auto [dram_channel, dram_subchannel] = get_dram_chan(arch, core, dram_chan_map);
        cores_per_dram_channel.at(dram_channel) += 1;
    }
    return cores_per_dram_channel;
}

uint64_t tt_epoch_dram_manager::get_top_of_epoch0_start_table() {
    return epoch_queue::DRAM_EPOCH_METADATA_LIMIT;
}

uint64_t tt_epoch_dram_manager::get_bin_size_q_slot(int t6_cores_per_chan, int eth_cores_per_chan) {
    uint64_t bin_size_q_slot =
        dram_mem::address_map::FW_DRAM_BLOCK_SIZE() * t6_cores_per_chan +
        eth_l1_mem::address_map::FW_DRAM_BLOCK_SIZE * eth_cores_per_chan;
    return bin_size_q_slot;
}

uint64_t tt_epoch_dram_manager::get_top_of_binaries(int t6_cores_per_chan, int eth_cores_per_chan) {
    uint64_t top_of_epoch0_start_table = get_top_of_epoch0_start_table();
    // FW_DRAM_BLOCK_SIZE - size of all binaries in a single epoch that belong to one core
    uint64_t bin_size_q_slot = get_bin_size_q_slot(t6_cores_per_chan, eth_cores_per_chan);
    uint64_t bin_size_bytes = bin_size_q_slot * (uint64_t)epoch_queue::get_epoch_bin_num_slots();
    uint64_t top_of_binaries = top_of_epoch0_start_table + bin_size_bytes;
    log_trace(tt::LogLoader, "Top of binaries = 0x{:x} for t6_cores_per_chan: {}, t6_core_q_slot_size: 0x{:x}, eth_cores_per_chan: {}, eth_core_q_slot_size: 0x{:x}", top_of_binaries, t6_cores_per_chan, dram_mem::address_map::FW_DRAM_BLOCK_SIZE(), eth_cores_per_chan, eth_l1_mem::address_map::FW_DRAM_BLOCK_SIZE);
    return top_of_binaries;
}

uint64_t tt_epoch_dram_manager::get_top_of_q_update_blobs(int t6_cores_per_chan, int eth_cores_per_chan) {
    uint64_t top_of_binaries = get_top_of_binaries(t6_cores_per_chan, eth_cores_per_chan);
    uint64_t q_update_blob_size_bytes = (uint64_t)epoch_queue::get_queue_update_blob_size_bytes() * (uint64_t)epoch_queue::get_epoch_io_queue_update_num_slots() * (t6_cores_per_chan + eth_cores_per_chan);
    uint64_t top_of_q_update_blobs = top_of_binaries + q_update_blob_size_bytes;
    return top_of_q_update_blobs;
}

void tt_epoch_dram_manager::allocate_epoch_queue(tt_epoch_queue* queue, bool distribute_tables) {
    log_assert(queue->is_active(), "Expected populated associated routing core for hex queue");

    tuple<int,int> dram_chan = {-1,-1};
    int dram_channel         = -1;
    int dram_subchannel      = -1;
    tt_xy_pair routing_core  = queue->associated_routing_core;
    uint64_t dram_addr       = 0;
    uint64_t l1_shadow_addr  = 0;

    if (sdesc.is_worker_core(routing_core)) {
        tt_xy_pair worker_core = sdesc.get_worker_core(routing_core);
        log_assert(worker_core.x != -1 && worker_core.y != -1, "Invalid worker core.");
        if (distribute_tables) {
            dram_chan = get_dram_chan(sdesc.arch, routing_core, sdesc.dram_core_channel_map);
        } else {
            dram_chan = {0,0}; // allocate to chan 0 by default
        }
    } else {
        log_assert(routing_core.x != -1 && routing_core.y != -1, "Invalid routing core.");
        dram_chan = get_dram_chan(sdesc.arch, routing_core, sdesc.dram_core_channel_map);
    }
    dram_channel = std::get<0>(dram_chan);
    dram_subchannel = std::get<1>(dram_chan);
    dram_addr = chan_struct_vec.at(dram_channel).alloc_epoch_address_q(queue->size_bytes(), routing_core);
    // log_trace(tt::LogLoader, "Allocated epoch cmd queue for chip {} worker/eth core {} at DRAM addr 0x{:x} ch: {} subch: {} (core {})",
    //     associated_chip, routing_core.str(), dram_addr, dram_channel, dram_subchannel, sdesc.get_core_for_dram_channel(dram_channel, dram_subchannel).str());

    if (sdesc.is_worker_core(routing_core)) {
        l1_shadow_addr = l1_mem::address_map::NCRISC_L1_EPOCH_Q_BASE;
    } else {
        l1_shadow_addr = eth_l1_mem::address_map::L1_EPOCH_Q_BASE;
    }

    queue->set_l1_shadow_addr(l1_shadow_addr);
    queue->set_dram_addr(dram_addr);
    queue->set_dram_chan(dram_channel);
    queue->set_dram_subchannel(dram_subchannel);
    queue->set_chip_id(associated_chip);
}
// // ---------------------------------------------------------------------------
// // Epoch Binary Cache (inherited from tt_recency_cache)
// // ---------------------------------------------------------------------------

tt_epoch_binary_cache::tt_epoch_binary_cache(std::string name, uint32_t capacity, bool enable_mru_with_backtrace_bin_cache) : tt_recency_cache(capacity) {
    use_lru = !enable_mru_with_backtrace_bin_cache;
    cache_name = name;
}

bool tt_epoch_binary_cache::full() {
    return list.size() == capacity;
}

int tt_epoch_binary_cache::policy_based_binary_slot(uint32_t offset) {
    return use_lru ? cache.at(list.back()).first : cache.at(*std::next(list.begin(), offset)).first;
}

string tt_epoch_binary_cache::policy_based_binary_name(uint32_t offset) {
    return use_lru ? list.back() : *std::next(list.begin(), offset);
}

void tt_epoch_binary_cache::clear_pinned_binaries() {
    if (pinned_binary_names.size() > 0){
        log_trace(tt::LogLoader, "Clearing {} pinned binaries for {}", pinned_binary_names.size(), cache_name);
    }
    pinned_binary_names.clear();
}

bool tt_epoch_binary_cache::get_slot_for_binary(const std::string& binary_name, std::unordered_map<std::string, int>& num_cmds_per_bin, tt_queue_ptr& binary_q_ptr, int& slot, 
    std::vector<tt_epoch_queue*>& command_qs, tt_cluster* cluster, bool binary_preload, bool pin_binary, bool io_queue_update_cmd) {
    slot = get(binary_name, binary_preload);
    bool cache_hit = slot >= 0;

    if (pin_binary) {
        pinned_binary_names.insert(binary_name);
        log_trace(tt::LogLoader, "Pinning Binary: {} in Cache: {}", binary_name, cache_name);
    }
    
    if(!cache_hit) {
        uint32_t offset = capacity / 2;
        if(full()) {
            log_assert(pinned_binary_names.size() <= capacity,
                "Insufficient space for binary {} in {} (capacity: {} pinned_binaries: {}). Consider increasing size via env-var.",
                binary_name, cache_name, capacity, pinned_binary_names.size());

            while(num_cmds_per_bin.at(policy_based_binary_name(offset))) {
                // Find a binary that can be evicted. Starting in the middle of the cache and moving towards the LRU binary reduces overhead of querying device rd ptrs.
                for(int i = 0; i < command_qs.size(); i++) {
                    (command_qs[i]) -> update_num_commands_per_binary(cluster, io_queue_update_cmd);
                }
                offset = (offset - 1 + capacity) % capacity; // Wrap at 0 back to capacity.
            }
            // After rd ptr updates, trace back towards the MRU binary, in case a more recent binary got evicted. This approximates an ideal MRU cache eviction policy.
            uint32_t initial_offset = (!use_lru) * offset;
            for(int i = 0; i < initial_offset; i++) {
                if(!num_cmds_per_bin.at(policy_based_binary_name(offset - 1))) {
                    offset--;
                }
                else break;
            }

            // Catch the case if somehow evicted binary is pinned (requried to not be evicted)
            auto evicted_binary_name = policy_based_binary_name(offset);
            if (pinned_binary_names.find(evicted_binary_name) != pinned_binary_names.end()){
                log_fatal("Insufficient space in {} - pinned binary {} would be evicted to make room for {}.", cache_name, evicted_binary_name, binary_name);
            }

            slot = policy_based_binary_slot(offset);
        }
        else {
            slot = binary_q_ptr.incr_get_wr();
        }
        if(use_lru) put_lru(binary_name, slot, offset);
        else put_mru_bt(binary_name, slot, offset);
    }
    return cache_hit;
}

// ---------------------------------------------------------------------------
// Epoch Binary
// ---------------------------------------------------------------------------
void tt_epoch_binary::create_build_dir(const std::string &output_dir) {
    if (fs::exists(output_dir)) {
        log_debug(tt::LogLoader, "Deleting existing build output_dir: {}", output_dir);
        fs::remove_all(output_dir);
    }
    fs::create_directory(output_dir);
}

int tt_epoch_binary::number_of_tensix_hex_images() {
    return(trisc0_bin_vec.size());
}

int tt_epoch_binary::number_of_ethernet_hex_images()
{
    return(ethernet_blob_bin_vec.size());
}

tt_xy_pair tt_epoch_binary::get_logical_coord(int hex_id) {
    int core_r = trisc0_bin_vec[hex_id].creating_core->get_logical_absolute_row_id();
    int core_c = trisc0_bin_vec[hex_id].creating_core->get_logical_absolute_col_id();
    return tt_xy_pair(core_c, core_r);
}

tt_xy_pair tt_epoch_binary::get_routing_coord(int hex_id) {
    return trisc0_bin_vec[hex_id].associated_routing_core;
}

vector <uint32_t> tt_epoch_binary::get_risc_binary(string path, uint32_t id) {
    // vector <uint32_t> bin;
    string path_to_bin = path;
    fs::path bin_file(path_to_bin);
    log_assert(fs::exists(bin_file), "Error {} doesn't exist", bin_file.c_str());
    ifstream hex_istream(path_to_bin);
    log_assert(hex_istream.is_open(), "{} - Cannot open {}. errno: {}", __FUNCTION__, path_to_bin, std::strerror(errno));
    ll_api::memory mem = ll_api::memory::from_discontiguous_risc_hex(hex_istream, id==1 ? ll_api::memory::NCRISC: ll_api::memory::BRISC);
    return mem.get_content();
}

vector<uint32_t> tt_epoch_binary::get_trisc_binary(string path, uint32_t id) {
    vector <uint32_t> bin;
    string path_to_bin = path + "/tensix_thread" + to_string(id) +
        "/tensix_thread" + to_string(id) + ".hex";
    fs::path bin_file(path_to_bin);
    log_assert(fs::exists(bin_file), "Error {} doesn't exist", bin_file.c_str());
    ifstream hex_istream(path_to_bin);
    log_assert(hex_istream.is_open(), "{} - Cannot open {}. errno: {}", __FUNCTION__, path_to_bin, std::strerror(errno));
    ll_api::memory mem = ll_api::memory::from_discontiguous_risc_hex(hex_istream, (ll_api::memory::risc_type_t)((int)ll_api::memory::TRISC0+id));
    return mem.get_content();
}

vector <uint32_t> tt_epoch_binary::get_overlay_binary(string path) {
    string path_to_bin = path;
    fs::path bin_file(path_to_bin);
    log_assert(fs::exists(bin_file), "Error {} doesn't exist", bin_file.c_str());
    ifstream hex_istream(path_to_bin);
    log_assert(hex_istream.is_open(), "{} - Cannot open {}. errno: {}", __FUNCTION__, path_to_bin, std::strerror(errno));
    ll_api::memory mem = ll_api::memory::from_discontiguous_hex(hex_istream);
    return mem.get_content();
}

vector <uint32_t> tt_epoch_binary::get_empty_trisc_binary() {
    // dummy hex to initialize a binary variable and go through dram allocation
    vector <uint32_t> bin(32, 0);
    return bin;
}

vector <uint32_t> tt_epoch_binary::get_empty_overlay_binary() {
    // dummy hex to initialize a binary variable and go through dram allocation
    vector <uint32_t> bin(dram_mem::address_map::OVERLAY_FULL_BLOB_SIZE()/4, 0);
    return bin;
}

vector <uint32_t> tt_epoch_binary::get_empty_ethernet_overlay_binary() {
    // dummy hex to initialize a binary variable and go through dram allocation
    vector <uint32_t> bin(eth_l1_mem::address_map::OVERLAY_FULL_BLOB_SIZE()/4, 0);
    return bin;
}

vector <uint32_t> tt_epoch_binary::get_runtime_config_binary(const tt_op_info &op_info) {
    vector <uint32_t> bin(l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE/4, 0);
    // valid values 0 - 64, 0 means use default settings (this control is disabled), 64 means poll
    // immediately, other values set the intial count value, from which we count to 64. 64 wraps to 0
    uint32_t dram_header_polling_freq = parse_env<uint32_t>("TT_BACKEND_DRAM_POLLING_FREQUENCY", 0);
    tt_epoch_config config = {
        .overlay_size_bytes = (op_info.overlay_size > 0) ? (uint32_t)op_info.overlay_size
                                                         : dram_mem::address_map::OVERLAY_FULL_BLOB_SIZE(),
        .dram_header_polling_freq = dram_header_polling_freq > 64 ? 64 : dram_header_polling_freq,
    };
    log_assert(sizeof(config) <= l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE,
        "Epoch runtime config size is too large (exceeds {} bytes)", l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE);
    std::memcpy(bin.data(), &config, sizeof(config));
    return bin;
}

vector <uint32_t> tt_epoch_binary::get_empty_runtime_config_binary() {
    // dummy hex to initialize a runtime config and go through dram allocation
    vector <uint32_t> bin(eth_l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE/4, 0);
    return bin;
}

void tt_epoch_binary::get_epoch_binaries(
    const std::string &output_dir,
    const std::string &graph_name,
    const map<string, tt_op_info> &op_map,
    const buda_soc_description& sdesc)
{
    log_debug(tt::LogLoader, "Get binaries for graph: {}, num OPs = {}", graph_name, op_map.size());
    
    string epoch_path = output_dir + "/graph_" + graph_name;

    // This is a critical section. Since static binary objects are shared amongst threads
    static_binary_mutex.lock();
    if(ncrisc_vec.size() == 0) {
        // Static binaries were not initialized. Set them up once, to be shared amongst all tt_epoch_binary objects
        ncrisc_vec = get_risc_binary(output_dir + "/ncrisc/ncrisc.hex", 1);
        brisc_vec = get_risc_binary(output_dir + "/brisc/brisc.hex", 0);
        if(sdesc.ethernet_cores.size()) {
            // Need to get erisc FW binaries
            if(sdesc.arch == tt::ARCH::WORMHOLE_B0) {
                // Enable ERISC FW execution in iRAM for B0
                erisc_vec = get_risc_binary(output_dir + "/erisc/erisc_app.l1.hex", 2);
                erisc_iram_vec = get_risc_binary(output_dir + "/erisc/erisc_app.iram.hex", 2);
            }
            else {
                erisc_vec =  get_risc_binary(output_dir + "/erisc/erisc_app.hex", 2);
            }
        }
        string blob_init_build_dir = output_dir + "/blob_init";
        blob_bin_init = get_overlay_binary(blob_init_build_dir + "/blob_init.hex");
    }
    // Critical section done
    static_binary_mutex.unlock();

    uint32_t op_index = 0;
    map<string, tt_op_info>::const_iterator it_op;

    std::unordered_set<tt_xy_pair> used_worker_physical_cores = {};

    // Go through all ops, and cores in each op and populate non-static binary vectors
    for (it_op = op_map.begin(); it_op != op_map.end(); it_op++, op_index++) {
        if (netlist_utils::is_non_tensix_op(it_op->second.type)) {
            continue;
        }
        string op_path = epoch_path + "/op_" + to_string(op_index);
        std::shared_ptr<tt_op> op = it_op->second.my_op;
        log_debug(tt::LogLoader, "Getting binaries for op {} w/ index {}", op->name, op_index);
        log_debug(tt::LogLoader, "Grid shape {}", op->get_grid_shape());

        for(int rr=0; rr<op->get_grid_shape().r; ++rr)
        {
            for(int cc=0; cc<op->get_grid_shape().c; ++cc)
            {
                const tt_xy_pair &core_xy = tt_xy_pair(
                    sdesc.worker_log_to_routing_x.at(op->cores[rr][cc]->get_logical_absolute_col_id()), 
                    sdesc.worker_log_to_routing_y.at(op->cores[rr][cc]->get_logical_absolute_row_id())
                );
                this->trisc0_bin_vec.push_back(tt_hex(get_trisc_binary(op_path, 0), tt_hex_type::Trisc0, core_xy));
                this->trisc1_bin_vec.push_back(tt_hex(get_trisc_binary(op_path, 1), tt_hex_type::Trisc1, core_xy));
                this->trisc2_bin_vec.push_back(tt_hex(get_trisc_binary(op_path, 2), tt_hex_type::Trisc2, core_xy));
                this->blob_bin_vec.push_back(tt_hex(get_empty_overlay_binary(), tt_hex_type::Blob, core_xy));
                this->runtime_config_vec.push_back(tt_hex(get_runtime_config_binary(it_op->second), tt_hex_type::RuntimeConfig, core_xy));
                tt_core* core = op->cores[rr][cc];
                // log_debug(tt::LogLoader, "\tCore r:{}, c:{},\tpr:{}, pc:{}", rr, cc, core->get_logical_absolute_row_id(), core->get_logical_absolute_col_id());

                used_worker_physical_cores.insert(core_xy);
            }
        }
    }

    for (const auto &physical_worker_core : sdesc.workers) {
        bool core_unused = used_worker_physical_cores.find(physical_worker_core) == used_worker_physical_cores.end();
        if (core_unused) {
            this->trisc0_bin_vec.push_back(tt_hex(get_empty_trisc_binary(), tt_hex_type::Trisc0, physical_worker_core));
            this->trisc1_bin_vec.push_back(tt_hex(get_empty_trisc_binary(), tt_hex_type::Trisc1, physical_worker_core));
            this->trisc2_bin_vec.push_back(tt_hex(get_empty_trisc_binary(), tt_hex_type::Trisc2, physical_worker_core));
            this->blob_bin_vec.push_back(tt_hex(get_empty_overlay_binary(), tt_hex_type::Blob, physical_worker_core));
            this->runtime_config_vec.push_back(tt_hex(get_empty_runtime_config_binary(), tt_hex_type::RuntimeConfig, physical_worker_core));
        }
    }

    const auto &eth_cores = sdesc.ethernet_cores;
    for (const tt_xy_pair &eth_core_loc : eth_cores) {
        this->ethernet_blob_bin_vec.push_back(tt_hex(get_empty_ethernet_overlay_binary(), tt_hex_type::Blob, eth_core_loc));
        this->ethernet_runtime_config_vec.push_back(tt_hex(get_empty_runtime_config_binary(), tt_hex_type::RuntimeConfig, eth_core_loc));
        // log_debug(tt::LogLoader, "\tEthernet blob bin and runtime config bin assigned to routing core (y={}, x={})", eth_core_loc.y, eth_core_loc.x);
    }
}

void tt_epoch_binary::update_overlay_binary(const std::string &output_dir, const std::string &graph_name, int temporal_epoch, int chip_id, const buda_soc_description* sdesc, tt_cluster* cluster)
{
    // Support for graph specific or epoch specific overlay blobs. TODO: merge to the same convention
    const string &epoch_path = output_dir + "/temporal_epoch_" + to_string(temporal_epoch);// + "/graph_" + graph_name;

    string overlay_blob_path = epoch_path + "/overlay";
    log_debug(tt::LogLoader, "Update overlay blob using path {}", overlay_blob_path);

    for (tt_hex &hex : blob_bin_vec) {
        auto route_r = hex.associated_routing_core.y;
        auto route_c = hex.associated_routing_core.x;
        
        cluster -> translate_to_noc_table_coords(chip_id, route_r, route_c);        
        string blob_filename = overlay_blob_path + "/pipegen_epoch" + to_string(temporal_epoch) + "_" + to_string(chip_id) + "_" + to_string(route_r) + "_" + to_string(route_c) + ".hex";
        hex.hex_vec = get_overlay_binary(blob_filename);
        log_trace(tt::LogLoader, "update_overlay_binary({} bytes) at routing {}-{}, chip={}", hex.hex_vec.size()*4, route_r, route_c, hex.d_chip_id);
    }

    for (tt_hex &hex : ethernet_blob_bin_vec) {
        auto route_r = hex.associated_routing_core.y;
        auto route_c = hex.associated_routing_core.x;

        cluster -> translate_to_noc_table_coords(chip_id, route_r, route_c);
        string blob_filename = overlay_blob_path + "/pipegen_epoch" + to_string(temporal_epoch) + "_" + to_string(chip_id) + "_" + to_string(route_r) + "_" + to_string(route_c) + ".hex";
        hex.hex_vec = get_overlay_binary(blob_filename);
        log_trace(tt::LogLoader, "\tethernet_blob_bin_vec({} bytes) @routing_core(chip={}, x={}, y={})", hex.hex_vec.size()*4, hex.d_chip_id, route_c, route_r);
    }
}

void tt_epoch_binary::assign_tensix_binaries_to_dram(int hex_id, int dram_channel, int dram_subchannel, uint64_t dram_start_addr)
{
    tt_hex &trisc0_hex = trisc0_bin_vec[hex_id];
    tt_hex &trisc1_hex = trisc1_bin_vec[hex_id];
    tt_hex &trisc2_hex = trisc2_bin_vec[hex_id];
    tt_hex &blob_hex = blob_bin_vec[hex_id];
    tt_hex &runtime_config_hex = runtime_config_vec[hex_id];

    // NCRISC and BRISC FW are directly loaded to L1s bypass DRAM, see send_static_binaries()
    // Only non static binaries are sent here
    trisc0_hex.set_dram_chan(dram_channel);
    trisc0_hex.set_dram_subchannel(dram_subchannel);
    trisc0_hex.set_dram_addr(dram_start_addr + dram_mem::address_map::TRISC0_BASE);
    trisc0_hex.set_chip_id(chip_id);

    trisc1_hex.set_dram_chan(dram_channel);
    trisc1_hex.set_dram_subchannel(dram_subchannel);
    trisc1_hex.set_dram_addr(dram_start_addr + dram_mem::address_map::TRISC1_BASE);
    trisc1_hex.set_chip_id(chip_id);

    trisc2_hex.set_dram_chan(dram_channel);
    trisc2_hex.set_dram_subchannel(dram_subchannel);
    trisc2_hex.set_dram_addr(dram_start_addr + dram_mem::address_map::TRISC2_BASE);
    trisc2_hex.set_chip_id(chip_id);

    blob_hex.set_dram_chan(dram_channel);
    blob_hex.set_dram_subchannel(dram_subchannel);
    blob_hex.set_dram_addr(dram_start_addr + dram_mem::address_map::OVERLAY_BLOB_BASE);
    blob_hex.set_chip_id(chip_id);

    runtime_config_hex.set_dram_chan(dram_channel);
    runtime_config_hex.set_dram_subchannel(dram_subchannel);
    runtime_config_hex.set_dram_addr(dram_start_addr + dram_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE);
    runtime_config_hex.set_chip_id(chip_id);
}

void tt_epoch_binary::assign_ethernet_binaries_to_dram(int hex_id, int dram_channel, int dram_subchannel, uint64_t dram_start_addr)
{
    tt_hex &blob_hex = ethernet_blob_bin_vec[hex_id];
    tt_hex &runtime_config_hex = ethernet_runtime_config_vec[hex_id];

    blob_hex.set_dram_chan(dram_channel);
    blob_hex.set_dram_subchannel(dram_subchannel);
    blob_hex.set_dram_addr(dram_start_addr);
    blob_hex.set_chip_id(chip_id);

    runtime_config_hex.set_dram_chan(dram_channel);
    runtime_config_hex.set_dram_subchannel(dram_subchannel);
    runtime_config_hex.set_dram_addr(dram_start_addr + eth_l1_mem::address_map::OVERLAY_BLOB_SIZE);
    runtime_config_hex.set_chip_id(chip_id);
}

// ---------------------------------------------------------------------------
// Epoch queue
// ---------------------------------------------------------------------------

// tt_epoch_queue class methods
tt_epoch_queue::tt_epoch_queue() : dram_ptrs(0xabadface) {
    associated_routing_core = tt_xy_pair(0xff, 0xff);
    slots = 0xabadface;
    slot_size = 0xabadface;
    hexq.clear();
}

tt_epoch_queue::tt_epoch_queue(int islots, int islot_size, tt_xy_pair core, bool immio_chip, unordered_map<std::string, int>* num_cmds_per_binary, unordered_map<int, int>* num_cmds_per_epoch_id, unordered_map<std::string, int>* num_cmds_per_q_update_blob) : dram_ptrs(islots), binaries_in_use(islots, ""), external_blobs_in_use(islots, ""), epoch_ids_in_queue(islots, std::numeric_limits<int>::max()), num_cmds_per_binary_ptr(num_cmds_per_binary), num_cmds_per_epoch_id_ptr(num_cmds_per_epoch_id), num_cmds_per_q_update_blob_ptr(num_cmds_per_q_update_blob){
    // Initialize epoch_ids_in_queues with max values. In this context, max at a rd_ptr in the queue indicates that no valid epoch command was pushed to that index, or that the command at that index was consumed by the core.
    // Thus, if the current epoch_id is max, either the core is waiting on a command or is executing an invalid command.
    associated_routing_core = core;
    slots = islots;
    slot_size = islot_size;
    mmio_chip = immio_chip;

    hexq.clear();
    if (std::getenv("TT_BACKEND_NUM_BITS_FOR_EPOCH_ID")) {
        int bits_for_epoch_id = parse_env("TT_BACKEND_NUM_BITS_FOR_EPOCH_ID", 0);
        max_offset_between_epoch_ids = std::pow(2, bits_for_epoch_id) - 1;
    }
}

tt_epoch_queue::tt_epoch_queue(const tt_epoch_queue &queue) : dram_ptrs(queue.slots) {
    associated_routing_core = queue.associated_routing_core;
    slots = queue.slots;
    slot_size = queue.slot_size;
    d_addr = queue.d_addr;
    shadow_l1_addr = queue.shadow_l1_addr;
    d_chan = queue.d_chan;
    d_record_vld = queue.d_record_vld;
    num_valid_cmds = queue.num_valid_cmds;
    epoch_ids_in_queue = queue.epoch_ids_in_queue;
    num_cmds_per_binary_ptr = queue.num_cmds_per_binary_ptr;
    num_cmds_per_epoch_id_ptr = queue.num_cmds_per_epoch_id_ptr;
    wc_window_size = queue.wc_window_size;
    mmio_chip = queue.mmio_chip;
    hexq.clear();
}

bool tt_epoch_queue::is_active() {
    return (associated_routing_core != tt_cxy_pair(0xff, 0xff, 0xff));
}


int tt_epoch_queue::size_bytes() {
    log_assert(slots != 0xabadface, "Epoch Queue is unintialized.");
    log_assert(slot_size != 0xabadface, "Epoch Queue is unintialized.");

    // Pad size by 32 bytes for pointers and other metadata
    int size = slots * slot_size + 32;
    log_assert(size % 32 == 0, "Expected epoch queue size to be 32 byte aligned.");

    return(size);
}


// Wrapper to either push to host intermediate buffer with write-combine feature, or push straight to device DRAM. Flush if needed.
void tt_epoch_queue::push_command(std::shared_ptr<tt_hex> hex_ptr, tt_cluster *cluster, bool last_send_epoch_cmd) {

    if (wc_window_size != 0) {
        push(hex_ptr);

        // Should not occur, redundant with later flushing assert, but check that occupancy is not too large to cause 
        // non-adjacent slot writes in epoch cmd queue. Considers that physically wrptr wraps at 2*num_slots
        log_assert((get_wr_ptr()/slots) == ((get_wr_ptr() + occupancy() - 1)/slots),
            "{} - WC Buffer on {} would cause wrptr wrap. occupancy: {} wrptr: {} slots: {} - This is unexpected.",
             __FUNCTION__, get_associated_routing_core().str(), occupancy(), get_wr_ptr(), slots);

        bool is_wc_window_full  = occupancy() == wc_window_size;
        bool is_final_eq_slot   = ((get_wr_ptr() + occupancy()) % slots) == 0; // Wrap at slots, or 0.
        bool non_mmio_cmd_limit = occupancy() == (host_mem::address_map::ETH_ROUTING_BLOCK_SIZE / epoch_queue::EPOCH_Q_SLOT_SIZE);
        bool do_flush           = is_wc_window_full || is_final_eq_slot || non_mmio_cmd_limit;

        if (do_flush) {
            log_trace(tt::LogLoader, "{} - {} WC. do_flush: {} (wc_window_size: {} get_wr_ptr(): {}"
                " HostOccupancy: {} EQSlots: {} is_wc_window_full: {} is_final_eq_slot: {} non_mmio_cmd_limit: {})",
                __FUNCTION__, get_chip_and_associated_routing_core().str(), do_flush, wc_window_size, get_wr_ptr(), occupancy(), 
                slots, is_wc_window_full, is_final_eq_slot, non_mmio_cmd_limit);
            flush_cmds(cluster);
            flush_wrptr(cluster);
            update_write_ptr_in_l1(cluster);
        }
    } else {
        push_to_dram(cluster, hex_ptr, true, last_send_epoch_cmd);
    }
}


// Push either a single command in hex_ptr or all accumulated cmds in queue's WC buffer (hex_ptr constructed on the fly) as 
// single Write Combined transfer to an Epoch Queue on device. Assume the queue has sufficient space and just execute the write
void tt_epoch_queue::push_to_dram(tt_cluster *cluster, std::shared_ptr<tt_hex> hex_ptr, bool send_wrptr_update, bool last_send_epoch_cmd) {

    // Check that we have all the params we need
    log_assert(slots != 0xabadface, "Epoch Queue is unintialized.");
    log_assert(slots != 0, "Number of slots in epoch queue should be  > 0.");
    log_assert(slot_size != 0xabadface, "Epoch Queue is unintialized.");
    log_assert(d_record_vld, "Epoch Queue is unintialized.");

    // Single or Write Combined mode.
    int num_wc_cmds = occupancy();
    uint32_t wr_ptr;

    if (num_wc_cmds == 0) {
        log_assert(hex_ptr != nullptr, "Expected hex to be populated.");
        wr_ptr = dram_ptrs.incr_get_wr(1);
    } else {
        log_assert(hex_ptr == nullptr, "Expected hex to be populated.");
        hex_ptr = std::make_shared<tt_hex>();
        wr_ptr = dram_ptrs.incr_get_wr(num_wc_cmds);

        // Make sure WC flush is to adjacent slots by checking that wrptr doesn't wrap at num_slots. 
        // Since wrptr itself wraps at 2 * num_slots need to be a bit careful here. This should never
        // be hit because flush when pushing commands to WC buffer would be triggered right before wrap.
        bool wrptr_not_wrapped = dram_ptrs.get_wr_ptr() == 0 || ((wr_ptr/slots == dram_ptrs.get_wr_ptr()/slots) && (wr_ptr < dram_ptrs.get_wr_ptr()));
        log_assert(wrptr_not_wrapped, "{} - Epoch Queue DRAM Wrptr wrapped from {} to {} during WC Flush, this is not expected.", __FUNCTION__, wr_ptr, dram_ptrs.get_wr_ptr());
        log_assert(num_wc_cmds <= slots, "{} - Cannot write-combine more epoch cmds {} than epoch queue slots: {}", __FUNCTION__, num_wc_cmds, slots);

        auto num_words_per_slot = slot_size/sizeof(uint32_t);
        log_assert((slot_size % sizeof(uint32_t)) == 0, "Expected slot size to be 4 byte aligned");

        // Write combined hex ptr. Zero pad slots to required size.
        hex_ptr->hex_vec.reserve(num_wc_cmds * num_words_per_slot);
        for (auto& hex : hexq) {
            hex_ptr->hex_vec.insert(hex_ptr->hex_vec.end(), hex->hex_vec.begin(), hex->hex_vec.end());
            hex_ptr->hex_vec.insert(hex_ptr->hex_vec.end(), num_words_per_slot - hex->hex_vec.size(), 0);
        }

        log_assert(hex_ptr->hex_vec.size()*sizeof(hex_ptr->hex_vec[0]) == (num_wc_cmds * slot_size), "WC buffer size vs hex push size mismatch.");
    }
    
    // use local write pointer
    uint32_t wr_addr = d_addr + (wr_ptr * slot_size) + 32;
    hex_ptr->d_addr = wr_addr;
    hex_ptr->d_chan = d_chan;
    hex_ptr->d_subchannel = d_subchannel;
    hex_ptr->d_chip_id = d_chip_id;
    hex_ptr->d_record_vld = d_record_vld;

    constexpr bool small_access = true;     // Dedicated TLB for small read/write accesses for epoch cmds + wrptr update to reduce contention
    constexpr bool send_epoch_cmd = true;   // Dedicated driver API for sending epoch commands

    if (send_wrptr_update) {
        // safe to get raw ptr from shared ptr, since shared ptr goes out of scope after raw ptr is completely used
        cluster->send_hex_to_dram(hex_ptr.get(), small_access, send_epoch_cmd, true);

        // Required to ensure Cmd and Wrptr update to DRAM on remote chips are ordered. Supported only in send_epoch_cmd variant.
        bool ordered_with_prev_remote_write = !mmio_chip;
        tt_driver_atomics::sfence();
        update_write_ptr_in_dram(cluster, true, ordered_with_prev_remote_write);
    } else {
        cluster->send_hex_to_dram(hex_ptr.get(), small_access, send_epoch_cmd, last_send_epoch_cmd);
    }
}

void tt_epoch_queue::init_queue_ptrs(tt_cluster *cluster)
{
    // 32B array
    vector<uint32_t> ptr_vec = {0,0,0,0,0,0,0,0};

    buda_soc_description &sdesc = cluster->get_soc_desc(d_chip_id);
    for (const std::vector<tt_xy_pair> &dram_channel_coords : sdesc.dram_cores) {
        for (const tt_xy_pair &dram_coord : dram_channel_coords) {
            tt_cxy_pair dram_core = tt_cxy_pair(d_chip_id, dram_coord);
            cluster->write_dram_vec(ptr_vec, dram_core, d_addr);
        }
    }

    // Required to ensure DRAM init and L1 init are ordered. Especially do not want
    // polling-epoch-queue-tag (disables l1 wrptr shadow) to arrive before DRAM init.
    cluster->wait_for_non_mmio_flush();

    // For Epoch Queues in L1, only initialize ethernet cores. worker cores handled by ncrisc, or later by runtime if l1wrptr shadow dis.
    if (!sdesc.is_worker_core(associated_routing_core)){
        init_queue_ptrs_l1(cluster);
    }
}

void tt_epoch_queue::init_queue_ptrs_l1(tt_cluster *cluster)
{
    vector<uint32_t> ptr_vec = {0,0,0,0,0,0,0,0};

    if (disable_shadow_l1_wrptr){
        ptr_vec.at(1) = epoch_queue::POLLING_EPOCH_QUEUE_TAG;
    }
    tt_cxy_pair l1_core = get_chip_and_associated_routing_core();
    // log_trace(tt::LogLoader, "Epoch Queue Shadow in L1, (dis_shadow: {}) going to init l1_core: {} addr: 0x{:x},", disable_l1wrptr_shadow, l1_core.str(), shadow_l1_addr);
    cluster->write_dram_vec(ptr_vec, l1_core, shadow_l1_addr);
}


// Write to Epoch Queue Wr Pointer in DRAM
void tt_epoch_queue::update_write_ptr_in_dram(tt_cluster *cluster, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write)
{
    constexpr bool small_access = true;
    constexpr bool reg_txn = false;
    constexpr bool send_epoch_cmd = true; // Required for ordering.
    vector<uint32_t> ptr_vec = {dram_ptrs.wr_ptr};
    cluster->write_dram_vec(ptr_vec, target_dram, d_addr + 4, small_access, send_epoch_cmd, last_send_epoch_cmd, reg_txn, ordered_with_prev_remote_write);
}

// Write to Epoch Queue Wr Pointer Shadow in L1
void tt_epoch_queue::update_write_ptr_in_l1(tt_cluster *cluster, bool last_send_epoch_cmd)
{
    if (is_shadow_l1_wrptr_enabled()) {
        constexpr bool send_epoch_cmd = true;
        vector<uint32_t> ptr_vec = {dram_ptrs.wr_ptr};
        tt_cxy_pair l1_core = get_chip_and_associated_routing_core();
        cluster->write_dram_vec(ptr_vec, l1_core, shadow_l1_addr + 4, true, send_epoch_cmd, last_send_epoch_cmd);
    }
}

bool tt_epoch_queue::is_full_dram(tt_cluster *cluster)
{
    bool full = false;

    // Check local pointers to avoid waiting for the read round trip
    // if that doesn't indicate the buffer is full - it's not
    // otherwise, update local read pointer and check again
    if (dram_ptrs.full()) {
        if (device_loop_stack_depth > 0 && (dram_ptrs.rd_ptr == wr_ptr_device_loop_start_cmd)){
            log_fatal("Epoch Cmd Queue (num_slots: {}) on {} is full, cannot fit entire loop on device's cmds. Try increasing size.", 
                get_num_slots(), get_associated_routing_core().str());
        }
        // Update pointers from DRAM or L1 and check again
        get_rd_ptr_dram(cluster);
        if(dram_ptrs.full()) full = true;
    }
    return full;
}

uint32_t tt_epoch_queue::get_rd_ptr_dram(tt_cluster *cluster) {
    vector<uint32_t> tmp = {};
    cluster->read_dram_vec(tmp, target_dram, d_addr, 4);

    // Update num_valid_cmds and the epoch_ids_in_queue list -> used to keep track of active epochs per core
    // Also update the epoch binary usage (num_cmds_per_binary). Used to keep track of the number of active epochs for the LRU binary before eviction.

    uint32_t max_val_for_intermed_ptr = tmp.at(0); // Case for when device rd_ptr did not wrap around
    // Device rd_ptr wraps around: intermed_ptr should proceed till the unwrapped value of the device rd_ptr (logic similar to tt_queue_ptr::occupancy())
    if(tmp.at(0) < dram_ptrs.rd_ptr) max_val_for_intermed_ptr = tmp.at(0) + dram_ptrs.get_limit();
    for(uint32_t intermed_ptr = dram_ptrs.rd_ptr; intermed_ptr < max_val_for_intermed_ptr; intermed_ptr++){
        int queue_slot = intermed_ptr % slots;
        if(epoch_ids_in_queue.at(queue_slot) != std::numeric_limits<int>::max()){
            (*num_cmds_per_binary_ptr).at(binaries_in_use.at(queue_slot)) -= 1; // decrease the number of cmds referencing binary on this device, as cmd at intermed_ptr is stale
            (*num_cmds_per_epoch_id_ptr).at(epoch_ids_in_queue.at(queue_slot) % max_offset_between_epoch_ids) -=1;
            epoch_ids_in_queue.at(queue_slot) = std::numeric_limits<int>::max(); // epoch_id for this cmd was used up -> set to max
            num_valid_cmds -= 1; // valid cmd was used up
            binaries_in_use.at(queue_slot) = ""; // cmd referencing binary is stale
        }

        if(external_blobs_in_use.at(queue_slot).size()) {
            (*num_cmds_per_q_update_blob_ptr).at(external_blobs_in_use.at(queue_slot)) -= 1;
            external_blobs_in_use.at(queue_slot) = "";            
        }
    }
    dram_ptrs.rd_ptr = tmp.at(0);
    return dram_ptrs.rd_ptr;
}

uint32_t tt_epoch_queue::get_wr_ptr() {
    return dram_ptrs.wr_ptr;
}

void tt_epoch_queue::update_read_ptr(tt_cluster *cluster) {
    vector<uint32_t> ptr_vec = {dram_ptrs.get_rd_ptr()};
    cluster->write_dram_vec(ptr_vec, target_dram, d_addr);

    log_assert(false, "tt_epoch_queue::update_read_ptr() is updating Shadow L1 Epoch Queue which may not be desired.");
    tt_cxy_pair l1_core = get_chip_and_associated_routing_core();
    cluster->write_core_vec(ptr_vec, l1_core, shadow_l1_addr);
}

bool tt_epoch_queue::is_empty_dram(tt_cluster *cluster)
{
    bool empty = true;

    // Check local pointers to avoid waiting for the read round trip
    // if that indicates the buffer is empty - it's empty
    // otherwise, update local read pointer and check again
    if (!dram_ptrs.empty()) {
        // Update pointers from DRAM and check again
        get_rd_ptr_dram(cluster);
        if (!dram_ptrs.empty()) empty = false;
    }
    return empty;
}

int tt_epoch_queue::occupancy() {
    return hexq.size();
}

int tt_epoch_queue::occupancy_dram(tt_cluster *cluster) {
    uint32_t rd_ptr = get_rd_ptr_dram(cluster);
    return dram_ptrs.occupancy();
}

int tt_epoch_queue::free_space_dram(tt_cluster *cluster) {
    uint32_t rd_ptr = get_rd_ptr_dram(cluster);
    return slots - dram_ptrs.occupancy();
}

void tt_epoch_queue::push(std::shared_ptr<tt_hex> hex_ptr) {
    hexq.push_back(hex_ptr);
}

void tt_epoch_queue::pop(int amt) {
    for (int i=0; i<amt; i++) {
        log_assert(hexq.size() != 0, "Attempt to pop empty hexq");
        hexq.pop_front(); // This destroys pointer to binary at the front of queue -> deallocates binary
    }
}


void tt_epoch_queue::wait_for_free_space(tt_cluster* cluster, int num_slots_required) {

    // Flush would already avoid wrap. Also helps ensure slots required doesn't exceed epoch queue size.
    log_assert(num_slots_required <= slots, "Waiting for more epoch cmd queue Write Combine slots than queue size. Unexpected.");
    auto free_space = dram_ptrs.free_space();

    log_trace(tt::LogLoader, "{} - Starting with free_space: {} num_slots_required: {}", __FUNCTION__, free_space, num_slots_required);

    // Check local ptrs first, then DRAM for required free space for write-combine transaction.
    if (free_space < num_slots_required){

        // Only record event if we actually waited.
        perf::record_scope record(perf::HostEventType::WAIT_FOR_QUEUE_READY_WC, d_chip_id);

        while (free_space_dram(cluster) < num_slots_required) {
            log_trace(tt::LogLoader, "Chip {} epoch queue for worker core {}-{} at addr=0x{:x} waiting for {} slots (rd={},wr={})",
                d_chip_id,
                associated_routing_core.x,
                associated_routing_core.y,
                d_addr,
                num_slots_required,
                dram_ptrs.rd_ptr,
                dram_ptrs.wr_ptr);
        }
    }
}

// Flush WC buffer on host to device for CMDS. First wait for required number of epoch cmd queue slots.
void tt_epoch_queue::flush_cmds(tt_cluster* cluster, bool last_send_epoch_cmd) {

    int num_cmds_to_flush = occupancy();
    if (num_cmds_to_flush == 0) {
        return;
    }

    log_trace(tt::LogLoader, "{} - chip/core: {} Starting for num_cmds_to_flush: {} wc_window_size: {}",
        __FUNCTION__, get_chip_and_associated_routing_core().str(), num_cmds_to_flush, wc_window_size);

    log_assert(!flushed_cmds, "Attempting to flush cmds again without flushing wrptrs, unexpected");
    log_assert(wc_window_size != 0, "Flushing epoch cmds from host queue to DRAM expects WC to be enabled");
    log_assert(num_cmds_to_flush <= wc_window_size, "WC occupancy {} exceeds window size {}.", num_cmds_to_flush, wc_window_size);

    wait_for_free_space(cluster, num_cmds_to_flush);
    push_to_dram(cluster, nullptr, false, last_send_epoch_cmd);
    flushed_cmds = true;
}

// Flush WC buffer on host to device of WRPTR updates. First wait for required number of epoch cmd queue slots.
void tt_epoch_queue::flush_wrptr(tt_cluster* cluster, bool ordered_write, bool last_send_epoch_cmd) {

    int num_cmds_to_flush = occupancy();
    if (num_cmds_to_flush == 0) {
        return;
    }

    log_trace(tt::LogLoader, "{} - chip/core: {} Starting for num_cmds_to_flush: {} wc_window_size: {}",
        __FUNCTION__, get_chip_and_associated_routing_core().str(), num_cmds_to_flush, wc_window_size);

    log_assert(flushed_cmds, "Attempting to flush wrptr without flushing cmds first, unexpected");

    tt_driver_atomics::sfence();
    // Required to ensure Cmd and Wrptr update to DRAM on remote chips are ordered.
    bool ordered_with_prev_remote_write = !mmio_chip && ordered_write;
    update_write_ptr_in_dram(cluster, last_send_epoch_cmd, ordered_with_prev_remote_write);
    pop(num_cmds_to_flush);
    flushed_cmds = false;
}

int tt_epoch_queue::get_num_slots() {
    return slots;
}

bool tt_epoch_queue::is_totally_empty() {
    return hexq.empty();
}

std::shared_ptr<tt_hex> tt_epoch_queue::get_hex_ptr() {
    if (hexq.size() == 0) {
        throw std::runtime_error("Error: tt_epoch_queue is empty.");
    }
    return(hexq[0]);
}

tt_xy_pair tt_epoch_queue::get_associated_routing_core() {
    return associated_routing_core;
}

inline tt_cxy_pair tt_epoch_queue::get_chip_and_associated_routing_core() {
    return {d_chip_id, associated_routing_core};
}

void tt_epoch_queue::assign_epoch_info_to_queue_slot(const tt_epoch_program_info& info){
    // Update the epoch_id at this index from std::numeric_limits<int>::max() to an actual epoch-id
    // Used to determine if pushing a new epoch can cause aliasing.
    // When write-combine feature is enabled, dram_wrptr isn't updated until later (flush) so must use WC buffer occupancy.
    const int slot_idx = wc_window_size > 0 ? (dram_ptrs.wr_ptr + occupancy()) % slots : dram_ptrs.wr_ptr % slots;
    binaries_in_use.at(slot_idx) = info.name;
    epoch_ids_in_queue.at(slot_idx) = info.epoch_id;
}

void tt_epoch_queue::update_num_commands_per_binary(tt_cluster* cluster, bool io_queue_update_cmd){ 
    if(!(io_queue_update_cmd or num_valid_cmds)) return; // This queue does not have any valid commands that reference binaries. Don't need to get rd_ptr from device
    get_rd_ptr_dram(cluster);
}

// During initialization, runtime must wait until NCRISC has initialized Shadow Epoch Queue in L1 memory to be 0 before using it.
void tt_epoch_queue::wait_for_ncrisc_init_queue(tt_cluster *cluster, int timeout_in_seconds) {

    tt_cxy_pair l1_core = get_chip_and_associated_routing_core();
    // log_trace(tt::LogLoader, "Epoch Queue Shadow in L1, going to sync on NCRISC mailbox for l1_core: {}", l1_core.str());

    bool timeout_expired = false;
    auto timeout_in_ms = 1000 * timeout_in_seconds;
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();

    vector<uint32_t> mem_vector;
    bool ready = false;
    while (!ready){
        cluster->read_dram_vec(mem_vector, l1_core, l1_mem::address_map::NCRISC_FIRMWARE_BASE + 4, 4);
        ready = (mem_vector.at(0) == 0xF);
        // log_trace(tt::LogLoader, "Syncing for l1_core: {} got MemVector: {} Ready: {}", l1_core.str(), mem_vector, ready);

        if (!ready && timeout_in_ms > 0) {
            auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
            if (elapsed_time_in_ms > std::chrono::milliseconds{timeout_in_ms}){
                log_fatal("{} - Exceeded timeout of {} seconds waiting for NCRISC worker core {} epoch queue to be initialized. Device may need warm-reset.",
                    __FUNCTION__, timeout_in_seconds, l1_core.str());
                break;
            }
        }
    }
}


tt_epoch_queue::~tt_epoch_queue() {
    hexq.clear();
}
// ---------------------------------------------------------------------------
// Epoch control
// ---------------------------------------------------------------------------

void tt_epoch_control::update_rd_ptr_for_queues_with_valid_cmds(){
    for(int i = 0; i < (hexqs).size(); i++){
        if(hexqs[i] -> num_valid_cmds) {
            (hexqs[i]) -> get_rd_ptr_dram(cluster);
        }
    }
}

vector<uint32_t> tt_epoch_control::get_valid_qcmd(tt_xy_pair dram_xy, uint64_t dram_addr, bool perf_en, uint16_t overlay_decouple_mask) {
    vector<uint32_t> qcmd(3, 0);
    qcmd[0] = (uint32_t) (dram_addr & 0xffffffff);
    qcmd[1] = (epoch_queue::EpochCmdValid << 28) | (dram_xy.y << 22) | (dram_xy.x << 16) |
              (uint32_t)((dram_addr & (uint64_t)0x0000ffff00000000) >> 32);
    qcmd[2] = (uint32_t) (
        (perf_en ? 0xff : 0) |
        (uint32_t(overlay_decouple_mask & 0xffff) << 8));
    return qcmd;
}

vector<uint32_t> tt_epoch_control::get_invalid_qcmd() {
    static const vector<uint32_t> qcmd = {0, epoch_queue::EpochCmdNotValid << 28};
    return qcmd;

}

vector<uint32_t> tt_epoch_control::get_endprog_qcmd() {
    static const vector<uint32_t> qcmd = {0, epoch_queue::EpochCmdEndProgram << 28};
    return qcmd;
}


vector<uint32_t> tt_epoch_control::get_loopstart_qcmd(uint32_t num_loops) {
    vector<uint32_t> qcmd = {0, (epoch_queue::EpochCmdLoopStart << 28) | (num_loops & 0xfffffff)};
    return qcmd;
}

vector<uint32_t> tt_epoch_control::get_loopend_qcmd() {
    static const vector<uint32_t> qcmd = {0, epoch_queue::EpochCmdLoopEnd << 28};
    return qcmd;

}

tt_epoch_control::tt_epoch_control(tt_cluster *cluster, int chip_id) :
    cluster(cluster), bin_q_ptrs(0xabadface), cache(std::string("EpochBinaryCache_device_") + std::to_string(chip_id), epoch_queue::get_epoch_bin_num_slots(), false), curr_gen_id(0), sync_gen_id(0) {
    log_assert(cluster != nullptr, "Expected cluster to be populated");
    associated_chip = chip_id;
    mmio_chip = cluster->get_cluster_desc()->is_chip_mmio_capable(associated_chip);

    // Desired WC window size for all queues. May be enabled/disabled depending on conditions.
    auto target_size = parse_env("TT_BACKEND_EPOCH_QUEUE_WC_WINDOW_SIZE", 0xFFFF);
    auto epoch_q_num_slots = epoch_queue::get_epoch_q_num_slots();
    epoch_queue_wc_window_size_target = std::min(epoch_q_num_slots, target_size);
    epoch_queue_wc_enable_for_mmio_chips = parse_env("TT_BACKEND_EPOCH_QUEUE_WC_ENA_MMIO", false);

}

vector<tt_epoch_queue *> *tt_epoch_control::get_hexqs_ptr() {
    return(&hexqs);
}

int tt_epoch_control::occupancy() {
    log_assert(hexqs.size() > 0, "Expected queue to be initialized");
    return(hexqs[0]->occupancy());
}

bool tt_epoch_control::all_empty() {
    vector <tt_epoch_queue *>::iterator it;
    bool empty = true;

    if (hexqs.empty()) {
        empty = true;
    } else {
        for(it=hexqs.begin();it!=hexqs.end();++it)
        {
            if(!(*it)->is_totally_empty()) empty = false;
        }
    }
    return(empty);
}

bool tt_epoch_control::all_not_empty() {
    vector <tt_epoch_queue *>::iterator it;
    bool not_empty = true;

    if (hexqs.empty()) {
        not_empty = false;
    } else {
        for (it=hexqs.begin(); it!=hexqs.end(); ++it) {
            if ((*it)->is_totally_empty()) not_empty = false;
        }
    }
    return(not_empty);
}

bool tt_epoch_control::all_not_full_dram() {
    log_assert(hexqs.size() > 0, "Expected queue to be initialized");
    bool all_not_full = true;
    vector <tt_epoch_queue *>::iterator it;
    buda_soc_description &sdesc = cluster -> get_soc_desc(associated_chip);
    
    for (it=hexqs.begin(); it!=hexqs.end(); ++it) {
        if ((*it)->is_active()) {
            if ((*it)->is_full_dram(cluster)) {
                log_trace(tt::LogLoader, "Epoch queue at chip={} chan={} sub_ch={} addr=0x{:x} for worker/eth core {} is full (not ready for push)",
                    associated_chip,
                    (*it)->d_chan, (*it)->d_subchannel, (*it)->d_addr,
                    (*it)->associated_routing_core.str());
                all_not_full = false;
            }
        }
    }
    return all_not_full;
}

bool tt_epoch_control::check_epoch_done() {
    log_assert(hexqs.size() > 0, "Expected queue to be initialized");
    vector <tt_epoch_queue *>::iterator it;
    bool all_epoch_qs_empty = true;
    int gen_id_to_sync = get_curr_gen_id();
    buda_soc_description &sdesc = cluster -> get_soc_desc(associated_chip);
    for (it=hexqs.begin(); it!=hexqs.end(); ++it) {
        if ((*it)->is_active()) {
            bool is_epoch_q_empty = (*it)->is_empty_dram(cluster);

            if (!is_epoch_q_empty) {
                log_trace(tt::LogLoader, "Epoch queue at chip={} chan={} sub_ch={} addr=0x{:x} for worker/eth core {} is full (core busy)",
                    associated_chip,
                    (*it)->d_chan, (*it)->d_subchannel, (*it)->d_addr,
                    (*it)->associated_routing_core.str());
                all_epoch_qs_empty = false;
                break;
            }
        }
    }
    if (all_epoch_qs_empty) {
        // update synchronized generation id
        update_sync_gen_id(gen_id_to_sync);
    }
    return all_epoch_qs_empty;
}

void tt_epoch_control::wait_for_cmds_remaining(int num_cmds) {
    log_assert(hexqs.size() > 0, "Expected queue to be initialized");
    vector <tt_epoch_queue *>::iterator it;
    bool all_epoch_qs_advanced = false;
    int gen_id_to_sync = get_curr_gen_id();

    auto pending = hexqs;
    buda_soc_description &sdesc = cluster->get_soc_desc(associated_chip);

    while (!all_epoch_qs_advanced) {
        for (it = pending.begin(); it != pending.end();){
            if (!(*it)->is_active()) {
                it = pending.erase(it); // exclude unused ones
            } else if ((*it)->occupancy_dram(cluster) <= num_cmds) {
                it = pending.erase(it);
            } else {
                log_trace(tt::LogLoader, "Chip {} epoch queue for worker core {}-{} at addr=0x{:x} is not empty (rd={},wr={})",
                    associated_chip,
                    (*it)->associated_routing_core.x,
                    (*it)->associated_routing_core.y,
                    (*it)->d_addr,
                    (*it)->dram_ptrs.rd_ptr,
                    (*it)->dram_ptrs.wr_ptr
                    );

                it++;
            }
        }
        all_epoch_qs_advanced = (pending.size() == 0);
    }
    // full wait for idle sync has been performed, safe to advance gen id
    if (num_cmds == 0) {
        // update synchronized generation id
        update_sync_gen_id(gen_id_to_sync);
    }
}

void tt_epoch_control::incr_wr_ptr() {
    bin_q_ptrs.incr_get_wr();
}

void tt_epoch_control::init_epoch_ctrl(int cmd_slots, int cmd_size, int bin_slots, bool enable_mru_with_backtrace_bin_cache)
{
    buda_soc_description &sdesc = cluster -> get_soc_desc(associated_chip);
    tt_cluster_description *cluster_desc = cluster->get_cluster_desc();
    bool is_mmio_chip = cluster_desc->is_chip_mmio_capable(associated_chip);

    bin_q_ptrs = tt_queue_ptr(bin_slots);
    grid_shape = {epoch_queue::GridSizeRow, epoch_queue::GridSizeCol};
    
    if(enable_mru_with_backtrace_bin_cache) {
        cache.use_lru = false;
    }
    // We need to know if we are a silicon device until we can enable ethernet cores in Versim.
    // See the note in the routing_core assignment branch below
    bool is_silicon = this->cluster->type == TargetDevice::Silicon;
    unsigned num_worker_hexqs = 0;
    // Always create a grid of constant size
    // Iterate through every Q and if associated core isn't present, mark it as invalid
    num_cmds_per_q_update_blob = std::vector<unordered_map<string, int>>(grid_shape[0] * grid_shape[1], unordered_map<string, int>()); 
    for(int i=0;i<grid_shape[0];++i)
    {
        for(int j=0;j<grid_shape[1];++j)
        {
            tt_xy_pair routing_core = tt_xy_pair(j, i);

            if (sdesc.cores.find(routing_core) == sdesc.cores.end()) {
                // Mark queue inactive since routing core isn't present in this config
                routing_core = tt_xy_pair(0xff, 0xff);
            } else if (sdesc.cores[routing_core].type != CoreType::WORKER) {
                // At the moment, it's not possible to simulate an ethernet core in Versim. Therefore, if we enable the command queue for the ethernet core in Versim,
                // we will end up hanging in any wormhole versim run as the ethernet cores will not be available to complete their (IDLE) epochs.
                //
                // After the RTL is modified and enabled to be versim compatible, this check against the target device can be removed.
                // NOTE: When enabling erisc, the condition should be `if (gstate_ptr->config.arch.target_device != TargetDevice::Silicon || gstate_ptr->soc_descriptor->cores[routing_core].type != CoreType::ETH)`
                // For now it's not because the erisc FW isn't enabled.

                if (!is_silicon || sdesc.cores[routing_core].type != CoreType::ETH) {
                    // Mark queue inactive since routing core isn't worker core or ethernet_core
                    routing_core = tt_xy_pair(0xff, 0xff);
                }
            } else {
                // just keeping track of active queues to make sure we allocated a hexq for each worker core below
                num_worker_hexqs++;
            }
            std::string cache_name = "IOQueueUpdateBinaryCache_device_" + std::to_string(associated_chip) + "_" + routing_core.str();
            io_queue_update_caches.push_back(tt_epoch_binary_cache(cache_name, epoch_queue::get_epoch_io_queue_update_num_slots(), enable_mru_with_backtrace_bin_cache)); // create an external blob binary cache for each epoch queue. Set size to cmd slots for now
            io_q_update_ptrs.push_back(tt_queue_ptr(epoch_queue::get_epoch_io_queue_update_num_slots()));
            uint32_t queue_index = i * grid_shape[1] + j;
            tt_epoch_queue* queue = new tt_epoch_queue(cmd_slots, cmd_size, routing_core, is_mmio_chip, &num_cmds_per_binary, &num_cmds_per_epoch_id, &num_cmds_per_q_update_blob.at(queue_index));
            hexqs.push_back(queue);
        }
    }
    log_assert(num_worker_hexqs == sdesc.worker_grid_size.x * sdesc.worker_grid_size.y, "Not every worker core is allocated a hexq");
}

void tt_epoch_control::push_command(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> cmd_hex, bool last_send_epoch_cmd) {
    dram_profiler.add_report_for_command_queue_entry(q_ptr, cmd_hex);
    q_ptr->push_command(cmd_hex, cluster, last_send_epoch_cmd);
}

void tt_epoch_control::push_command_and_update_l1_wptr(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> cmd_hex) {
    push_command(q_ptr, cmd_hex, true);

    if (!is_epoch_queue_wc_enabled) {
        q_ptr->update_write_ptr_in_l1(cluster);
    }
}

// Optionally send Wrptr update to L1 Shadow Wrptr for Epoch Cmd Queue
void tt_epoch_control::update_all_shadow_l1_wrptrs() {

    const auto &active_queues_with_l1_wrptr_shadow = get_active_queues_with_shadow_l1_wrptr();

    if (active_queues_with_l1_wrptr_shadow.size() > 0) {
        // Wait for previous cmd pushes to epoch queue in DRAM to be flushed.
        cluster->wait_for_non_mmio_flush();
        // Iterate over queues with L1 wrptr shadowed in L1 and send updates.
        for (int i = 0; i < active_queues_with_l1_wrptr_shadow.size(); ++i) {
            tt_epoch_queue *q_ptr = active_queues_with_l1_wrptr_shadow.at(i);
            q_ptr->update_write_ptr_in_l1(cluster, i == (active_queues_with_l1_wrptr_shadow.size() - 1));
        }
    }
}

// Send Wrptr update to Epoch Cmd Queue in DRAM
void tt_epoch_control::update_all_dram_wrptrs(vector<tt_epoch_queue*> active_occupied_queues) {

    // Optimization to do single non-mmio-flush per chip. Once FW6.4 is more widely adopted
    // could change WC Flush to use ordered-write feature, issuing cmd,wrptr,cmd,wrptr ordered
    // instead of all-cmds, all-wrptrs.
    if (!mmio_chip) {
        cluster->wait_for_non_mmio_flush();
    }

    for (int i = 0; i < active_occupied_queues.size(); i++) {
        tt_epoch_queue *q_ptr = active_occupied_queues.at(i);
        q_ptr->flush_wrptr(cluster, false, i == (active_occupied_queues.size()-1));
    }
}


// Push the same epoch cmd to all cores (eth and tensix) and update l1 wrptr if needed.
void tt_epoch_control::push_command_to_all_workers_and_update_l1_wptr(const vector<uint32_t> &qcmd) {

    const auto &active_queues = get_active_queues();

    for (int i = 0; i < active_queues.size(); ++i) {
        tt_epoch_queue *q_ptr = active_queues.at(i);
        tt_xy_pair routing_core(q_ptr->associated_routing_core.x, q_ptr->associated_routing_core.y);
        push_command(q_ptr, std::make_shared<tt_hex>(qcmd, tt_hex_type::Misc, routing_core), i == (active_queues.size() - 1));
    }
 
    // Epoch Queue Write Combine flush path will take care of non-mmio-flush and L1 ptr update.
    if (!is_epoch_queue_wc_enabled) {
        update_all_shadow_l1_wrptrs();
    }

    // assign new generation id after an epoch cmd is pushed
    update_curr_gen_id(get_next_gen_id());
}

// Determine if incoming epoch_id will alias with currently active epochs on device. The incoming epoch will be prevented from running alongside
// aliased epoch either by inserting a Full Grid Sync cmd or Stalling on host the dispatch of epoch cmds until earlier alias epoch is finished.
bool tt_epoch_control::has_epoch_id_alias_hazard_with_device(tt_epoch_id_aliasing_sync_tracker& sync_tracker, const int& wrapped_epoch_id, const int& epoch_id, const string& epoch_name, bool avoid_via_full_grid_sync) {

    // Checking if epoch_id aliasing hazard exists depends on avoidance mechanism (host-stall vs full-grid-sync).
    //  1. When avoiding via full-grid-sync, we can expect to have other aliased epochs with the same wrapped id, or even if the same
    //     epoch on device already, because full-grid-syncs will be inserted as needed to avoid them simultaneously running. They are
    //     determined as needed if "last sync idx" does not include the idx of the most recent wrapped epoch_id being aliased with.
    //  2. When avoiding via host-stall, we can expect to see only other aliased epochs with the same wrapped id on device, but
    //     do not consider earlier instances of the same epoch that may still be running, since this is not a hazard.
    bool has_hazard = false;
    bool has_active_cmds_for_wrapped_epoch_id = num_cmds_per_epoch_id.count(wrapped_epoch_id) and num_cmds_per_epoch_id.at(wrapped_epoch_id) > 0;

    if (has_active_cmds_for_wrapped_epoch_id) {
        if (avoid_via_full_grid_sync) {
            bool sync_needed = true;
            sync_needed &= sync_tracker.wrapped_epoch_id_to_last_idx_map.count(wrapped_epoch_id) and sync_tracker.last_sync_idx < sync_tracker.wrapped_epoch_id_to_last_idx_map.at(wrapped_epoch_id); // Prev sync not enough
            sync_needed &= sync_tracker.wrapped_epoch_id_to_last_epoch_id_map.count(wrapped_epoch_id) && sync_tracker.wrapped_epoch_id_to_last_epoch_id_map.at(wrapped_epoch_id) != epoch_id; // Not same epoch
            has_hazard = sync_needed;
        } else {
            bool has_active_cmds_for_curr_epoch = num_cmds_per_binary.count(epoch_name) and num_cmds_per_binary.at(epoch_name) > 0;
            has_hazard = !has_active_cmds_for_curr_epoch;
        }
    }

    return has_hazard;
}

// Used for runtime checking when program looping on device, that epoch queue can fit all commands.
void tt_epoch_control::record_wr_ptr_device_loop_start_cmds() {

    const auto &active_queues = get_active_queues();

    for (int i = 0; i < active_queues.size(); ++i) {
        tt_epoch_queue *q_ptr = active_queues.at(i);
        q_ptr->device_loop_stack_depth++;
        if (q_ptr->device_loop_stack_depth == 1) {
            q_ptr->wr_ptr_device_loop_start_cmd = q_ptr->get_wr_ptr();
            // log_info(tt::LogLoader, "LoopStart Depth: {} Wrptr: {} for {}", target_queue->device_loop_stack_depth, target_queue->wr_ptr_device_loop_start_cmd, tt_xy_pair(j, i).str());
        }
    }

}

// Used for runtime checking when program looping on device, that epoch queue can fit all commands. Clear wrptr at loop end, and display analytics.
void tt_epoch_control::clear_wr_ptr_device_loop_start_cmds(std::string prog_name) {

    int max_epoch_cmds_in_loop = 0;

    const auto &active_queues = get_active_queues();

    for (int i = 0; i < active_queues.size(); ++i) {
        tt_epoch_queue *q_ptr = active_queues.at(i);
        q_ptr->device_loop_stack_depth--;
        if (q_ptr->device_loop_stack_depth == 0) {

            if constexpr (varinst_cmd_debug) {
                auto core_str           = q_ptr->get_chip_and_associated_routing_core().str();
                int curr_wr_ptr         = q_ptr->get_wr_ptr();
                int start_cmd_wr_ptr    = q_ptr->wr_ptr_device_loop_start_cmd;
                int num_cmds            = curr_wr_ptr > start_cmd_wr_ptr ? (curr_wr_ptr - start_cmd_wr_ptr) + 1 : (1 + curr_wr_ptr + q_ptr->get_num_slots()) - start_cmd_wr_ptr ;
                max_epoch_cmds_in_loop  = num_cmds > max_epoch_cmds_in_loop ? num_cmds : max_epoch_cmds_in_loop;
                // log_trace(tt::LogLoader, "Analytics. {} EpochCmdLoopEnd have curr_wr_ptr: {} start_cmd_wr_ptr: {} => {} EpochCmds between LoopStart/LoopEnd", core_str, curr_wr_ptr, start_cmd_wr_ptr, num_cmds);
            }
            q_ptr->wr_ptr_device_loop_start_cmd = -1;
        }

    }

    if (varinst_cmd_debug && max_epoch_cmds_in_loop > 0) {
        log_info(tt::LogLoader, "Analytics for Program Looping on Device : {} device_id: {} Max num (across cores) of Epoch Commands between LoopStart/LoopEnd : {}", prog_name, associated_chip, max_epoch_cmds_in_loop);
    }
}

// Flush all Write Combine buffers to device DRAM for all not-empty Epoch Queues on device, and update L1 shadow wrptr.
void tt_epoch_control::flush_all_wc_epoch_queues_to_dram() {

    if (!is_epoch_queue_wc_enabled) {
        return;
    }

    // For two passes so last flag can be set on final transfer for chip.
    vector<tt_epoch_queue*> active_occupied_queues = {};

    // Step 1: Send epoch commands to Epoch CMD Queue in DRAM
    {
        log_debug(tt::LogLoader, "Flushing all WC epoch queues to DRAM for device: {}", associated_chip);

        perf::record_scope record("push_all_wc_epoch_q_cmds_to_dram");
        PROFILE_SCOPE(microseconds);

        for (auto &q_ptr : get_active_queues()) {
            if (q_ptr->occupancy() > 0) {
                active_occupied_queues.push_back(q_ptr);
            }
        }

        for (int i = 0; i < active_occupied_queues.size(); i++) {
            tt_epoch_queue *q_ptr = active_occupied_queues.at(i);
            q_ptr->flush_cmds(cluster, i == (active_occupied_queues.size()-1));
        }
    }

    // Step 2: Update Wrptr of Epoch CMD Queue in DRAM
    if (active_occupied_queues.size() > 0) {
        perf::record_scope record("update_all_wc_epoch_q_write_ptrs_in_dram");
        PROFILE_SCOPE(microseconds);
        update_all_dram_wrptrs(active_occupied_queues);
    }

    // Step 3: Update Shadow Wrptr in L1 if commands were flushed previously. Just send to all active queues
    // with l1 shadow ptr enabled instead of filtering by active occupied queues. Overlap should be high.
    if (active_occupied_queues.size() > 0) {
        perf::record_scope record("update_all_wc_epoch_q_write_ptrs_in_l1");
        PROFILE_SCOPE(microseconds);
        update_all_shadow_l1_wrptrs();
    }
}


tt_queue_header& tt_epoch_control::get_cached_io_qs(const tt_queue_info &queue_info) {
    std::string base_queue_name = get_base_queue_name(queue_info);
    return cached_io_qs[base_queue_name];
}


void tt_epoch_control::set_cached_io_qs_wrap(const tt_queue_info &queue_info, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask) {
    std::string base_queue_name = get_base_queue_name(queue_info);

    if (cached_io_qs.find(base_queue_name) != cached_io_qs.end()) {

        tt_queue_header_wrap combined_header_wrap = cached_io_qs.at(base_queue_name); // Uses new constructor

        if (header_mask.has_full()) {
            combined_header_wrap.set_vec({header_wrap.get_vec(), 0});
        } else {
            if (header_mask.has_global_rd_ptr()) {
                combined_header_wrap.set_vec(header_wrap.get_rdptr_vec());
            }
            if (header_mask.has_global_wr_ptr()) {
                combined_header_wrap.set_vec(header_wrap.get_wrptr_vec());
            }
            if (header_mask.has_local_settings()) {
                combined_header_wrap.set_vec(header_wrap.get_settings_vec());
            } else if (header_mask.has_local_rd_ptr()) {
                combined_header_wrap.set_vec(header_wrap.get_lrdptr_vec());
            }
        }
        cached_io_qs[base_queue_name] = combined_header_wrap.header;
    } else {
        cached_io_qs[base_queue_name] = header_wrap.header;
    }
}

bool tt_epoch_control::is_queue_in_use(const tt_queue_info &queue_info) {
    std::string base_queue_name = get_base_queue_name(queue_info);
    return (in_use_io_qs.find(base_queue_name) != in_use_io_qs.end());
}

void tt_epoch_control::set_queue_in_use(const tt_queue_info &queue_info) {
    std::string base_queue_name = get_base_queue_name(queue_info);
    in_use_io_qs.insert(base_queue_name);
}

void tt_epoch_control::clear_all_queues_in_use() {
    in_use_io_qs.clear();
}

std::shared_ptr<tt_hex> tt_epoch_control::get_hex_ptr(int index) {
    log_assert(index < hexqs.size(), "Index is out of bounds for hexq");
    return(hexqs[index]->get_hex_ptr());
}

tt_epoch_queue *tt_epoch_control::get_q_ptr(int index) {
    log_assert(index < hexqs.size(), "Index is out of bounds for hexq");
    return(hexqs[index]);
}

vector<tt_epoch_queue *> &tt_epoch_control::get_active_queues() {
    if (active_hexqs.empty()) {
        for (int i = 0; i < grid_shape[0]; i++) {
            for (int j = 0; j < grid_shape[1]; j++) {
                int queue_index = i * grid_shape[1] + j;
                tt_epoch_queue* target_queue = get_q_ptr(queue_index);
                if(target_queue -> is_active()) {
                    active_hexqs.push_back(target_queue);
                }
            }
        }
    }
    return active_hexqs;
}

// Cache the active queues with l1 shadow wrptr enabled requiring updates.
vector<tt_epoch_queue *> &tt_epoch_control::get_active_queues_with_shadow_l1_wrptr() {
    if (active_hexqs_wth_shadow_l1_wrptr.empty()) {
        for (auto &q_ptr : get_active_queues()) {
            if (q_ptr->is_shadow_l1_wrptr_enabled()) {
                active_hexqs_wth_shadow_l1_wrptr.push_back(q_ptr);
            }
        }
    }
    return active_hexqs_wth_shadow_l1_wrptr;
}

bool tt_epoch_control::is_epoch_in_progress() {
    // check if current generation is ahead of sync'd generation
    return (curr_gen_id > sync_gen_id);
}

void tt_epoch_control::update_curr_gen_id(int gen_id) {
    curr_gen_id = gen_id;
    log_assert(curr_gen_id >= sync_gen_id, "Active epoch generation must be greater or equal to last sync'd generation!");
}

void tt_epoch_control::update_sync_gen_id(int gen_id) {
    sync_gen_id = gen_id;
    log_assert(sync_gen_id <= curr_gen_id, "Sync epoch generation must be less or equal to current active generation!");
}

int tt_epoch_control::get_next_gen_id() {
    return curr_gen_id+1;
}

int tt_epoch_control::get_curr_gen_id() {
    return curr_gen_id;
}

int tt_epoch_control::get_sync_gen_id() {
    return sync_gen_id;
}

tt_epoch_control::~tt_epoch_control() {
    vector<tt_epoch_queue *>::iterator it;
    for (it = hexqs.begin(); it != hexqs.end(); ++it) {
        delete *it;
    }
}

// The first two words in this command represent:
// - When updating a single buffer:  The address and dram core coordinates of the queue header
// - When updating multiple buffers: The address and dram core coordinates of the binaries (where the queue header addresses are stored)
vector<uint32_t> get_q_update_read_qcmd(
    tt_xy_pair dram_xy,
    uint64_t queue_header_or_binary_addr,
    uint8_t num_buffers,
    uint16_t reader_index,
    uint16_t num_readers,
    const tt_queue_header_wrap &queue_wrap,
    const tt_queue_header_mask &mask) {
    
    vector<uint32_t> qcmd(8, 0);
    qcmd[0] = (uint32_t) (queue_header_or_binary_addr & 0xffffffff);
    qcmd[1] = (epoch_queue::EpochCmdIOQueueUpdate << 28) | (dram_xy.y << 22) | (dram_xy.x << 16) |
              (uint32_t)((queue_header_or_binary_addr & (uint64_t)0x0000ffff00000000) >> 32);
    vector<uint32_t> queue_header = queue_wrap.get_vec();
    log_assert(queue_header.size() == 8, "Currently queue header size must be equal to 32B");
    uint8_t update_mask = mask.get();
    qcmd[2] =   (num_buffers            & 0xff)        |
                ((reader_index          & 0xff) << 8)  |
                ((num_readers           & 0xff) << 16) |
                ((update_mask           & 0xff) << 24);

    // Currently only the first 5 words of the header are included in this command
    // cout << "KD-command = " << endl;
    for (uint word = 0; word < 5; word++) {
        qcmd[3+word] = queue_header.at(word);
        // cout << qcmd[3+word] << endl;
    }
    
    return qcmd;
}

vector<uint32_t> tt_epoch_loader::get_q_header_binaries(
    const tt_queue_allocation_info &alloc_info, const tt_xy_pair &core, const buda_soc_description &soc_descriptor) {
    uint64_t q_addr = alloc_info.address;

    if (soc_descriptor.get_pcie_core() == core) {
        q_addr += soc_descriptor.get_noc2host_offset(alloc_info.channel);
    }
    vector<uint32_t> qcmd(2, 0);
    qcmd[0] = (q_addr & 0xffffffff);
    qcmd[1] = (core.y << 22) | (core.x << 16) | (q_addr & (uint64_t)0x0000ffff00000000) >> 32;
    return qcmd;
}

vector<uint32_t> tt_epoch_loader::get_buffer_update_read_binaries(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, const map<string, tt_queue_wrap> &queues, const buda_soc_description &sdesc) {
    vector<uint32_t> qcmd(2 * buffers.size(), 0);
    std::uint32_t qcmd_vector_idx = 0;
    for(const auto& buf : buffers) {
        tt_xy_pair core = queues.at(std::get<0>(buf)).my_queue_info.loc == QUEUE_LOCATION::DRAM ? sdesc.get_core_for_dram_channel(std::get<1>(buf).channel, 0) : sdesc.get_pcie_core();
        
        vector<uint32_t> q_bin = get_q_header_binaries(std::get<1>(buf), core, sdesc);
        std::memcpy(qcmd.data() + qcmd_vector_idx, q_bin.data(), q_bin.size() * sizeof(uint32_t));
        qcmd_vector_idx += q_bin.size();
    }
    return qcmd;
}
vector<uint32_t> tt_epoch_loader::get_q_update_read_binaries(
    const tt_queue_info &queue_info, const buda_soc_description &sdesc) {
    const std::vector<tt_queue_allocation_info> &alloc = queue_info.alloc_info;
    const uint num_buffers = alloc.size();

    // Each address will occupy two words
    vector<uint32_t> qcmd(2 * num_buffers, 0);
    for (uint buf_idx = 0; buf_idx < num_buffers; buf_idx++) {
        tt_xy_pair core = (queue_info.loc == QUEUE_LOCATION::DRAM)
                              ? sdesc.get_core_for_dram_channel(alloc.at(buf_idx).channel, 0)
                              : sdesc.get_pcie_core();
        vector<uint32_t> q_bin = get_q_header_binaries(alloc.at(buf_idx), core, sdesc);
        std::copy(q_bin.begin(), q_bin.end(), qcmd.begin() + 2 * buf_idx);
    }
    return qcmd;
}

// ---------------------------------------------------------------------------
// Epoch Loader
// ---------------------------------------------------------------------------
tt_epoch_loader::tt_epoch_loader(tt_cluster* cluster, string output_dir, std::set<int> target_device_ids) : cluster(cluster), output_dir(output_dir) {
    event_counters.insert({"epoch_cache_hit", 0});
    event_counters.insert({"epoch_cache_miss", 0});
    event_counters.insert({"epoch_barrier", 0});
    event_counters.insert({"epoch_count", 0});
    event_counters.insert({"full_grid_syncs", 0});
    event_counters.insert({"epoch_id_alias_hazards", 0});

    auto &sdesc_per_chip = cluster->get_sdesc_for_all_devices();
    
    // Covers all device_ids (graphs, and queues)
    for (auto &device_id : target_device_ids){
        log_assert(sdesc_per_chip.find(device_id) != sdesc_per_chip.end(), "workload device_id: {} not found in cluster. (Cluster has {} devices)", device_id, sdesc_per_chip.size());
        tt_epoch_dram_manager* mgr = new tt_epoch_dram_manager(device_id, cluster -> get_soc_desc(device_id));
        tt_epoch_control* ctrl = new tt_epoch_control(cluster, device_id);
        epoch_ctrl.insert({device_id, ctrl});
        dram_mgr.insert({device_id, mgr});
        target_devices.insert(device_id);
    }
    dram_profiler.record_initial_state(chan_struct_map, cluster -> get_all_chips()); //Moving this outside the dram_mgr constructor, as as the chan_struct_map should be fully populated for this step
    
    // Setup and register early exit handler to write dram_profiler report to disc in case an error/hang is encountered
    auto early_exit_handler = [] (int signum) {
        dram_profiler.print_loader_json_report();
        dram_profiler.dram_profiler_report.clear();
        
        // Re-raise the signal to trigger the default handler
        restore_handler_and_raise(signum);
    };
    register_early_exit_function(early_exit_handler);
}

tt_epoch_loader::~tt_epoch_loader() {
    for (auto it = epoch_ctrl.begin(); it != epoch_ctrl.end(); ++it) {
        delete it->second;
    }
    for (auto it = dram_mgr.begin(); it != dram_mgr.end(); ++it) {
        delete it->second;
    }
    dram_profiler.print_loader_json_report();
    dram_profiler.dram_profiler_report.clear();
}

void tt_epoch_loader::set_optimization_level(int level) {
    log_info(tt::LogLoader, "Set OPT LEVEL to {}", level);
    if (level == 0) {   // No optimizations
        check_binaries = false;  // cannot reliably check due to PCIe RO reads issue #369
    }
    if (level >= 1) {   // Epoch caching enabled, preload epoch binaries, and resource overlap checks
        enable_epoch_caching = true;
        enable_epoch_preloading = true;
        enable_optimized_barriers = true;
        enable_runtime_hazard_checks = true; // can be used to guard potentially expensive runtime hazard checks.
    }
    if (level >= 2) {   // All prev optimizations + queue settings reuse + mru cache for epoch binaries
        enable_queue_settings_reuse = true;
        enable_mru_with_backtrace_bin_cache = true;
    }
    if (level >= 3) {   // All prev optimizations + on-device queue updates (incl varinst cmd)
        enable_hw_io_queue_update = true;
        disable_eq_shadow_l1_wrptrs = true;
    }
    if (level >= 4) {   // All prev optimization + looping-on-device (w/ varinst on device) + disable eq l1 shadow ptrs + dis hazard checks
        enable_varinst_merge_io_queue_updates = parse_env("TT_BACKEND_VARINST_MERGE_IO_QUEUE_UPDATES", false);
        bool en_legacy_looping = parse_env("NUM_EXEC_LOOP_ITERATIONS", false);
        enable_looping_on_device = !en_legacy_looping; // Not compatible with legacy/hardcoded looping feature.
        enable_mru_with_backtrace_bin_cache = false; // Not compatible with looping on device.
        enable_runtime_hazard_checks = parse_env("TT_BACKEND_ENABLE_RUNTIME_HAZARD_CHECKS", false);
        enable_write_combine_epoch_cmds = true;
    }
}
tt_epoch_control& tt_epoch_loader::get_epoch_ctrl(const int device_id) {
    return *(this->epoch_ctrl.at(device_id));
}

void tt_epoch_loader::create_and_allocate_epoch_queues(bool distribute_tables) {
    std::uint32_t epoch_q_num_slots = epoch_queue::get_epoch_q_num_slots();
    bool valid_epoch_q_num_slots = epoch_q_num_slots > 0 && ((epoch_q_num_slots & (epoch_q_num_slots - 1)) == 0);
    log_assert(valid_epoch_q_num_slots, "Number of slots in Epoch Queue must be greater than zero and a power of 2!");
    
    if (skip_device_init) return;

    // Initialize all Epoch queues with default settings
    vector<uint32_t> header_vec = {0,0,0,0,0,0,0,0};

    log_assert(!target_devices.empty(), "target_devices was empty");
    for (int device: target_devices) {
        auto &sdesc = cluster->get_soc_desc(device);
        tt_epoch_control* ctrl = epoch_ctrl[device];
        ctrl->init_epoch_ctrl(epoch_queue::get_epoch_q_num_slots(), epoch_queue::EPOCH_Q_SLOT_SIZE, epoch_queue::get_epoch_bin_num_slots(), enable_mru_with_backtrace_bin_cache);
        bool disable_shadow_l1_ptr_for_chip = disable_eq_shadow_l1_wrptrs && !cluster->get_cluster_desc()->is_chip_mmio_capable(device);

        // Allocate/Init epoch command queues
        vector<tt_epoch_queue*>::iterator it;
        for (it = ctrl->get_hexqs_ptr()->begin(); it != ctrl->get_hexqs_ptr()->end(); ++it) {
            tt_epoch_queue* queue = *it;
            if (queue->is_active()) {
                log_assert(chan_struct_map.count(ctrl->associated_chip) > 0, "requested chip id for binary not found in the DRAM allocator");
                dram_mgr[ctrl->associated_chip]->allocate_epoch_queue(queue, distribute_tables);
                queue->init_queue_ptrs(cluster);
                 // Only disable for non-mmio-chips NCRISC workers. ERIC once ready.
                if (disable_shadow_l1_ptr_for_chip && sdesc.is_worker_core(queue->associated_routing_core)) {
                    // log_trace(tt::LogLoader, "Disabling EQ Shadow L1 Wrptr for core: {} (is_eth: {} is_worker: {}) on device: {} is_mmio: {}",
                    //     queue->associated_routing_core.str(), sdesc.is_ethernet_core(queue->associated_routing_core), sdesc.is_worker_core(queue->associated_routing_core),
                    //     device, cluster->get_cluster_desc()->is_chip_mmio_capable(device));
                    queue->set_disable_shadow_l1_wrptr(true);
                }
            }
        }
        // Init epoch command sync slots
        for (int chan=0; chan<sdesc.get_num_dram_channels(); chan++) {
            tt_target_dram dram = {device, chan, 0};
            cluster->write_rolled_dram_vec(header_vec, epoch_queue::EPOCH_ALLOC_QUEUE_SYNC_SLOTS, dram, epoch_alloc_queue_sync_addr);
        }
        dram_profiler.create_worker_to_command_queue_map(ctrl->hexqs, device);
    }
    cluster->wait_for_non_mmio_flush();
    // All queues have the same value for max_offset_between_epoch_ids. Just use the first one. Used to check for epoch aliasing.
    epoch_window_size = (epoch_ctrl.at(*target_devices.begin()) -> hexqs)[0] -> max_offset_between_epoch_ids;

}

void tt_epoch_loader::create_and_allocate_io_queues(const map<string, tt_queue_wrap> &queues) {
    if (skip_io_init) return;

    // Initialize all DRAM IO queues with default settings
    vector<uint32_t> header_vec = {0,0,0,0,0,0,0,0};
    tt_queue_header_wrap header_wrap = {header_vec};

    for (auto &queue : queues) {
        std::string queue_name = queue.first;
        const tt_queue_info &queue_info = queue.second.my_queue_info;
        unordered_map<chip_id_t, uint32_t> dram_bank_size_per_chip; //Caches per chip
        unordered_map<chip_id_t, vector<int>> dram_channels_per_chip;

        if (queue_info.loc == QUEUE_LOCATION::HOST) {
            chip_id_t src_device_id = queue_info.src_device_id;
            log_assert(src_device_id != -1, "Must set src_device_id from producer op for host queues");
            log_debug(tt::LogLoader, "\tInit HOST IO queue '{}' w/ src_device_id: {}", queue_name, src_device_id);
            for (auto &alloc : queue_info.alloc_info) {
                cluster->write_sysmem_vec(header_vec, alloc.address, alloc.channel, src_device_id);
            }
        } else if (queue_info.loc == QUEUE_LOCATION::DRAM) {
            log_debug(tt::LogLoader, "\tInit DRAM IO queue '{}'", queue_name);
            uint32_t dram_bank_size;
            vector<int> dram_channels;
            if (dram_bank_size_per_chip.find(queue_info.target_device) == dram_bank_size_per_chip.end()) {
                buda_soc_description &sdesc = cluster->get_soc_desc(queue_info.target_device);
                dram_bank_size = sdesc.dram_bank_size;
                dram_channels = sdesc.get_dram_chan_map();
                dram_bank_size_per_chip[queue_info.target_device] = dram_bank_size;
                dram_channels_per_chip[queue_info.target_device] = dram_channels;
            } else {
                dram_bank_size = dram_bank_size_per_chip[queue_info.target_device];
                dram_channels = dram_channels_per_chip[queue_info.target_device];
            }
            for (auto &alloc : queue_info.alloc_info) {
                log_assert(alloc.address < dram_bank_size, "IO queue alloc.address exceeds soc_descriptor dram_bank_size");
                bool valid_dram_channel = std::find(dram_channels.begin(), dram_channels.end(), alloc.channel) != dram_channels.end();
                log_assert(valid_dram_channel, "Invalid DRAM channel specified, not in soc_descriptor. If this is versim, consider using FORCE_FULL_SOC_DESC=1 to avoid reduced soc_desc.");
                tt_target_dram dram = {queue_info.target_device, alloc.channel, 0/*subchan*/};
                cluster->write_dram_vec(header_vec, dram, alloc.address);
            }
        }
        if (target_devices.find(queue_info.target_device) != target_devices.end()) {
            epoch_ctrl.at(queue_info.target_device)->get_cached_io_qs(queue_info) = header_wrap.header;
        }
    }
}

// Wait for NCRISC to signal that epoch queues in L1 (worker cores only) are initialized to 0x0 by polling mailbox.
void tt_epoch_loader::wait_for_ncrisc_init_all_epoch_queues(bool enable_timeout) {

    int timeout_in_seconds = enable_timeout ? 30 : 0;
    log_info(tt::LogLoader, "Starting {} for {} devices (timeout_in_seconds: {}).", __FUNCTION__, target_devices.size(), timeout_in_seconds);
    for (int device: target_devices) {
        tt_epoch_control* ctrl = epoch_ctrl[device];
        vector<tt_epoch_queue*>::iterator it;
        for (it = ctrl->get_hexqs_ptr()->begin(); it != ctrl->get_hexqs_ptr()->end(); ++it) {
            tt_epoch_queue* queue = *it;

            auto queue_routing_core = queue->get_associated_routing_core();
            auto &sdesc = cluster->get_soc_desc(device);
            if (queue->is_active() && sdesc.is_worker_core(queue_routing_core)) {
                queue->wait_for_ncrisc_init_queue(cluster, timeout_in_seconds); // L1 Queues must wait for NCRISC to init memory

                // Only need to disable l1 wrptr shadow, otherwise already initialized by ncrisc.
                if (!queue->is_shadow_l1_wrptr_enabled()) {
                    queue->init_queue_ptrs_l1(cluster);
                }
            }
        }
    }
}

void tt_epoch_loader::generate_allocate_queue_binaries(
    const map<string, tt_queue_wrap> &queues, const unordered_set<string> &queues_to_dealloc, const buda_soc_description& sdesc,
    std::vector<std::unordered_set<tt_xy_pair>>& sync_cores_per_group, std::vector<tt_hex>& external_binaries_per_group, std::vector<uint32_t>& num_buffers_per_group, std::string& alloc_key) {
    log_assert(enable_hw_io_queue_update, "Dynamic allocation from device requires enable_hw_io_queue_update=true");
    log_assert(queues.size() > 0, "Cannot allocate 0 queues");
    std::string key = " ALLOC:";
    for(const auto& queue : queues) {
        key += queue.second.my_queue_info.name + ",";
    }
    alloc_key = key;
    if(queues_to_dealloc.size()) {
        key += " DEALLOC:";
        for(const auto& queue : queues_to_dealloc) {
            key += queue + ",";
        }
    }
    if(state.external_binary_cache.find(key) == state.external_binary_cache.end()) {
        std::vector<std::tuple<std::string, tt_queue_allocation_info>> buffers_to_alloc = {};
        std::tuple<std::vector<std::unordered_set<tt_xy_pair>>, std::vector<tt_hex>, std::vector<uint32_t>> cache_entry;
        bool cached_binaries_found = (state.external_binary_cache.find(alloc_key) != state.external_binary_cache.end());
        state.external_binary_cache.insert({key, cache_entry});

        auto& sync_cores = std::get<0>(state.external_binary_cache.at(key));
        auto& external_binaries = std::get<1>(state.external_binary_cache.at(key));
        auto& num_buffers_in_group =  std::get<2>(state.external_binary_cache.at(key));
        uint target_device = queues.begin()->second.my_queue_info.target_device;
        
        for(const auto& queue : queues) {
            const tt_queue_info &queue_info = queue.second.my_queue_info;
            log_assert(queue_info.loc == QUEUE_LOCATION::DRAM, "Only DRAM IO queue supports dynamic allocation from device!");
            log_assert(target_device == queue_info.target_device, "All batch allocated queues must be on the same device");
            for(const auto& buf : queue_info.alloc_info) {
                buffers_to_alloc.push_back({queue_info.name, buf});
            }
        }
        auto buf_it = buffers_to_alloc.begin();
        uint32_t split_count = 0;
        while(buf_it != buffers_to_alloc.end()) {
            std::vector<std::tuple<std::string, tt_queue_allocation_info>> bufs_to_alloc = {};
            while(buf_it != buffers_to_alloc.end()) {
                if(bufs_to_alloc.size() >= epoch_queue::get_queue_update_blob_num_entries()) {
                    break;
                }
                bufs_to_alloc.push_back(*buf_it);
                buf_it++;
            }
            sync_cores.push_back({});
            get_unique_producer_cores_from_buffers(bufs_to_alloc, sync_cores.back());
            get_unique_consumer_cores_from_buffers(bufs_to_alloc, sync_cores.back());
            if(!split_count) {
                get_unique_consumer_cores_from_queues(queues_to_dealloc, sync_cores.back());
            }
            if(!cached_binaries_found) {
                uint32_t num_buffers = bufs_to_alloc.size();
                tt_queue_allocation_info& first_buffer = std::get<1>(*bufs_to_alloc.begin());
                std::vector<uint32_t> update_binaries = get_buffer_update_read_binaries(bufs_to_alloc, queues, sdesc);
                external_binaries.push_back(tt_hex(update_binaries, tt_hex_type::Misc, nullptr));
                external_binaries.back().set_dram_addr(first_buffer.address);
                external_binaries.back().set_dram_chan(first_buffer.channel);
                external_binaries.back().set_dram_subchannel(0);
                num_buffers_in_group.push_back(num_buffers);
            }
            split_count++;
        }
        if(cached_binaries_found) {
            external_binaries = std::get<1>(state.external_binary_cache.at(alloc_key));
            num_buffers_in_group = std::get<2>(state.external_binary_cache.at(alloc_key));
        }
    }
    sync_cores_per_group = std::get<0>(state.external_binary_cache.at(key));
    external_binaries_per_group = std::get<1>(state.external_binary_cache.at(key));
    num_buffers_per_group =  std::get<2>(state.external_binary_cache.at(key));
}

void tt_epoch_loader::send_allocate_queue_commands(const map<string, tt_queue_wrap> &queues, const unordered_set<string> &queues_to_dealloc, const bool wait_for_eq) {


    uint target_device = queues.begin()->second.my_queue_info.target_device;
    buda_soc_description &sdesc = cluster->get_soc_desc(target_device);
    const std::lock_guard<std::mutex> lock(epoch_ctrl_mutex);  // guard epoch ctrl state change
    auto &device_epoch_ctrl = *epoch_ctrl[target_device];
    auto &dram_allocators = dram_mgr[target_device]->get_dram_allocators();

    vector<uint32_t> header_vec = {0,0,0,0,0,0,0,0};

    for(const auto& queue : queues) {
        device_epoch_ctrl.set_queue_in_use(queue.second.my_queue_info);
        device_epoch_ctrl.get_cached_io_qs(queue.second.my_queue_info) = tt_queue_header_wrap(header_vec).header;
    }
    
    std::string alloc_cache_key = "";
    std::vector<std::unordered_set<tt_xy_pair>> sync_cores_per_group = {};
    std::vector<tt_hex> external_binaries_per_group = {};
    std::vector<uint32_t> num_buffers_per_group = {};
    generate_allocate_queue_binaries(queues, queues_to_dealloc, sdesc, sync_cores_per_group, external_binaries_per_group, num_buffers_per_group, alloc_cache_key);
    for(int buf_group = 0; buf_group < sync_cores_per_group.size(); buf_group++) {
        std::string group_cache_key = alloc_cache_key + std::to_string(buf_group);
        tt_queue_header_wrap header_wrap = {header_vec};
        std::pair<uint, uint> &sync_flag = get_current_sync_flag_loc(target_device);
        tt_xy_pair qstride_sync_loc = sdesc.get_core_for_dram_channel(sync_flag.first, 0);

        std::unordered_set<tt_xy_pair>& sync_cores = sync_cores_per_group.at(buf_group);
        tt_hex& update_hex = external_binaries_per_group.at(buf_group);
        
        // Debug only: perform full chip sync
        bool full_grid_sync = false;
        if (full_grid_sync) {
            sync_cores = unordered_set<tt_xy_pair>(sdesc.workers.begin(), sdesc.workers.end());
        }

        tt_queue_header_mask header_mask = {tt_queue_header_mask::NULL_MASK};
        header_wrap.val[0] = qstride_sync_loc.x;
        header_wrap.val[1] = qstride_sync_loc.y;
        header_wrap.val[2] = sync_flag.second;
        // Need num_buffers here (from cache).
        uint32_t num_buffers = num_buffers_per_group.at(buf_group);
        
        // Send the queue update command and external update list to the device
        uint sync_core_index = 0;
        for (const tt_xy_pair &sync_core: sync_cores) {
            int queue_index = sync_core.y*device_epoch_ctrl.grid_shape[1] + sync_core.x;
            tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
            int epoch_q_slot = (target_queue -> dram_ptrs).get_wr_ptr();
            if (num_buffers > 1) {
                std::tuple<int, int> epoch_q_dram_chan = dram_mgr[target_device]->get_dram_chan(sdesc.arch, sync_core, sdesc.dram_core_channel_map);
                uint32_t dram_channel = std::get<0>(epoch_q_dram_chan);
                uint32_t dram_subchannel = std::get<1>(epoch_q_dram_chan);

                uint worker_idx_within_channel = dram_mgr[target_device]->get_worker_idx_in_channel(sync_core, dram_channel);

                uint64_t binary_size = epoch_queue::get_queue_update_blob_size_bytes();
                uint64_t blob_start_address = dram_allocators[dram_channel].top_of_binaries + worker_idx_within_channel * (uint64_t)epoch_queue::get_epoch_io_queue_update_num_slots() * binary_size;

                int update_hex_slot;
                auto& num_cmds_per_update_blob = device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index];
                auto& io_q_update_ptrs = device_epoch_ctrl.io_q_update_ptrs[queue_index];
                std::vector< tt_epoch_queue*>cmd_q = {target_queue};
                bool pin_binary = state.in_loop_on_device;
                bool cache_hit = device_epoch_ctrl.io_queue_update_caches[queue_index].get_slot_for_binary(group_cache_key, num_cmds_per_update_blob, io_q_update_ptrs, update_hex_slot, cmd_q, cluster, false, pin_binary, true);

                uint32_t hex_dram_addr = blob_start_address + update_hex_slot * binary_size;

                update_hex.set_chip_id(target_device);
                update_hex.set_dram_chan(dram_channel);
                update_hex.set_dram_subchannel(dram_subchannel);
                update_hex.set_dram_addr(hex_dram_addr);
                if(!cache_hit) {
                    // Send queue update blob to cache on device
                    
                    cluster->send_hex_to_dram(&update_hex, true);
                    tt_driver_atomics::sfence();
                    dram_profiler.add_report_for_q_update_blob(&device_epoch_ctrl, &update_hex, "queue-allocate", sync_core, hex_dram_addr);
                }
            }
            const tt_xy_pair dram_core = sdesc.get_core_for_dram_channel(update_hex.get_dram_chan(), update_hex.get_dram_subchannel());
            std::vector<uint32_t> update_command = get_q_update_read_qcmd(dram_core, update_hex.d_addr, num_buffers, sync_core_index, sync_cores.size(), header_wrap, header_mask);

            log_assert(target_queue->is_active(), "Expected queue to be active");
            // Get the epoch queue command index for the current core

            if (wait_for_eq) {
                wait_for_epoch_queue_slot(target_queue, cluster);
            }
            if(num_buffers > 1) {
                if(device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].find(group_cache_key) == device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].end()) {
                    device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].insert({group_cache_key, 0});
                }
                device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].at(group_cache_key)++;
                target_queue -> external_blobs_in_use.at(epoch_q_slot) = group_cache_key;
            }
            // Push queue update(allocate) command to device
            device_epoch_ctrl.push_command_and_update_l1_wptr(target_queue, std::make_shared<tt_hex>(update_command, tt_hex_type::Misc, sync_core, "queue-allocate"));
            tt_driver_atomics::sfence();
            sync_core_index++;
        }
        incr_sync_flag_loc(target_device, sdesc);
    }
}

// Sync Flag is DRAM core and slot id. Used for dynamic queues, full grid syncs.
pair<uint, uint>& tt_epoch_loader::get_current_sync_flag_loc(uint target_device) {
    auto &sync_flag_tracker = state.allocate_queue_sync_flag_tracker;

    if (sync_flag_tracker.find(target_device) == sync_flag_tracker.end()) {
        sync_flag_tracker.insert({target_device, {0, 0}});
    }

    return sync_flag_tracker.at(target_device);
}

// Sync slots will round robin across DRAM channels first, increse slot id second.
void tt_epoch_loader::incr_sync_flag_loc(uint target_device, buda_soc_description &sdesc) {
    auto &sync_flag_tracker = state.allocate_queue_sync_flag_tracker;

    if (sync_flag_tracker.find(target_device) != sync_flag_tracker.end()) {
        auto &sync_flag = sync_flag_tracker.at(target_device);
        if (++sync_flag.first == sdesc.get_num_dram_channels()) {
            sync_flag.first = 0;
            if (++sync_flag.second == epoch_queue::EPOCH_ALLOC_QUEUE_SYNC_SLOTS) {
                sync_flag.second = 0;
            }
        }
    } else {
        log_fatal("Cannot increment sync slot for device: {} not yet used", target_device);
    }
}

// Get epoch alias sync tracker for a given device, used to resolve epoch_id aliasing hazards.
tt_epoch_id_aliasing_sync_tracker& tt_epoch_loader::get_epoch_id_aliasing_sync_tracker(uint target_device) {
    auto &sync_tracker = state.epoch_alias_sync_tracker;

    if (sync_tracker.find(target_device) == sync_tracker.end()) {
        sync_tracker.insert({target_device, tt_epoch_id_aliasing_sync_tracker()});
    }

    return sync_tracker.at(target_device);
}

// If feature on, preload in order up to NUM_SLOTS epoch binaries per device_id, after queues are allocated. Remainder sent as needed on execute cmd.
void tt_epoch_loader::preload_epoch_queues(const map<string, tt_digraph> &graphs, const std::vector<string> &graph_order) {
    log_trace(tt::LogLoader, "\tpreload_epoch_queues() with enable_epoch_preloading: {}", enable_epoch_preloading);
    if (enable_epoch_preloading){

        std::unordered_map<int, int> device_id_epoch_preload_map;

        for (auto &graph_name : graph_order) {
            int graph_target_device = graphs.at(graph_name).my_graph_info.target_device;
            if (!device_id_epoch_preload_map.count(graph_target_device)){
                device_id_epoch_preload_map.insert({graph_target_device, 0});
            }

            if (device_id_epoch_preload_map.at(graph_target_device)++ < epoch_queue::get_epoch_bin_num_slots()){
                
                send_epoch_program(graph_name, true);
            }
        }
    }
}

void tt_epoch_loader::get_unique_consumer_cores_from_buffers(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, std::unordered_set<tt_xy_pair>& all_readers, bool allow_empty_set) {
    for(const auto& buf : buffers) {
        queue_buffer_info buf_info = {.queue_name = std::get<0>(buf), .alloc_info = std::get<1>(buf)};
        bool found_buf = false;
        if(queue_buffer_to_consumers_map.find(buf_info) != queue_buffer_to_consumers_map.end()) {
            found_buf = true;
            const unordered_set<tt_xy_pair>& buf_consumers = queue_buffer_to_consumers_map.at(buf_info);
            all_readers.insert(buf_consumers.begin(), buf_consumers.end());
        }
        log_assert(allow_empty_set || found_buf, "Could not find buffer in queue_to_consumers_map");
    }
}
void tt_epoch_loader::get_unique_consumer_cores_from_queues(const unordered_set<string> &queues, std::unordered_set<tt_xy_pair>& all_readers, bool allow_empty_set) {
    for (const string &queue_name : queues) {
        bool found_consumers = false;
        for (auto &buffer_it: queue_buffer_to_consumers_map) {
            if (buffer_it.first.queue_name == queue_name) {
                const unordered_set<tt_xy_pair> &queue_readers = buffer_it.second;
                all_readers.insert(queue_readers.begin(), queue_readers.end());
                found_consumers = true;
            }
        }
        log_assert(found_consumers || allow_empty_set, "Queue {} is missing from queue_buffer_to_consumers_map!", queue_name);
    }
}

void tt_epoch_loader::get_unique_consumer_cores_from_queue_buffer(
        const string& queue_name,
        const tt_queue_allocation_info& alloc_info,
        uint target_device,
        std::unordered_set<tt_xy_pair>& all_readers,
        bool allow_empty_set) {

    queue_buffer_info buf_info = {.queue_name=queue_name, .alloc_info=alloc_info};

    if (queue_buffer_to_consumers_map.find(buf_info) == queue_buffer_to_consumers_map.end()) {
        log_assert(allow_empty_set, "Queue {} is missing in queue_buffer_to_consumers_map!", queue_name);
    } else {
        const unordered_set<tt_xy_pair> &queue_consumers = queue_buffer_to_consumers_map.at(buf_info);
        all_readers.insert(queue_consumers.begin(), queue_consumers.end());
    }
}

void tt_epoch_loader::get_unique_producer_cores_from_buffers(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set) {
    for(const auto& buf : buffers) {
        bool found_buf = false;
        queue_buffer_info buf_info = {.queue_name = std::get<0>(buf), .alloc_info = std::get<1>(buf)};
        if(queue_buffer_to_producers_map.find(buf_info) != queue_buffer_to_producers_map.end()) {
            found_buf = true;
            const set<tt_xy_pair>& buf_producers = queue_buffer_to_producers_map.at(buf_info);
            all_writers.insert(buf_producers.begin(), buf_producers.end());
        }
        log_assert(allow_empty_set || found_buf, "Could not find buffer in queue_to_producers_map");
    }
}
void tt_epoch_loader::get_unique_producer_cores_from_queue_buffer(const string& queue_name, const tt_queue_allocation_info& alloc_info, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set) {
    queue_buffer_info buf_info = {.queue_name = queue_name, .alloc_info = alloc_info};
    if(queue_buffer_to_producers_map.find(buf_info) == queue_buffer_to_producers_map.end()) {
        log_assert(allow_empty_set, "Queue {} is missing in queue_buffer_to_producers_map!", queue_name);
    } else {
        const set<tt_xy_pair> &queue_producers = queue_buffer_to_producers_map.at(buf_info);
        all_writers.insert(queue_producers.begin(), queue_producers.end());
    }
}

void tt_epoch_loader::get_unique_producer_cores_from_queues(const unordered_set<string> &queues, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set) {
    for (const string &queue_name : queues) {
        bool found_producers = false;
        queue_buffer_info buf_info = {.queue_name=queue_name};
        for(const auto& buffer_it : queue_buffer_to_producers_map) {
            if(buffer_it.first.queue_name == queue_name) {
                const set<tt_xy_pair> &queue_writers = buffer_it.second;
                all_writers.insert(queue_writers.begin(), queue_writers.end());
                found_producers = true;
            }
        }
        log_assert(found_producers || allow_empty_set, "Queue {} is missing in queue_buffer_to_producers_map!", queue_name);
    }
}

int get_max_num_buffers_for_inline(int default_value) {
    // For now this variable will control the threshold for using the multiple-header update
    // If the number of queue buffers is less than or equal to this value:
    //      - For each queue buffer a command will be pushed to every consumer core of the queue
    //      - The queue buffer header address is included in the command
    // If the number of queue buffers is larger than this value:
    //      - A single command will be pushed to all consumer cores of this queue. A blob of queue buffer header addresses will be pushed to dram
    //      - The address in the command will point to the location of this blob
    //      - One of these consumers will update the header for all of the buffers
    int max_num_buffers_for_inline = parse_env("TT_BACKEND_OVERRIDE_NUM_BUFFERS_FOR_Q_UPDATE", default_value);
    log_assert(max_num_buffers_for_inline >= 1,
                "There is a minimum two number of buffers required to enable the q-update blob mode");
    return max_num_buffers_for_inline;
}

tt_queue_header_wrap tt_epoch_loader::get_queue_header_wrap(tt_queue_info queue_info, tt_queue_setting_info queue_setting, const unordered_map<string, int> &vars) {
    tt_queue_header_wrap wrap;

    uint32_t local_rdptr  = queue_info.type == tt::IO_TYPE::Queue ? get_variable_from_map(queue_setting.rd_ptr_local, vars) : 0;
    uint32_t global_wrptr = 0;
    uint32_t global_wrptr_autoinc = 0;
    if ((not get_variable_from_map(queue_setting.read_only, vars)) and (queue_info.type == tt::IO_TYPE::RandomAccess)) {
        global_wrptr = get_variable_from_map(queue_setting.wr_ptr_global, vars);
        global_wrptr_autoinc = get_variable_from_map(queue_setting.global_wrptr_autoinc, vars, "0");
    }
    uint32_t global_rdptr =  get_variable_from_map(queue_setting.rd_ptr_global, vars);    
    uint32_t local_rdptr_autoinc = get_variable_from_map(queue_setting.rd_ptr_autoinc, vars, queue_info.type == tt::IO_TYPE::Queue? "1" : "0");
    uint32_t global_rdptr_autoinc = get_variable_from_map(queue_setting.global_rdptr_autoinc, vars, "0");
    bool zero = get_variable_from_map(queue_setting.zero, vars);
    bool prolog = get_variable_from_map(queue_setting.prolog, vars);
    log_assert(!zero or prolog, "Zero flag can only be set when prologue is true");

    tt_queue_header header;
    wrap.header.local_rd_ptr         = local_rdptr;
    wrap.header.global_rd_ptr        = global_rdptr;
    wrap.header.global_wr_ptr        = global_wrptr;
    wrap.header.zero                 = zero;
    wrap.header.local_rdptr_autoinc  = local_rdptr_autoinc;
    wrap.header.global_rdptr_autoinc = global_rdptr_autoinc;
    wrap.header.global_wrptr_autoinc = global_wrptr_autoinc;
    return wrap;
}

void tt_epoch_loader::populate_queue_to_core_map_from_net2pipe(const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams, string build_dir_path, int num_temporal_epochs, bool queue_to_producer) {
    std::string file_name = (queue_to_producer ? "queue_to_producer.yaml" : "queue_to_consumer.yaml");
    std::string cores_tag = queue_to_producer ? "producers" : "consumers";
    for (int temporal_epoch = 0; temporal_epoch < num_temporal_epochs; temporal_epoch++) {
        string overlay_path = tt::get_overlay_output_dir(build_dir_path, temporal_epoch);
        string queue_to_core_file_path = overlay_path + file_name;
        log_assert(fs::exists(queue_to_core_file_path),  "{} does not exist for temporal epoch {} ", file_name, std::to_string(temporal_epoch));
        YAML::Node queue_to_core_yaml = YAML::LoadFile(queue_to_core_file_path);

        auto default_sdesc = load_soc_descriptor_from_yaml(this->output_dir + "/device_desc.yaml");
        for (YAML::const_iterator q_it = queue_to_core_yaml.begin(); q_it != queue_to_core_yaml.end(); ++q_it) {
            string queue_name = q_it->first.as<string>();
            // For dual-view rams, add producers or consumers to base/alias name as well (all views), for simpler lookup code at runtime.
            bool is_dual_view_ram = dual_view_rams.find(queue_name) != dual_view_rams.end();
            std::unordered_set<std::string> queue_views = {queue_name};
            if (is_dual_view_ram) {
                queue_views.insert(dual_view_rams.at(queue_name).alt_views.begin(),dual_view_rams.at(queue_name).alt_views.end());
            }

            for (YAML::const_iterator buf_it = q_it->second.begin(); buf_it != q_it->second.end(); ++buf_it) {
                uint buf_idx = buf_it->first.as<uint>();
                tt_queue_allocation_info queue_alloc;
                uint32_t target_device = 0;
                set<tt_xy_pair> buf_cores;
                bool channel_key_found = false;
                bool buf_addr_key_found = false;
                bool core_key_found = false;
                bool target_device_found = false;
                for (YAML::const_iterator buf_info = buf_it->second.begin(); buf_info != buf_it->second.end(); ++buf_info) {
                    if (buf_info->first.as<string>() == "channel") {
                        channel_key_found = true;
                        queue_alloc.channel = buf_info->second.as<uint32_t>();
                    }
                    if (buf_info->first.as<string>() == "addr") {
                        buf_addr_key_found = true;
                        queue_alloc.address = buf_info->second.as<uint32_t>();
                    }
                    if (buf_info->first.as<string>() == "queue_target_device") {
                        target_device_found = true;
                        target_device = buf_info->second.as<uint32_t>();
                    }
                    if (buf_info ->first.as<string>() == cores_tag) {
                        core_key_found = true;
                        for (YAML::const_iterator core_it = buf_info->second.begin(); core_it != buf_info->second.end(); ++core_it) {
                            uint core_idx = core_it->first.as<uint>();
                            int chip_id = -1;
                            int x = -1;
                            int y = -1;

                            for (YAML::const_iterator core_info = core_it->second.begin(); core_info != core_it->second.end(); ++core_info) {
                                if (core_info->first.as<string>() == "chip_id") {
                                    chip_id = core_info->second.as<int>();
                                }
                                if (core_info->first.as<string>() == "x") {
                                    x = core_info->second.as<int>();
                                }
                                if (core_info->first.as<string>() == "y") {
                                    y = core_info->second.as<int>();
                                }
                            }
                            log_assert(target_device_found, "{} parser error Inside queue {} buffer_index {}, target_device keyword was not found", file_name, queue_name, buf_idx);
                            log_assert(chip_id != -1, "{} parser error: Inside queue {} buffer_index {}, core index {} chip_id keyword was not found", file_name, queue_name, buf_idx, core_idx);
                            log_assert(x != -1, "{} parser error: Inside queue {} buffer_index {}, core index {} x keyword was not found", file_name, queue_name, buf_idx, core_idx);
                            log_assert(y != -1, "{} parser error: Inside queue {} buffer_index {}, core index {} y keyword was not found", file_name, queue_name, buf_idx, core_idx);

                            // Cross chip sync is not supported. This was always the assumption in on-device sync feature where sync cmds are inserted on the same chip
                            // as queue, but make it explicit here, for being able to throw a warning if other cases arise.
                            if (chip_id == target_device) {

                                tt_xy_pair current_core(x, y);
                                if(std::find(default_sdesc->workers.begin(), default_sdesc->workers.end(), current_core) != default_sdesc->workers.end()) {
                                    current_core = default_sdesc->get_worker_core(current_core);  // translate back to logical core using default soc desc
                                    current_core = cluster->get_soc_desc(chip_id).get_routing_core(current_core);  // retranslate to routing core using harvested soc desc
                                }
                                else {
                                    log_assert(std::find(default_sdesc->ethernet_cores.begin(), default_sdesc->ethernet_cores.end(), current_core) != default_sdesc->ethernet_cores.end(),
                                                    "queue_buffer_to_producers_map can only contain worker or ethernet cores. Core {}", current_core.str());
                                }
                                buf_cores.insert(current_core);

                            } else {
                                // Only cross-chip case supported is Producer for GS. All others are unexpected.
                                if(!(queue_to_producer && cluster->get_soc_desc(chip_id).arch == tt::ARCH::GRAYSKULL)) {
                                    log_warning(tt::LogLoader, "Remote {} for queue_name: {} are not currently supported/expected.", cores_tag, queue_name);
                                }
                            }
                        }
                        
                    }
                }
                log_assert(channel_key_found, "{} parser error: Inside queue {} buffer_index {}, channel keyword was not found", file_name, queue_name, buf_idx);
                log_assert(buf_addr_key_found, "{} parser error: Inside queue {} buffer_index {}, addr keyword was not found", file_name, queue_name, buf_idx);
                log_assert(core_key_found, "{} parser error: Inside queue {} buffer_index {}, producer/consumer keyword was not found", file_name, queue_name, buf_idx);

                for (auto &queue_view_name : queue_views) {
                    queue_buffer_info buf_info = {.queue_name=queue_view_name, .alloc_info=queue_alloc};
                    // log_info(tt::LogLoader, "Adding {} cores {} for queue: {} view_name: {} queue_to_producer: {}", buf_cores.size(), buf_cores, queue_name, queue_view_name, queue_to_producer);
                    if(queue_to_producer) {
                        if (queue_buffer_to_producers_map.find(buf_info) == queue_buffer_to_producers_map.end()) {
                            queue_buffer_to_producers_map.insert({buf_info, {}});
                        }
                        queue_buffer_to_producers_map.at(buf_info).insert(buf_cores.begin(), buf_cores.end());
                    } else {
                        if (queue_buffer_to_consumers_map.find(buf_info) == queue_buffer_to_consumers_map.end()) {
                            queue_buffer_to_consumers_map.insert({buf_info, {}});
                        }
                        queue_buffer_to_consumers_map.at(buf_info).insert(buf_cores.begin(), buf_cores.end());
                    }
                }
            }
        }
    }
}

set<string> get_all_dram_queues_for_graph(
        const netlist_workload_data &workload, string graph_name, bool all_input_queues, bool all_output_queues) {
    
    set<string> queue_names;

    log_assert(workload.graphs.find(graph_name) != workload.graphs.end(), "graph name must exist in workload");
    const tt_digraph &graph = workload.graphs.at(graph_name);

    const map<string, tt_op_info> &op_map = graph.my_graph_info.op_map;

    for (const std::pair<const string, tt_op_info>& op_it : op_map) {
        const tt_op_info &op_info = op_it.second;
        if (all_input_queues) {
            for (const string &input_name: op_info.input_names) {
                bool is_queue_input = workload.queues.find(input_name) != workload.queues.end();
                if (is_queue_input) {
                    queue_names.insert(input_name);
                }
            }
        }
        if (all_output_queues) {
            for (const string &output_name: workload.op_to_outputs.at(op_info.name)) {
                bool is_queue_output = workload.queues.find(output_name) != workload.queues.end();
                if (is_queue_output) {
                    bool is_dram_queue = workload.queues.at(output_name).my_queue_info.loc == QUEUE_LOCATION::DRAM;
                    if (is_dram_queue) {
                        queue_names.insert(output_name);
                    }
                }
            }
        }
    }
    return queue_names;
}

set<string> get_all_queue_names_to_decouple_for_graph(const netlist_workload_data &workload, string graph_name, perf::PerfDramDecouple dram_decouple) {
    set<string> all_queue_names;
    log_assert(!dram_decouple.queue_name.empty(), "queue name must have been populated");
    if (dram_decouple.all_inputs) {

        // Get all input dram queues for this graph
        set<string> queue_names = get_all_dram_queues_for_graph(workload, graph_name, true, false);
        all_queue_names.insert(queue_names.begin(), queue_names.end());
    }
    if (dram_decouple.all_outputs) {

        // Get all output dram queues for this graph
        set<string> queue_names = get_all_dram_queues_for_graph(workload, graph_name, false, true);
        all_queue_names.insert(queue_names.begin(), queue_names.end());
    }
    if (!dram_decouple.all_inputs && !dram_decouple.all_outputs) {

        log_assert(workload.queues.find(dram_decouple.queue_name) != workload.queues.end(), 
            "Invalid dram decoupling config. Queue name {} does not exist in workload", dram_decouple.queue_name);
        
        log_assert(workload.queues.at(dram_decouple.queue_name).my_queue_info.loc == QUEUE_LOCATION::DRAM, "Only queues on dram can be decoupled");
        all_queue_names.insert(dram_decouple.queue_name);
    }
    return all_queue_names;
}

void tt_epoch_loader::populate_dram_decouple_config_map(const netlist_workload_data &workload) {

    if (!cluster) {
        return;
    }

    for (perf::PerfDramDecouple dram_decouple: cluster->perf_state.get_perf_desc().dram_decouple_config) {
        if (dram_decouple.graph_name == "*") {
            for (auto graph_name_to_digraph: workload.graphs) {
                string graph_name = graph_name_to_digraph.first;
                if (graph_name_to_queue_decouplings.find(graph_name) == graph_name_to_queue_decouplings.end()) {
                    graph_name_to_queue_decouplings.insert({graph_name, {}});
                }
                set<string> queue_names = get_all_queue_names_to_decouple_for_graph(workload, graph_name, dram_decouple);
                graph_name_to_queue_decouplings.at(graph_name).insert(queue_names.begin(), queue_names.end());
            }
        } else {
            log_assert(workload.graphs.find(dram_decouple.graph_name) != workload.graphs.end(),
                "Invalid dram decoupling config. Graph name {} does not exist in workload", dram_decouple.graph_name);
            if (graph_name_to_queue_decouplings.find(dram_decouple.graph_name) == graph_name_to_queue_decouplings.end()) {
                graph_name_to_queue_decouplings.insert({dram_decouple.graph_name, {}});
            }
            set<string> queue_names = get_all_queue_names_to_decouple_for_graph(workload, dram_decouple.graph_name, dram_decouple);
            graph_name_to_queue_decouplings.at(dram_decouple.graph_name).insert(queue_names.begin(), queue_names.end());
        }
    }
    log_debug(tt::LogPerfInfra, "graph_name_to_queue_decouplings: ");
    for (auto graph_decoupling_it: graph_name_to_queue_decouplings) {
        log_debug(tt::LogPerfInfra, "Graph_name {}", graph_decoupling_it.first);
        for (auto queue: graph_decoupling_it.second) {
            log_debug(tt::LogPerfInfra, "- Queue_name {}", queue);
        }
    }

}

void tt_epoch_loader::write_dram_decouplings_to_queue_headers(const netlist_workload_data &workload, string graph_name, bool reset) {
    
    if (graph_name_to_queue_decouplings.find(graph_name) == graph_name_to_queue_decouplings.end()) {
        return;
    }
    const set<string> &queue_names_to_decouple = graph_name_to_queue_decouplings.at(graph_name);
    for (const string &queue_name: queue_names_to_decouple) {
        log_assert(workload.queues.find(queue_name) != workload.queues.end(), "queue name must exist in workload");
        const tt_queue_info &queue_info = workload.queues.at(queue_name).my_queue_info;

        log_assert(queue_info.loc == QUEUE_LOCATION::DRAM, "Dram decouplings feature is only supported for queues on dram");

        if (queue_info.loc == QUEUE_LOCATION::DRAM) {
            for (auto &alloc: queue_info.alloc_info) {
                vector<uint32_t> current_header;
                tt_target_dram dram = {queue_info.target_device, alloc.channel, 0/*subchan*/};
                cluster->read_dram_vec(current_header, dram, alloc.address, 32);
                log_assert(current_header.size() == 8, "We should read 8, 4B values as the header");
                if (reset) {
                    log_debug(tt::LogPerfInfra, "Reseting queue header for {}, device {}, channel {}, address {} with decoupling disabled", queue_name, queue_info.target_device, alloc.channel, alloc.address);
                    current_header.at(3) &= 0xfffdffff;
                } else {
                    log_debug(tt::LogPerfInfra, "Updating queue header for {}, device {}, channel {}, address {} with decoupling enabled", queue_name, queue_info.target_device, alloc.channel, alloc.address);
                    current_header.at(3) |= 0x00020000;
                }
                cluster->write_dram_vec(current_header, dram, alloc.address);
                stringstream ss;
                print_queue_header(ss, current_header);
                log_debug(tt::LogPerfInfra, "Updated queue header: {}", ss.str());
            }
        }
        
    }

}

void tt_epoch_loader::write_io_queue_header(vector<uint32_t> &vec, uint32_t offset, const tt_queue_info &queue_info) {
    switch (queue_info.loc) {
        case QUEUE_LOCATION::DRAM:
            for (const auto &alloc : queue_info.alloc_info) {
                tt_target_dram dram = {queue_info.target_device, alloc.channel, 0 /*subchan*/};
                cluster->write_dram_vec(vec, dram, alloc.address + offset, true);
            }
            break;
        case QUEUE_LOCATION::HOST:
            for (const auto &alloc : queue_info.alloc_info) {
                chip_id_t src_device_id = queue_info.src_device_id;
                cluster->write_sysmem_vec(vec, alloc.address + offset, alloc.channel, src_device_id);
            }
            break;
        default: log_fatal("Unsupported queue location"); break;
    }
}

void tt_epoch_loader::update_io_queue_settings(const map<string, tt_queue_wrap> &queues,
    const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams,
    const vector<tt_queue_setting_info> &queue_settings, const unordered_map<string, int> &vars, const tt_queue_header_mask &header_mask)
{
    perf::record_scope record(perf::HostEventType::QUEUE_UPDATE_COMMAND);
    bool update_settings = true; // default: always push each setting to device
    const std::lock_guard<std::mutex> lock(epoch_ctrl_mutex);

    for (auto &queue_setting : queue_settings) {
        const std::string queue_name = queue_setting.name;
        const tt_queue_info &queue_info = queues.at(queue_name).my_queue_info;

        // Dual View Rams must sync on both producer/consumers to avoid race conditions.
        bool is_dual_view_ram = dual_view_rams.find(queue_name) != dual_view_rams.end();

        log_assert(chan_struct_map.count(queue_info.target_device) > 0, "chip_id not in chan_struct_map");
        auto &ctrl = get_epoch_ctrl(queue_info.target_device);
        auto &dram_allocators = dram_mgr[queue_info.target_device]->get_dram_allocators();

        // Wrptr update only applies to RAM memory. Clear rdptr mask if write-only Dual View RAM.
        tt_queue_header_mask update_mask = header_mask;
        if (queue_info.type == tt::IO_TYPE::RandomAccess and (not get_variable_from_map(queue_setting.read_only, vars))) {
            update_mask.set_global_wr_ptr();
            if (is_dual_view_ram) {
                update_mask.clear_global_rd_ptr();
            }
        }
        tt_queue_header_wrap header_wrap = get_queue_header_wrap(queue_info, queue_setting, vars);
        bool update_from_device = false;
        if (enable_queue_settings_reuse) {
            update_settings = !header_wrap.matches(ctrl.get_cached_io_qs(queue_info), update_mask);
        }
        if (enable_hw_io_queue_update) {
            update_from_device = ctrl.is_queue_in_use(queue_info);
        }
        log_trace(
            tt::LogLoader,
            "{} queue={}, updates={}, gr={}/gw={}/lr={}/ls={}, mode={}, {}",
            __FUNCTION__,
            queue_name,
            update_settings,
            update_mask.has_global_rd_ptr(),
            update_mask.has_global_wr_ptr(),
            update_mask.has_local_rd_ptr(),
            update_mask.has_local_settings(),
            update_from_device ? "device" : "host",
            header_wrap.header);

        if (update_settings) {
            ctrl.set_cached_io_qs_wrap(queue_info, header_wrap, update_mask);
            if (update_from_device) {
                update_io_queue_setting_from_device(queue_info, is_dual_view_ram, header_wrap, update_mask, queues);
            } else {
                update_io_queue_setting_from_host(queue_info, header_wrap, update_mask);
            }
        } else {
            log_trace(tt::LogLoader, "{} skipping {} queue settings update due to reuse", __FUNCTION__, queue_name);
        }
    }
}

void tt_epoch_loader::update_io_queue_setting_from_device(const tt_queue_info &queue_info, bool is_dual_view_ram, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask, const map<string, tt_queue_wrap> &queues) {
    const std::string queue_name = queue_info.name;
    buda_soc_description &sdesc = cluster->get_soc_desc(queue_info.target_device);
    auto &dram_allocators = dram_mgr[queue_info.target_device]->get_dram_allocators();
    auto &device_epoch_ctrl = *epoch_ctrl[queue_info.target_device];
    // Max threshold for inlining queue update cmds
    const static int max_num_buffers_for_inline = get_max_num_buffers_for_inline(2);

    uint num_buffers = queue_info.alloc_info.size();
    auto sync_type   = header_mask.get_sync_type();
    if (num_buffers <= max_num_buffers_for_inline) {
        // Loop over every buffer in the queue
        int buffer_index = 0;
        for (auto &alloc : queue_info.alloc_info) {
            const auto buffer_addr =
                (uint64_t)alloc.address + (queue_info.loc == QUEUE_LOCATION::DRAM ? 0 : sdesc.get_noc2host_offset(alloc.channel));
            const auto buffer_core = queue_info.loc == QUEUE_LOCATION::DRAM
                                         ? sdesc.get_core_for_dram_channel(alloc.channel, 0)
                                         : sdesc.get_pcie_core();

            // For dual-view RAMS, will sync on producers+consumers (pre-populated for both views), share single cache entry via base name.
            const auto buffer_name =
                get_base_queue_name(queue_info) + "_sync_type_" + std::to_string((int)sync_type) + "_buffer_" + std::to_string(buffer_index);

            if (state.sync_cores_cache.find(buffer_name) == state.sync_cores_cache.end()) {
                if ((sync_type == tt_queue_settings_sync_type::SyncOnProducers) || (sync_type == tt_queue_settings_sync_type::SyncOnBoth) || is_dual_view_ram) {
                    get_unique_producer_cores_from_queue_buffer(queue_name, alloc, state.sync_cores_cache[buffer_name], true);
                }
                if ((sync_type == tt_queue_settings_sync_type::SyncOnConsumers) || (sync_type == tt_queue_settings_sync_type::SyncOnBoth) || is_dual_view_ram) {
                    get_unique_consumer_cores_from_queue_buffer(queue_name, alloc, queue_info.target_device, state.sync_cores_cache[buffer_name], true);
                }
            }

            std::unordered_set<tt_xy_pair> &unique_readers = state.sync_cores_cache[buffer_name];
            uint reader_index = 0;            

            // Push one command to each core for each buffer in the queue.
            for (const tt_xy_pair &reader: unique_readers) {
                // The dram core and address in this command should represent the location of the queue buffer
                std::vector<uint32_t> update_command = get_q_update_read_qcmd(
                    buffer_core, buffer_addr, 1, reader_index, unique_readers.size(), header_wrap, header_mask);

                // Push the previous command to epoch queue
                // Get the epoch queue command index for the worker consumer core
                int queue_index = reader.y*device_epoch_ctrl.grid_shape[1] + reader.x;
                tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
                log_assert(target_queue->is_active(), "Expected queue to be active");
                wait_for_epoch_queue_slot(target_queue, cluster);
                device_epoch_ctrl.push_command_and_update_l1_wptr(target_queue, std::make_shared<tt_hex>(update_command, tt_hex_type::Misc, reader, "queue-update-inline " + queue_name));

                reader_index++;
            }
            buffer_index++;
        }
    } else {
        std::vector<tt_hex> external_binaries_per_group = {};
        std::vector<std::unordered_set<tt_xy_pair>> sync_cores_per_group = {};
        std::vector<uint32_t> num_buffers_per_group = {};
        std::string cache_key = "";

        generate_queue_update_external_binaries({queue_name}, cache_key, queues, sync_type, sdesc, sync_cores_per_group, external_binaries_per_group, num_buffers_per_group, false);
        uint64_t binary_size = epoch_queue::get_queue_update_blob_size_bytes();
        for(int buf_group = 0; buf_group < sync_cores_per_group.size(); buf_group++) {
            std::string group_cache_key = cache_key + std::to_string(buf_group);
            tt_hex& update_hex = external_binaries_per_group.at(buf_group);
            std::unordered_set<tt_xy_pair>& sync_cores = sync_cores_per_group.at(buf_group);
            uint32_t num_buffers = num_buffers_per_group.at(buf_group);
            uint core_index = 0;
            for(const auto &core : sync_cores) {
                // Get the current core where the blob must be pushed into
                auto [dram_channel, dram_subchannel] =
                    dram_mgr[queue_info.target_device]->get_dram_chan((dram_mgr[queue_info.target_device] -> sdesc).arch, core, (dram_mgr[queue_info.target_device] -> sdesc).dram_core_channel_map);
                uint64_t binary_size = epoch_queue::get_queue_update_blob_size_bytes();
                int queue_index = core.y*device_epoch_ctrl.grid_shape[1] + core.x;
            
                uint worker_idx_within_channel = dram_mgr[queue_info.target_device]->get_worker_idx_in_channel(core, dram_channel);

                const tt_xy_pair dram_core = sdesc.get_core_for_dram_channel(dram_channel, dram_subchannel);
                // The dram core and the address for this epoch command must point to the blob that contains the queue header address
                
                // Push the previous command to epoch queue
                // Get the epoch queue command index for the worker consumer core
                tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
                int epoch_q_slot = (target_queue -> dram_ptrs).get_wr_ptr();
                uint64_t start_addr_of_external_blob = dram_allocators[dram_channel].top_of_binaries + worker_idx_within_channel * (uint64_t)epoch_queue::get_epoch_io_queue_update_num_slots() * binary_size;

                int external_blob_slot;
                auto& num_cmds_per_update_blob = device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index];
                auto& io_q_update_ptrs = device_epoch_ctrl.io_q_update_ptrs[queue_index];
                std::vector< tt_epoch_queue*>cmd_q = {target_queue};
                bool pin_binary = state.in_loop_on_device;
                bool cache_hit = device_epoch_ctrl.io_queue_update_caches[queue_index].get_slot_for_binary(group_cache_key, num_cmds_per_update_blob, io_q_update_ptrs, external_blob_slot, cmd_q, cluster, false, pin_binary, true);

                uint32_t external_blob_dram_addr = start_addr_of_external_blob + external_blob_slot * binary_size;

                if(!cache_hit) {
                    update_hex.set_dram_chan(dram_channel);
                    update_hex.set_dram_subchannel(dram_subchannel);
                    update_hex.set_dram_addr(external_blob_dram_addr);
                    update_hex.set_chip_id(queue_info.target_device);
                    
                    cluster->send_hex_to_dram(&update_hex, true);
                    tt_driver_atomics::sfence();
                    dram_profiler.add_report_for_q_update_blob(&device_epoch_ctrl, &update_hex, group_cache_key, core, external_blob_dram_addr);
                }
                
                std::vector<uint32_t> update_command = get_q_update_read_qcmd(
                    dram_core, external_blob_dram_addr, num_buffers, core_index, sync_cores.size(), header_wrap, header_mask);

                log_assert(target_queue->is_active(), "Expected queue to be active");

                wait_for_epoch_queue_slot(target_queue, cluster);

                if(device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].find(group_cache_key) == device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].end()) {
                    device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].insert({group_cache_key, 0});
                }
                device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index].at(group_cache_key)++;
                target_queue -> external_blobs_in_use.at(epoch_q_slot) = group_cache_key;
                device_epoch_ctrl.push_command_and_update_l1_wptr(target_queue, std::make_shared<tt_hex>(update_command, tt_hex_type::Misc, core, "queue-update-external " + group_cache_key));
                tt_driver_atomics::sfence();
                core_index++;
            }            
        }
    }
}

void tt_epoch_loader::update_io_queue_setting_from_host(const tt_queue_info &queue_info, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask) {
    std::pair<vector<uint32_t>, int> vec_with_offset;
    if (header_mask.has_full()) {
        vec_with_offset = {header_wrap.get_vec(), 0};
        write_io_queue_header(vec_with_offset.first, vec_with_offset.second, queue_info);
    } else {
        if (header_mask.has_global_rd_ptr()) {
            vec_with_offset = header_wrap.get_rdptr_vec();
            write_io_queue_header(vec_with_offset.first, vec_with_offset.second, queue_info);
        }
        if (header_mask.has_global_wr_ptr()) {
            vec_with_offset = header_wrap.get_wrptr_vec();
            write_io_queue_header(vec_with_offset.first, vec_with_offset.second, queue_info);
        }
        if (header_mask.has_local_settings()) {
            vec_with_offset = header_wrap.get_settings_vec();
            write_io_queue_header(vec_with_offset.first, vec_with_offset.second, queue_info);
        } else if (header_mask.has_local_rd_ptr()) {
            vec_with_offset = header_wrap.get_lrdptr_vec();
            write_io_queue_header(vec_with_offset.first, vec_with_offset.second, queue_info);
        }
    }
}

void tt_epoch_loader::mark_io_queues_in_use(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings) {
    const std::lock_guard<std::mutex> lock(epoch_ctrl_mutex);  // guard epoch ctrl state change
    for (auto &queue_setting : queue_settings) {
        const tt_queue_info &queue_info = queues.at(queue_setting.name).my_queue_info;
        epoch_ctrl[queue_info.target_device]->set_queue_in_use(queue_info);
    }
}

bool tt_epoch_loader::check_resource_overlap(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings, const std::unordered_map<string, int> &vars) {
    if (enable_optimized_barriers) {
        const std::lock_guard<std::mutex> lock(epoch_ctrl_mutex);
        for (auto &queue_setting : queue_settings) {
            std::string queue_name = queue_setting.name;
            const tt_queue_info &queue_info = queues.at(queue_name).my_queue_info;
            // Issue #232: This is problem if we want to support input_queue device_id != graph device_id
            if (epoch_ctrl.at(queue_info.target_device)->is_queue_in_use(queue_info)) {
                if (vars.empty()) {
                    log_debug(tt::LogLoader, "{} is currently in use", queue_name);
                    return true;
                } else {
                    tt_queue_header_wrap header_wrap = get_queue_header_wrap(queue_info, queue_setting, vars);
                    const auto &qs_cache_header = epoch_ctrl.at(queue_info.target_device)->get_cached_io_qs(queue_info);
                    if (!enable_queue_settings_reuse or qs_cache_header != header_wrap.header) {
                        log_debug(tt::LogLoader, "{} is currently in use", queue_name);
                        return true;
                    } else {
                        log_debug(tt::LogLoader, "Skipping {} overlap check since current on device values can be reused {}", queue_name, qs_cache_header);
                    }
                }
            }
        }
        return false;
    }
    // always conservatively assume resource overlap
    return true;
}

void tt_epoch_loader::insert_epoch_program(tt_epoch_program_info &&epoch_info) {
    // Ensure that graphs are globally unique and not recompiled unnecessarily
    log_assert(graph_to_epoch_map.find(epoch_info.name) == graph_to_epoch_map.end(),
        "Epoch program already exists for graph = {}", epoch_info.name);
    (epoch_ctrl.at(epoch_info.target_device) ->  num_cmds_per_binary).insert({epoch_info.name , 0});
    graph_to_epoch_map.insert({epoch_info.name, std::move(epoch_info)});
}

void tt_epoch_loader::send_epoch_program(std::string name, bool epoch_binary_preload) {
    tt_epoch_program_info &epoch_info = get_epoch_program_info(name);
    tt_epoch_control &ctrl = *epoch_ctrl[epoch_info.target_device];

    // Check if epoch can be reused from device cache, if not lay it out in DRAM and update addrs
    bool epoch_cache_hit = lay_out_binaries(epoch_info, epoch_binary_preload);
    log_debug(tt::LogLoader, "\t(caching)\t epoch binary will be {} (epoch_binary_preload: {}) for graph = {}, device = {}", epoch_cache_hit ? "reused" : "sent", epoch_binary_preload, name, epoch_info.target_device);
    
    // Execution order: 
    // 1) If binary not in cache: send binary. This may overwrite an existing binary. The binary being overwritten is guaranteed to not be in use, as it is the least recently used 
    // and does not have have any active valid commands (we wait for active commands for this binary to become stale). 
    // 2) Wait for cmd queues to be ready
    // 3) Send commands
    
    if (!epoch_cache_hit) {
        send_epoch_binaries(epoch_info);  // copy pre-assembled payload to dram epoch_q slot
    }

    if (!epoch_binary_preload){
        wait_for_epoch_queues_ready(ctrl, epoch_info);    // wait for free slots available on device epoch_q
        send_epoch_commands(epoch_info);      // push valid epoch commands to dram epoch_q slot
    }else{
        log_assert(!epoch_cache_hit, "Expected epoch cache misses only for epoch_binary_preload=1");
    }
    
}

tt_epoch_program_info& tt_epoch_loader::get_epoch_program_info(std::string name) {
    if (graph_to_epoch_map.find(name) == graph_to_epoch_map.end()) {
        throw std::runtime_error("Epoch program not found for graph = " + name);
    }
    return graph_to_epoch_map.at(name);
}

bool tt_epoch_loader::lay_out_binaries(const tt_epoch_program_info &info, bool epoch_binary_preload) {
    perf::record_scope record(perf::HostEventType::LAYOUT_BINARIES);
    shared_ptr<tt_epoch_binary> bin = info.binary;
    tt_epoch_control* ctrl = epoch_ctrl.at(info.target_device);
    buda_soc_description &sdesc = cluster->get_soc_desc(info.target_device);
    vector<int> dram_chan_map = sdesc.get_dram_chan_map();
    log_assert(chan_struct_map.count(info.target_device)>0, "chip_id not in chan_struct_map");
    auto &dram_allocators = dram_mgr[info.target_device]->get_dram_allocators();
    bool epoch_cache_hit = false;
    int epoch_q_slot;

    if (enable_epoch_caching) {
        bool pin_binary = state.in_loop_on_device;
        epoch_cache_hit = ctrl -> cache.get_slot_for_binary(info.name, ctrl -> num_cmds_per_binary,ctrl -> bin_q_ptrs, epoch_q_slot, ctrl -> hexqs, cluster, epoch_binary_preload, pin_binary);
        log_debug(tt::LogLoader, "\t(caching)\t epoch binary cache {}, epoch_q_slot = {}", (epoch_binary_preload ? "PRELOAD" : epoch_cache_hit ? "HIT" : "MISS"), epoch_q_slot);
    } else {
        // Get epoch q current write slot and increment
        epoch_q_slot = ctrl->bin_q_ptrs.incr_get_wr();
    }

    if (!epoch_cache_hit) {
        bin -> tensix_hex_vec.clear(); // destroy hex objects in memory if there are any (deallocate shared pointers)
        bin -> eth_hex_vec.clear();
        // Set start address for every channel
        for (int dram_channel = 0; dram_channel < dram_chan_map.size(); dram_channel++) {
            dram_allocators[dram_channel].set_binary_address_to_q_slot_start(epoch_q_slot);
        }
        for (int i=0; i<bin->number_of_tensix_hex_images(); i++) {
            // Map to dram channels based on core location
            const auto &routing_core = bin -> get_routing_coord(i);
            auto [dram_channel, dram_subchannel] =
                dram_mgr[info.target_device]->get_dram_chan((dram_mgr[info.target_device]->sdesc).arch, routing_core, (dram_mgr[info.target_device]->sdesc).dram_core_channel_map);

            // Allocate core specific fw binary block using channel allocator
            log_assert(dram_channel < dram_allocators.size(), "DRAM channel {} does not exist", dram_channel);
            uint64_t binary_size = dram_mem::address_map::FW_DRAM_BLOCK_SIZE();
            uint64_t dram_start_addr = dram_allocators[dram_channel].alloc_bin(binary_size);

            dram_profiler.add_report_for_binary_alloc_entry(ctrl, info.target_device, dram_channel, dram_subchannel, dram_start_addr, binary_size, routing_core, info.name);

            // Assign dram channel and address to all binaries on current core
            // log_trace(tt::LogLoader, "\t{} allocated T6 logical core (x={}, y={}) binaries to dram channel: {} @{}", __FUNCTION__, core_c, core_r, dram_channel, dram_start_addr);
            bin->assign_tensix_binaries_to_dram(i, dram_channel, dram_subchannel, dram_start_addr);

            const auto dram_xy = sdesc.get_core_for_dram_channel((bin -> trisc0_bin_vec[i]).d_chan,  (bin -> trisc0_bin_vec[i]).d_subchannel);
            const auto dram_addr = (bin -> trisc0_bin_vec[i]).d_addr;
            bin -> tensix_hex_vec.push_back(std::make_shared<tt_hex>(tt_epoch_control::get_valid_qcmd(dram_xy, dram_addr, info.device_perf_trace_en, info.get_overlay_decouple_mask(routing_core)), tt_hex_type::Misc, bin -> get_routing_coord(i), info.name));
        }
        for (int i=0; i<bin->number_of_ethernet_hex_images(); i++) {
            const auto &core_xy = bin->ethernet_blob_bin_vec.at(i).associated_routing_core;

            auto [dram_channel, dram_subchannel] =
                dram_mgr[info.target_device]->get_dram_chan((dram_mgr[info.target_device]->sdesc).arch, core_xy, (dram_mgr[info.target_device]->sdesc).dram_core_channel_map);

            log_debug(tt::LogLoader, "ethernet core (y={},x={}) overlay binaries mapped to (channel={}, subchannel={})", core_xy.y, core_xy.x, dram_channel, dram_subchannel);
            uint64_t binary_size = eth_l1_mem::address_map::FW_DRAM_BLOCK_SIZE;
            uint64_t dram_start_addr = dram_allocators[dram_channel].alloc_bin(binary_size);
            bin->assign_ethernet_binaries_to_dram(i, dram_channel, dram_subchannel, dram_start_addr);
            const auto dram_xy = sdesc.get_core_for_dram_channel(bin->ethernet_blob_bin_vec[i].d_chan,  bin->ethernet_blob_bin_vec[i].d_subchannel);
            const auto dram_addr = (bin -> ethernet_blob_bin_vec[i]).d_addr;
            bin -> eth_hex_vec.push_back(std::make_shared<tt_hex>(tt_epoch_control::get_valid_qcmd(dram_xy, dram_addr, info.device_perf_trace_en, info.get_overlay_decouple_mask(core_xy)), tt_hex_type::Misc, bin -> ethernet_blob_bin_vec[i].associated_routing_core, info.name));
        }
        
        if (epoch_binary_preload){
            event_counters["epoch_cache_preload"] += 1;
        }else{
            event_counters["epoch_cache_miss"] += 1;
        }
    } else {
        event_counters["epoch_cache_hit"] += 1;
    }
    return epoch_cache_hit;
}

void tt_epoch_loader::send_static_binaries() {
    log_assert(cluster != nullptr, "Expected Cluster to be initialized");
    if (skip_device_init) return;

    // Get the first binary. All binaries have the same FW for this purpose.
    tt_epoch_program_info &info = graph_to_epoch_map.begin()->second;
    shared_ptr<tt_epoch_binary> binary = info.binary;

    // Iterate through all the worker cores
    // Statically load brisc, ncrisc and erisc firmware to L1
    log_assert(!graph_to_epoch_map.empty(), "graph_to_epoch_map was empty");

    // Broadcast static binaries to all tensix and ethernet cores
    cluster -> broadcast_write_to_all_tensix_in_cluster(binary -> ncrisc_vec, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
    cluster -> broadcast_write_to_all_tensix_in_cluster(binary -> brisc_vec, l1_mem::address_map::FIRMWARE_BASE);
    cluster -> broadcast_write_to_all_tensix_in_cluster(binary  -> blob_bin_init, l1_mem::address_map::OVERLAY_BLOB_BASE+0xC);

    if(cluster -> cluster_arch != tt::ARCH::GRAYSKULL) {
        cluster -> broadcast_write_to_all_eth_in_cluster(binary -> erisc_vec, eth_l1_mem::address_map::FIRMWARE_BASE);
        if(cluster -> cluster_arch == tt::ARCH::WORMHOLE_B0) {
            cluster -> broadcast_write_to_all_eth_in_cluster(binary -> erisc_iram_vec, eth_l1_mem::address_map::TILE_HEADER_BUFFER_BASE);
        }
        cluster -> broadcast_write_to_all_eth_in_cluster(binary -> blob_bin_init, eth_l1_mem::address_map::OVERLAY_BLOB_BASE+0xC);
    }
    
    if(check_binaries) {
        for (int device: target_devices) {
            buda_soc_description &sdesc = cluster->get_soc_desc(device);
            // Read back device contents and check against write data
            vector <uint32_t> rd_vec_nrisc;
            vector <uint32_t> rd_vec_brisc;
            vector <uint32_t> rd_vec_blob;
            for (const tt_xy_pair &core : sdesc.workers) {
                cluster->read_dram_vec(rd_vec_nrisc, tt_cxy_pair(device, core), l1_mem::address_map::NCRISC_FIRMWARE_BASE, binary -> ncrisc_vec.size()*4);
                cluster->read_dram_vec(rd_vec_brisc, tt_cxy_pair(device, core), l1_mem::address_map::FIRMWARE_BASE, binary -> brisc_vec.size()*4);
                cluster->read_dram_vec(rd_vec_blob, tt_cxy_pair(device, core), l1_mem::address_map::OVERLAY_BLOB_BASE+0xC, binary -> blob_bin_init.size()*4);
                log_assert(rd_vec_nrisc == binary -> ncrisc_vec, "nrisc firmware image reads back WRONG from core {}", tt_cxy_pair(device, core).str());
                log_assert(rd_vec_brisc == binary -> brisc_vec, "brisc firmware image reads back WRONG from core {}", tt_cxy_pair(device, core).str());
                log_assert(rd_vec_blob == binary -> blob_bin_init, "blob image reads back WRONG from core {}", tt_cxy_pair(device, core).str());
            }
            vector <uint32_t> rd_vec_erisc;
            vector <uint32_t> rd_vec_erisc_iram;
            vector <uint32_t> rd_vec_erisc_blob;
            for (const tt_xy_pair &ethernet_core : sdesc.ethernet_cores) {
                cluster->read_dram_vec(rd_vec_erisc, tt_cxy_pair(device, ethernet_core), eth_l1_mem::address_map::FIRMWARE_BASE, binary -> erisc_vec.size()*4);
                cluster->read_dram_vec(rd_vec_erisc_blob, tt_cxy_pair(device, ethernet_core), eth_l1_mem::address_map::OVERLAY_BLOB_BASE+0xC, binary -> blob_bin_init.size()*4);
                log_assert(rd_vec_erisc == binary -> erisc_vec, "erisc firmware image reads back WRONG from eth core {}", tt_cxy_pair(device, ethernet_core).str());
                log_assert(rd_vec_erisc_blob == binary -> blob_bin_init, "erisc blob image reads back WRONG from eth core {}", tt_cxy_pair(device, ethernet_core).str());
                if (sdesc.arch == tt::ARCH::WORMHOLE_B0) {
                    cluster->read_dram_vec(rd_vec_erisc_iram, tt_cxy_pair(device, ethernet_core), eth_l1_mem::address_map::TILE_HEADER_BUFFER_BASE, binary->erisc_iram_vec.size()*4);
                    log_assert(rd_vec_erisc_iram == binary->erisc_iram_vec, "erisc iram firmware image reads back WRONG from eth core {}", tt_cxy_pair(device, ethernet_core).str());
                }
            }
        }
    }

    this->load_and_send_padding_constants();
    if(cluster -> cluster_arch != tt::ARCH::GRAYSKULL) {
        // Wait for non-MMIO flush for WH devices. This is needed to ensure that binaries sent through broadcast have been commited.
        cluster -> wait_for_non_mmio_flush();
    }
}

void tt_epoch_loader::load_and_send_padding_constants() {

    string padding_table_file_path = this->output_dir + "/padding_table.yaml";
    log_assert(fs::exists(padding_table_file_path), "The padding_table.yaml file does not exist");
    YAML::Node padding_table_yaml = YAML::LoadFile(padding_table_file_path);
    vector<uint32_t> padding_table_blob;

    for (const auto & pad_entry: padding_table_yaml) {
        const uint32_t address = pad_entry["address"].as<uint32_t>();
        auto padding_entry_blob = pad_entry_to_padding_blob(pad_entry);
        cluster -> broadcast_write_to_all_dram_in_cluster(padding_entry_blob, address);
    }
}

void tt_epoch_loader::send_epoch_binaries(const tt_epoch_program_info &info) {
    perf::record_scope record(perf::HostEventType::SEND_EPOCH_BINARIES);
    log_debug(tt::LogLoader, "\tSending binaries for graph {}", info.name);
    const shared_ptr<tt_epoch_binary> bin = info.binary;
    int target_chip = info.target_device;
   
    for(int hex_id = 0; hex_id < bin -> number_of_tensix_hex_images(); hex_id++) {
        // Send all trisc and overlay binaries corresponding to a single core at once to minimize TLB misses
        tt_hex *hex = &(bin->trisc0_bin_vec[hex_id]);
        log_trace(tt::LogLoader, "\tSending trisc0 bin to noc core (chip={},x={}, y={}) to dram channel: {} @{}", hex -> d_chip_id, (hex -> associated_routing_core).x, (hex -> associated_routing_core).y, hex -> d_chan, hex -> d_addr);
        cluster -> send_hex_to_dram(hex);
        
        hex = &(bin->trisc1_bin_vec[hex_id]);
        log_trace(tt::LogLoader, "\tSending trisc1 bin to noc core (chip={},x={}, y={}) to dram channel: {} @{}", hex -> d_chip_id, (hex -> associated_routing_core).x, (hex -> associated_routing_core).y, hex -> d_chan, hex -> d_addr);
        cluster -> send_hex_to_dram(hex);
        
        hex = &(bin->trisc2_bin_vec[hex_id]);
        log_trace(tt::LogLoader, "\tSending trisc2 bin to noc core (chip={}, x={}, y={}) to dram channel: {} @{}", hex -> d_chip_id, (hex -> associated_routing_core).x, (hex -> associated_routing_core).y, hex -> d_chan, hex -> d_addr);
        cluster -> send_hex_to_dram(hex);

        hex = &(bin->runtime_config_vec[hex_id]);
        log_trace(tt::LogLoader, "\tSending runtime config to noc core (chip={}, x={}, y={}) to dram channel: {} @{}", hex -> d_chip_id, (hex -> associated_routing_core).x, (hex -> associated_routing_core).y, hex -> d_chan, hex -> d_addr);
        cluster -> send_hex_to_dram(hex);
        
        hex = &(bin->blob_bin_vec[hex_id]);
        log_trace(tt::LogLoader, "\tSending blob to noc core (chip={}, x={}, y={}) to dram channel: {} @{}", hex -> d_chip_id, (hex -> associated_routing_core).x, (hex -> associated_routing_core).y, hex -> d_chan, hex -> d_addr);
        cluster -> send_hex_to_dram(hex);
    }

    for (tt_hex &hex : bin->ethernet_blob_bin_vec) {
        log_trace(tt::LogLoader, "\tSending ethernet_blob for noc core (chip={}, x={}, y={}) to dram channel: {} @{}", hex.d_chip_id, hex.associated_routing_core.x, hex.associated_routing_core.y, hex.d_chan, hex.d_addr);
        cluster->send_hex_to_dram(&hex);
    }
    // Insert a Host -> Device DRAM barrier here to ensure that commands don't race ahead of binaries, when on different channels
    cluster -> memory_barrier(MemBarType::host_device_dram, info.target_device); 
    if (check_binaries) {
        for (tt_hex &hex : bin->trisc0_bin_vec) cluster->check_hex_from_dram(&hex, "trisc0");
        for (tt_hex &hex : bin->trisc1_bin_vec) cluster->check_hex_from_dram(&hex, "trisc1");
        for (tt_hex &hex : bin->trisc2_bin_vec) cluster->check_hex_from_dram(&hex, "trisc2");
        for (tt_hex &hex : bin->blob_bin_vec)   cluster->check_hex_from_dram(&hex, "overlay_blob");
        for (tt_hex &hex : bin->ethernet_blob_bin_vec) cluster->check_hex_from_dram(&hex, "ethernet_blob");
        for (tt_hex &hex : bin->runtime_config_vec) cluster->check_hex_from_dram(&hex, "runtime_epoch_config");
    }
}

void tt_epoch_loader::send_epoch_commands(const tt_epoch_program_info &info) {  
    perf::record_scope record(perf::HostEventType::SEND_EPOCH_COMMANDS);
    log_debug(tt::LogLoader, "\tSending epoch commands for graph {}", info.name);
    shared_ptr<tt_epoch_binary> bin = info.binary;
    int target_device = info.target_device;
    
    tt_epoch_control &device_epoch_ctrl = *epoch_ctrl[target_device];
    buda_soc_description &sdesc = cluster->get_soc_desc(info.target_device);
    
    int wrapped_epoch_id = info.epoch_id % epoch_window_size;
    if(!(device_epoch_ctrl.num_cmds_per_epoch_id).count(wrapped_epoch_id)) {
        device_epoch_ctrl.num_cmds_per_epoch_id.insert({wrapped_epoch_id, 0});
    }
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    for(int i=0; i<bin->number_of_tensix_hex_images(); ++i) {
        const tt_xy_pair& routing_core = bin->get_routing_coord(i);
        int queue_index = routing_core.y*device_epoch_ctrl.grid_shape[1] + routing_core.x;

        tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
        log_assert(target_queue->is_active(), "Expected queue to be active.");
        target_queue -> valid_cmd_sent = true;

        // The following logic is repeated for ethernet cores:
        // The command that was pushed at the current wr_ptr references the id in info. Update this in epoch_ids_in_queue. 
        // This queue allows us to map each valid command to its epoch id and check if wrapping the epoch ids will cause aliasing when a new epoch command is pushed.
        target_queue->assign_epoch_info_to_queue_slot(info);

        // Valid command was sent to this core's epoch command queue. Update num_valid_cmds. This field is checked before
        // running update_rd_ptr_for_queues_with_valid_cmds when checking for epoch aliasing.
        target_queue -> num_valid_cmds++;
        device_epoch_ctrl.num_cmds_per_binary.at(info.name)++;
        device_epoch_ctrl.num_cmds_per_epoch_id.at(wrapped_epoch_id)++;
        device_epoch_ctrl.push_command(target_queue, bin -> tensix_hex_vec[i], i == (bin->number_of_tensix_hex_images()-1));

        log_trace(tt::LogLoader, "\tQueue CMD 'EpochValid' for logical core (x={}, y={}) routing core (chip={}, x={}, y={}) to dram channel: {} @{}", sdesc.routing_x_to_worker_x.at(routing_core.x), sdesc.routing_y_to_worker_y.at(routing_core.y), bin->trisc0_bin_vec[i].d_chip_id, routing_core.x, routing_core.y,  bin->trisc0_bin_vec[i].d_chan,  bin->trisc0_bin_vec[i].d_subchannel);
    }  

    if (cluster->type == TargetDevice::Silicon) {
        for(int i = 0; i<bin->number_of_ethernet_hex_images(); ++i) {
            tt_xy_pair routing_core = bin->ethernet_blob_bin_vec[i].associated_routing_core;
            int queue_index = routing_core.y*device_epoch_ctrl.grid_shape[1] + routing_core.x;

            tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
            log_assert(target_queue->is_active(), "Expected queue to be active.");
            target_queue -> valid_cmd_sent = true;

            target_queue->assign_epoch_info_to_queue_slot(info);
            target_queue->num_valid_cmds++;
            device_epoch_ctrl.num_cmds_per_binary.at(info.name)++;
            device_epoch_ctrl.num_cmds_per_epoch_id.at(wrapped_epoch_id)++;
            device_epoch_ctrl.push_command(target_queue, bin -> eth_hex_vec[i], i == (bin->number_of_ethernet_hex_images()-1));
            log_trace(tt::LogLoader, "\tQueue CMD 'EpochValid' for ethernet core (chip={}, x={}, y={}) to dram channel: {} @{}", bin->ethernet_blob_bin_vec[i].d_chip_id, routing_core.x, routing_core.y, bin->ethernet_blob_bin_vec[i].d_chan, bin->ethernet_blob_bin_vec[i].d_subchannel);
        }
    }

    // Insert epoch_not_valid command to the rest of the queues
    vector<uint32_t> qcmd = tt_epoch_control::get_invalid_qcmd();

    const auto &active_queues = device_epoch_ctrl.get_active_queues();
    for (int i = 0; i < active_queues.size(); ++i) {
        tt_epoch_queue *q_ptr = active_queues.at(i);
        if(!q_ptr -> valid_cmd_sent) {
            tt_xy_pair routing_core(q_ptr->associated_routing_core.x, q_ptr->associated_routing_core.y);
            device_epoch_ctrl.push_command(q_ptr, std::make_shared<tt_hex>(qcmd, tt_hex_type::Misc, routing_core)); // Shared pointer for tt_hex will go out of scope once function returns -> memory gets deallocated
        }
        else {
            q_ptr -> valid_cmd_sent = false;
        }
    }

    // Epoch Queue Write Combine flush path will take care of non-mmio-flush and L1 ptr update.
    if (!device_epoch_ctrl.is_epoch_queue_wc_enabled) {
        device_epoch_ctrl.update_all_shadow_l1_wrptrs();
    }

    device_epoch_ctrl.update_curr_gen_id(device_epoch_ctrl.get_next_gen_id());

}

void tt_epoch_loader::send_end_program_commands() {
    vector<uint32_t> qcmd = tt_epoch_control::get_endprog_qcmd();

    for (int device: target_devices) {
        log_debug(tt::LogLoader, "\tSending end program commands to device {}", device);
        tt_epoch_control &device_epoch_ctrl = *epoch_ctrl[device];
        device_epoch_ctrl.push_command_to_all_workers_and_update_l1_wptr(qcmd);
    }

    sent_end_program = true;
}

// Pass 1 : convert netlist instructions to cmdinfo wrpaper struct for easier optimization passes, and eliminate cmds not bound to queues currently.
std::vector<tt_varinst_queue_update_cmd_info> tt_epoch_loader::varinst_cmd_info_list_generate_from_instrns(const tt_runtime_queue_ptrs_wrap &qptrs_wrap, const netlist_workload_data &workload, 
    const unordered_map<string, int> &vars, std::string program_name, int num_iterations) {

    std::vector<tt_varinst_queue_update_cmd_info> varinst_cmd_infos;

    for (auto &intrn_pair : qptrs_wrap.pending_varinst_queue_updates) {

        auto &instrn                = intrn_pair.first;
        auto &update_field_mask     = intrn_pair.second;
        std::string var_name        = get<0>(instrn.vars[0]);

        // It is expected that each variable is mapped to only a single field for now, to reduce code complexity.
        // Currently all fields supported by qptrs_wrap can support varinst-on-device. If this feature needs to
        // support a subset, need to guard them here by inspecting update_field type.
        auto update_field                       = update_field_mask.get_single_field_from_mask(var_name);
        auto &mapped_queues_for_var             = qptrs_wrap.get_queue_set(update_field, var_name);
        tt_queue_settings_sync_type sync_type   = tt_queue_varinst_sync_type_map.at(update_field);

        if (mapped_queues_for_var.size() > 0) {
            auto [opcode, operand_0, operand_1] = get_varinst_cmd_opcode_and_operands(instrn, vars);
            // Update qs_cache on host in sync with what will happen next on device, for all loop iterations.
            for (auto &queue_name : mapped_queues_for_var) {
                const tt_queue_info &queue_info = workload.queues.at(queue_name).my_queue_info;
                update_qs_cache_for_epoch_cmd_varinst(opcode, operand_0, operand_1, update_field, queue_info, num_iterations);
            }

            varinst_cmd_infos.push_back({
                    .var_name = var_name, .opcode = opcode, .operand_0 = operand_0, .operand_1 = operand_1,
                    .update_field_mask = update_field_mask, .queue_names = mapped_queues_for_var, .sync_type = sync_type});
        }
    }

    if constexpr (varinst_cmd_debug == true) {
        for (int i=0; i<varinst_cmd_infos.size(); i++) {
            auto &curr_cmd = varinst_cmd_infos.at(i);
            log_debug(tt::LogLoader, "{} cmd_idx: {} cmd: {}", __FUNCTION__, i, curr_cmd);
        }
    }

    log_debug(tt::LogLoader, "{} EpochCmdVarinst finished opt with num_cmds: {}.", __FUNCTION__, varinst_cmd_infos.size());

    return varinst_cmd_infos;
}


// Pass 2 : merge mathematically commutative operations to reduce number of commands.
void tt_epoch_loader::varinst_cmd_info_list_merge_commutative(std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos) {

    // Convert 2D vector of cmds into map of commands by var_name
    std::unordered_map<std::string, std::vector<tt_varinst_queue_update_cmd_info>> var_name_to_cmds_map_raw;
    for (auto &cmd_info : varinst_cmd_infos) {
        var_name_to_cmds_map_raw[cmd_info.var_name].push_back(cmd_info);
    }

    std::unordered_map<std::string, std::vector<tt_varinst_queue_update_cmd_info>> var_name_to_cmds_map_merged;
    for (auto &it : var_name_to_cmds_map_raw) {
        auto &var_name = it.first;
        auto &cmds = it.second;

        for (auto &curr_cmd : cmds) {
            // Add to map as-is if new, or not commutative, otherwise merge (replace prev_cmd)
            if (!var_name_to_cmds_map_merged.count(var_name)) {
                var_name_to_cmds_map_merged[var_name].push_back(curr_cmd);
            } else {
                auto prev_idx       = var_name_to_cmds_map_merged[var_name].size()-1;
                auto &prev_cmd      = var_name_to_cmds_map_merged[var_name].at(prev_idx);
                if (curr_cmd.are_varinst_cmds_commutative(prev_cmd)) {
                    auto merged_cmd = curr_cmd.merge_commutative_varinst_cmds(prev_cmd);
                    var_name_to_cmds_map_merged[var_name].pop_back();
                    var_name_to_cmds_map_merged[var_name].push_back(merged_cmd);
                } else {
                    var_name_to_cmds_map_merged[var_name].push_back(curr_cmd);
                }
            }
        }
    }

    // Convert back to single vector of commands.
    varinst_cmd_infos.clear();
    for (auto &it : var_name_to_cmds_map_merged) {
        for (auto &cmd: it.second) {
            varinst_cmd_infos.push_back(cmd);
        }
    }

    if constexpr (varinst_cmd_debug == true) {
        for (int i=0; i<varinst_cmd_infos.size(); i++) {
            auto &curr_cmd = varinst_cmd_infos.at(i);
            log_debug(tt::LogLoader, "{} cmd_idx: {} cmd: {}", __FUNCTION__, i, curr_cmd);
        }
    }

    log_debug(tt::LogLoader, "{} EpochCmdVarinst finished opt with num_cmds: {}.", __FUNCTION__, varinst_cmd_infos.size());
}


// Pass 3: Optimization to merge varinst instrn/cmds that will update LocalRdptr and GlobalRdpr the same way for matching queues.
void tt_epoch_loader::varinst_cmd_info_list_merge_local_global(std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos) {

    // Create intermediates map useful for detecting matches.
    std::unordered_map<std::string, std::vector<int>> var_name_to_cmd_idx_vector_map;
    std::unordered_map<std::string, std::unordered_map<bool, std::string>> queue_name_to_type_and_var_name_map;
    for (int i=0; i<varinst_cmd_infos.size(); i++) {
        auto &cmd_info = varinst_cmd_infos.at(i);
        var_name_to_cmd_idx_vector_map[cmd_info.var_name].push_back(i);

        for (auto &queue_name : cmd_info.queue_names) {
            auto global_rdptr = cmd_info.update_field_mask.has_field(tt_queue_header_field::GlobalRdptr);
            auto local_rdptr  = cmd_info.update_field_mask.has_field(tt_queue_header_field::LocalRdptr);
            if (global_rdptr || local_rdptr) {
                queue_name_to_type_and_var_name_map[queue_name][global_rdptr] = cmd_info.var_name;
            }
        }
    }

    // Find sibling variable name, cmds for that variable, and a matching cmd (bound to same set of queues) if it exists.
    for (int i=0; i<varinst_cmd_infos.size(); i++) {
        auto &curr_cmd                  = varinst_cmd_infos.at(i);
        std::string first_queue_name    = *curr_cmd.queue_names.begin();

        auto global_rdptr = curr_cmd.update_field_mask.has_field(tt_queue_header_field::GlobalRdptr);
        auto local_rdptr  = curr_cmd.update_field_mask.has_field(tt_queue_header_field::LocalRdptr);

        if (!global_rdptr && !local_rdptr) {
            continue;
        }

        bool has_sibling = queue_name_to_type_and_var_name_map.at(first_queue_name).count(!global_rdptr) > 0;

        if (!has_sibling || !curr_cmd.valid_cmd) {
            continue;
        }

        std::string sibling_var_name    = queue_name_to_type_and_var_name_map.at(first_queue_name).at(!global_rdptr);
        auto &sibling_cmd_indices       = var_name_to_cmd_idx_vector_map.at(sibling_var_name);
        int num_sibling_cmds            = sibling_cmd_indices.size();

        // Temp workaround - This should be handled properly, but avoid it for now until it can be tested more.
        if (num_sibling_cmds > 1) {
            log_warning(tt::LogLoader, "cmd_idx: {} Skipping gpr+rdptr opt for var_name: {} and sibling_var_name: {} since multiple instrns per var.", i, curr_cmd.var_name, sibling_var_name);
            continue;
        }

        // Find first sibling (local or global rdptr matching cmd), merge them and mark sibling as invalid.
        for (auto &idx : sibling_cmd_indices) {
            auto &sibling_cmd = varinst_cmd_infos.at(idx);
            if (curr_cmd.are_local_global_rdptr_siblings(sibling_cmd)) {
                curr_cmd.merge_local_globl_rdptr_siblings(sibling_cmd);
                log_trace(tt::LogLoader, "Merged cmd_idx: {} cmd: {} with sibling_cmd: {}", i, curr_cmd, sibling_cmd);
                break; // Merged, no need to check further.
            }
        }
    }

    remove_invalid_commands(varinst_cmd_infos);

    if constexpr (varinst_cmd_debug == true) {
        for (int i=0; i<varinst_cmd_infos.size(); i++) {
            auto &curr_cmd = varinst_cmd_infos.at(i);
            log_debug(tt::LogLoader, "{} cmd_idx: {} cmd: {}", __FUNCTION__, i, curr_cmd);
        }
    }

    log_debug(tt::LogLoader, "{} EpochCmdVarinst finished opt with num_cmds: {}.", __FUNCTION__, varinst_cmd_infos.size());
}

// Pass 4: Convert from cmdinfo to commands and binaries needed to be sent to device, and send them.  Consider enabled optimizations for merging queues, buffers,
// but inline (cmd+addr as single command) if num_buffers is less than threshold.
void tt_epoch_loader::generate_and_send_varinst_cmds_from_cmd_info_list(const std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos, const tt_runtime_workload &workload) {

    // Max threshold for inlining queue update cmds
    const static int max_num_buffers_for_inline = get_max_num_buffers_for_inline(2);
    const static bool opt_merge_buffers_per_queue = true; // Could be disabled for debug

    for (int i=0; i<varinst_cmd_infos.size(); i++) {
        auto &info = varinst_cmd_infos.at(i);
        uint16_t num_buffers = 0;
        log_trace(tt::LogLoader, "Generating EpochCmdVarinst from cmd_idx: {} num_queues: {} cmd: {} ", i, info.queue_names.size(), info);
        if (enable_varinst_merge_io_queue_updates) {
            for (auto &queue_name : info.queue_names) {
                num_buffers += workload.queues.at(queue_name).my_queue_info.alloc_info.size();
            }
            if (num_buffers <= max_num_buffers_for_inline) {
                generate_and_send_varinst_cmds_inline(info, workload, info.queue_names);
            } else {

                // Must not merge updates to queues on different devices into single command.
                std::unordered_map<int, std::set<std::string>> queue_names_by_device_id_map;
                for (const auto &queue_name : info.queue_names) {
                    const tt_queue_info &queue_info = workload.queues.at(queue_name).my_queue_info;
                    queue_names_by_device_id_map[queue_info.target_device].insert(queue_name);
                }

                for (auto &it : queue_names_by_device_id_map) {
                    generate_and_send_varinst_cmds_external(info, workload, it.second);
                }
            }
        }else if (opt_merge_buffers_per_queue) {
            for (auto &queue_name : info.queue_names) {
                if (workload.queues.at(queue_name).my_queue_info.alloc_info.size() <= max_num_buffers_for_inline) {
                    generate_and_send_varinst_cmds_inline(info, workload, {queue_name});
                } else {
                    num_buffers = workload.queues.at(queue_name).my_queue_info.alloc_info.size();
                    generate_and_send_varinst_cmds_external(info, workload, {queue_name});
                }
            }
        } else {
            generate_and_send_varinst_cmds_inline(info, workload, info.queue_names);
        }
    }

    log_debug(tt::LogLoader, "{} EpochCmdVarinst finished generating and sending varinst cmds. num_cmds: {}.", __FUNCTION__, varinst_cmd_infos.size());
}


// Inlined means a single cmd for every queue buffer per queue. Address of bufer is inlined in command.
void tt_epoch_loader::generate_and_send_varinst_cmds_inline(const tt_varinst_queue_update_cmd_info &info, const tt_runtime_workload &workload, std::unordered_set<std::string> queue_names) {
    const static uint8_t num_buffers    = 1;
    log_debug(tt::LogLoader, "{} starting with num_buffers: {} cmd: {} queue_names: {}", __FUNCTION__, num_buffers, info, queue_names);
    const auto &dual_view_rams = workload.get_dual_view_rams_map();

    for (auto &queue_name : queue_names){
        const tt_queue_info &queue_info = workload.queues.at(queue_name).my_queue_info;
        buda_soc_description &sdesc = cluster->get_soc_desc(queue_info.target_device);
        log_trace(tt::LogLoader, "\tGenerating EpochCmdVarinst for var: {} bound to queue: {} update_field_mask: {} buffers: {} (opcode: {} operand_0: {} operand_1: {}) ",
            info.var_name, queue_name, info.update_field_mask.value, queue_info.alloc_info.size(), info.opcode, info.operand_0, info.operand_1);
        uint16_t update_mask = get_varinst_cmd_update_mask(info);
        int buffer_index = 0;

        // Dual View Rams must sync on both producer/consumers to avoid race conditions.
        bool is_dual_view_ram = dual_view_rams.find(queue_name) != dual_view_rams.end();

        for (auto &alloc : queue_info.alloc_info){
            const auto buffer_addr =
                (uint64_t)alloc.address + (queue_info.loc == QUEUE_LOCATION::DRAM ? 0 : sdesc.get_noc2host_offset(alloc.channel));
            const auto buffer_core = queue_info.loc == QUEUE_LOCATION::DRAM
                                         ? sdesc.get_core_for_dram_channel(alloc.channel, 0)
                                         : sdesc.get_pcie_core();

            // For dual-view RAMS, will sync on producers+consumers (pre-populated for both views), share single cache entry via base name.
            const auto buffer_name = get_base_queue_name(queue_info) + "_varinst_sync_type_" + std::to_string((int)info.sync_type) + "_buffer_" + std::to_string(buffer_index);
            if (state.sync_cores_cache.find(buffer_name) == state.sync_cores_cache.end()) {
                if ((info.sync_type == tt_queue_settings_sync_type::SyncOnConsumers) || (info.sync_type == tt_queue_settings_sync_type::SyncOnBoth) || is_dual_view_ram) {
                    get_unique_consumer_cores_from_queue_buffer(queue_name, alloc, queue_info.target_device, state.sync_cores_cache[buffer_name], true);
                }
                if ((info.sync_type == tt_queue_settings_sync_type::SyncOnProducers) || (info.sync_type == tt_queue_settings_sync_type::SyncOnBoth) || is_dual_view_ram) {
                    get_unique_producer_cores_from_queue_buffer(queue_name, alloc, state.sync_cores_cache[buffer_name], true);
                }
            }
            uint8_t num_sync_cores  = state.sync_cores_cache[buffer_name].size();
            uint8_t reader_index = 0; // Could also be a writer_index, if syncing on producers.

            for (const tt_xy_pair &core : state.sync_cores_cache[buffer_name]) {
                auto cmd = get_varinst_cmd_for_io_queue_update(
                    info, buffer_addr, buffer_core, num_buffers, reader_index, num_sync_cores, update_mask);

                log_trace(
                    tt::LogLoader,
                    "\t\tSending EpochCmdVarinst(Inline) for var_name: {} queue: {} addr: 0x{:x} dram: {} num_buffers: "
                    "{} core: {} reader_index: {} device: {}",
                    info.var_name,
                    queue_name,
                    cmd.dram_addr_lower,
                    buffer_core.str(),
                    num_buffers,
                    core.str(),
                    reader_index,
                    queue_info.target_device);
                send_epoch_cmd_varinst(cmd, queue_info.target_device, core);
                reader_index++;
            }
            buffer_index++;
        }
    }
}

// Generate Binary, where binary is a list of buffer DRAM addr/channels, placed in a DRAM queue. If number of buffers would exceed binary size, split into smaller binaries.
void tt_epoch_loader::generate_queue_update_external_binaries(
    const std::set<std::string>& queue_names, std::string& cache_key, const map<string, tt_queue_wrap> &queues, 
    const tt_queue_settings_sync_type &sync_type, const buda_soc_description& sdesc, std::vector<std::unordered_set<tt_xy_pair>>& sync_cores_per_group,
    std::vector<tt_hex>& external_binaries_per_group, std::vector<uint32_t>& num_buffers_per_group, bool loop_on_device) {

    cache_key = (loop_on_device ? "VARINST_EXTERNAL_" : "IO_UPDATE_EXTERNAL_")  + std::to_string((int)sync_type) + "_";
    for(const auto& queue : queue_names) {
        cache_key += queue + ",";
    }
    
    if(state.external_binary_cache.find(cache_key) == state.external_binary_cache.end()) {
        std::vector<std::tuple<std::string, tt_queue_allocation_info>> buffers_to_update = {};

        std::tuple<std::vector<std::unordered_set<tt_xy_pair>>, std::vector<tt_hex>, std::vector<uint32_t>> cache_entry;
        state.external_binary_cache.insert({cache_key, cache_entry});

        auto& sync_cores = std::get<0>(state.external_binary_cache.at(cache_key));
        auto& external_binaries = std::get<1>(state.external_binary_cache.at(cache_key));
        auto& num_buffers_in_group =  std::get<2>(state.external_binary_cache.at(cache_key));

        std::set<int> queue_device_ids_set = {};
        for(const auto& queue : queue_names) {
            const tt_queue_info &queue_info = queues.at(queue).my_queue_info;
            queue_device_ids_set.insert(queue_info.target_device);
            for(const auto& buf : queue_info.alloc_info) {
                buffers_to_update.push_back({queue, buf});
            }
        }
        log_assert(queue_device_ids_set.size() == 1, "Queues covering more than one target_device is not supported/expected");
        auto buf_it = buffers_to_update.begin();
        while(buf_it != buffers_to_update.end()) {
            std::vector<std::tuple<std::string, tt_queue_allocation_info>> bufs_to_update = {};
            while(buf_it != buffers_to_update.end()) {
                if(bufs_to_update.size() >= epoch_queue::get_queue_update_blob_num_entries()) {
                    break;
                }
                bufs_to_update.push_back(*buf_it);
                buf_it++;
            }
            std::vector<uint32_t> update_binaries = get_buffer_update_read_binaries(bufs_to_update, queues, sdesc);
            external_binaries.push_back(tt_hex(update_binaries, tt_hex_type::Misc, nullptr));
            sync_cores.push_back({});
            if ((sync_type == tt_queue_settings_sync_type::SyncOnConsumers) || (sync_type == tt_queue_settings_sync_type::SyncOnBoth)){
                get_unique_consumer_cores_from_buffers(bufs_to_update, sync_cores.back(), true);
            }
            if ((sync_type == tt_queue_settings_sync_type::SyncOnProducers) || (sync_type == tt_queue_settings_sync_type::SyncOnBoth)){
                get_unique_producer_cores_from_buffers(bufs_to_update, sync_cores.back(), true);
            }
            num_buffers_in_group.push_back(bufs_to_update.size());
        }
    }
    sync_cores_per_group = std::get<0>(state.external_binary_cache.at(cache_key));
    external_binaries_per_group = std::get<1>(state.external_binary_cache.at(cache_key));
    num_buffers_per_group =  std::get<2>(state.external_binary_cache.at(cache_key));
}

// External means a single cmd + binary per core, the cmd points to binary, and binary contains DRAM addr/ch for every buffer being updated.
void tt_epoch_loader::generate_and_send_varinst_cmds_external(const tt_varinst_queue_update_cmd_info &info, const tt_runtime_workload &workload, const std::set<std::string>& queue_names) {
    log_debug(tt::LogLoader, "{} starting for cmd: {} queue_names.size(): {} queue_names: {} update_field_mask: {}",
        __FUNCTION__, info, queue_names.size(), queue_names, info.update_field_mask.value);
    // Only one device is allowed. Assert otherwise.
    int target_device = workload.queues.at(*queue_names.begin()).my_queue_info.target_device;
    buda_soc_description &sdesc = cluster->get_soc_desc(target_device);
    auto &dram_allocators       = dram_mgr[target_device]->get_dram_allocators();
    auto &device_epoch_ctrl     = *epoch_ctrl[target_device];
    uint16_t update_mask        = get_varinst_cmd_update_mask(info);

    std::vector<tt_hex> external_binaries_per_group = {};
    std::vector<std::unordered_set<tt_xy_pair>> sync_cores_per_group = {};
    std::vector<uint32_t> num_buffers_per_group = {};
    std::string cache_key = "";
    generate_queue_update_external_binaries(queue_names, cache_key, workload.queues, info.sync_type, sdesc, sync_cores_per_group, external_binaries_per_group, num_buffers_per_group, true);
    uint64_t binary_size = epoch_queue::get_queue_update_blob_size_bytes();
    for(int buf_group = 0; buf_group < sync_cores_per_group.size(); buf_group++) {
        std::string group_cache_key = cache_key + std::to_string(buf_group);
        tt_hex& update_hex = external_binaries_per_group.at(buf_group);
        std::unordered_set<tt_xy_pair>& sync_cores = sync_cores_per_group.at(buf_group);
        uint32_t num_buffers = num_buffers_per_group.at(buf_group);

        uint8_t num_sync_cores  = sync_cores.size();
        uint8_t reader_index    = 0; // Could also be a writer_index, if syncing on producers.

        for (const tt_xy_pair &core : sync_cores) {
            // This next chunk is basically common with updateIOQueueCmd, could be commonized "get_epoch_io_queue_update_info()"
            // Get the core for current reader where the blob must be pushed into
            auto [dram_channel, dram_subchannel] = dram_mgr[target_device]->get_dram_chan(sdesc.arch, core, sdesc.dram_core_channel_map);

            int queue_index                 = core.y*device_epoch_ctrl.grid_shape[1] + core.x;
            uint worker_idx_within_channel  = dram_mgr[target_device]->get_worker_idx_in_channel(core, dram_channel);
            const tt_xy_pair dram_core      = sdesc.get_core_for_dram_channel(dram_channel, dram_subchannel);
            // The dram core and the address for this epoch command must point to the blob that contains the queue header address
            // Get the epoch queue command index for the worker consumer core
            tt_epoch_queue* target_queue            = device_epoch_ctrl.get_q_ptr(queue_index);
            int epoch_q_slot                        = (target_queue -> dram_ptrs).get_wr_ptr();
            uint64_t start_addr_of_external_blob    = dram_allocators[dram_channel].top_of_binaries + worker_idx_within_channel * (uint64_t)epoch_queue::get_epoch_io_queue_update_num_slots() * binary_size;
            int external_blob_slot;
            auto& num_cmds_per_update_blob          = device_epoch_ctrl.num_cmds_per_q_update_blob[queue_index];
            auto& io_q_update_ptrs                  = device_epoch_ctrl.io_q_update_ptrs[queue_index];
            std::vector< tt_epoch_queue*>cmd_q      = {target_queue};
            bool pin_binary                         = state.in_loop_on_device;
            bool cache_hit                          = device_epoch_ctrl.io_queue_update_caches[queue_index].get_slot_for_binary(group_cache_key, num_cmds_per_update_blob, 
                                                        io_q_update_ptrs, external_blob_slot, cmd_q, cluster, false, pin_binary, true);
            uint64_t external_blob_dram_addr = start_addr_of_external_blob + external_blob_slot * binary_size;

            log_trace(tt::LogLoader, "External. dram_ch: {} cache_hit: {} external_blob_slot: {} start_addr_of_external_blob: 0x{:x} external_blob_dram_addr: 0x{:x} reader_index: {}", 
                dram_channel, cache_hit, external_blob_slot, start_addr_of_external_blob, external_blob_dram_addr, reader_index);

            if(!cache_hit) {
                update_hex.set_dram_chan(dram_channel);
                update_hex.set_dram_subchannel(dram_subchannel);
                update_hex.set_dram_addr(external_blob_dram_addr);
                update_hex.set_chip_id(target_device);
                cluster->send_hex_to_dram(&update_hex, true);
                tt_driver_atomics::sfence();
                dram_profiler.add_report_for_q_update_blob(&device_epoch_ctrl, &update_hex, "varinst-cmd", core, external_blob_dram_addr);
            }

            auto cmd = get_varinst_cmd_for_io_queue_update(info, external_blob_dram_addr, dram_core, num_buffers, reader_index, num_sync_cores, update_mask);
            log_trace(tt::LogLoader, "\t\tSending EpochCmdVarinst(External) for var_name: {} addr: 0x{:x} dram: {} num_buffers: {} core: {} reader_index: {} device: {}",
                info.var_name, cmd.dram_addr_lower, dram_core.str(), num_buffers, core.str(), reader_index, target_device);
            send_epoch_cmd_varinst(cmd, target_device, core);

            reader_index++;
        }
    }
}


void tt_epoch_loader::update_io_queue_rdptrs_varinst_on_device(const tt_runtime_queue_ptrs_wrap &qptrs_wrap, const tt_runtime_workload &workload,
    const unordered_map<string, int> &vars, std::string program_name, int num_iterations) {

    perf::record_scope record(perf::HostEventType::QUEUE_UPDATE_VARINST);
    auto start_time = std::chrono::high_resolution_clock::now(); // Quick profiling, remove once optimized.
    int num_pending_instrns = qptrs_wrap.pending_varinst_queue_updates.size();

    log_assert(enable_looping_on_device, "Varinst on device expected to have looping on device enabled");

    auto varinst_cmd_infos = varinst_cmd_info_list_generate_from_instrns(qptrs_wrap, workload, vars, program_name, num_iterations);
    int num_varinst_cmds_orig = varinst_cmd_infos.size();

    varinst_cmd_info_list_merge_commutative(varinst_cmd_infos);
    varinst_cmd_info_list_merge_local_global(varinst_cmd_infos);
    generate_and_send_varinst_cmds_from_cmd_info_list(varinst_cmd_infos, workload);

    if constexpr (varinst_cmd_debug == true) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
        log_info(tt::LogLoader, "{} finished for {} instrns ({} cmds before opts, {} cmds after opts) merge_queues: {} for program: {} num_iterations: {}. Duration: {} us.",
            __FUNCTION__, num_pending_instrns, num_varinst_cmds_orig, varinst_cmd_infos.size(), enable_varinst_merge_io_queue_updates, program_name, num_iterations, duration.count());
    }
}


// Given a command struct, convert to 32bit words, and send it to desired device and core.
void tt_epoch_loader::send_epoch_cmd_varinst(const epoch_queue::VarinstCmdInfo &cmd, int device_id, const tt_xy_pair &reader_xy) {
    tt_epoch_control &device_epoch_ctrl = *epoch_ctrl[device_id];

    // Convert from struct to vector of words for sending to device.
    const uint32_t* cmd_ptr = reinterpret_cast<const uint32_t*>(&cmd);
    vector<uint32_t> varinst_cmd(cmd_ptr, cmd_ptr + sizeof(epoch_queue::VarinstCmdInfo) / sizeof(uint32_t));

    // Get the epoch queue command index for the worker consumer core
    int queue_index = reader_xy.y*device_epoch_ctrl.grid_shape[1] + reader_xy.x;
    tt_epoch_queue* target_queue = device_epoch_ctrl.get_q_ptr(queue_index);
    log_assert(target_queue->is_active(), "Expected queue to be active.");
    wait_for_epoch_queue_slot(target_queue, cluster);
    device_epoch_ctrl.push_command_and_update_l1_wptr(target_queue, std::make_shared<tt_hex>(varinst_cmd, tt_hex_type::Misc, reader_xy, "varinst-cmd"));
}

// Update qs cache to mimic what device will do when using Varinst on device to update Queue Header rdptrs.
inline void tt_epoch_loader::update_qs_cache_for_epoch_cmd_varinst(uint16_t opcode, uint32_t operand_0, uint32_t operand_1, tt_queue_header_field update_field, const tt_queue_info &queue_info, int num_iterations){

    auto &qs_cache_header = epoch_ctrl.at(queue_info.target_device)->get_cached_io_qs(queue_info);
    tt_queue_header_wrap qs_cache_header_wrap = qs_cache_header; // Copy to wraper struct, for checking.

    switch (update_field) {
        case tt_queue_header_field::GlobalRdptr:
        {
            auto orig = qs_cache_header.global_rd_ptr;
            update_var_for_varinst_cmd_opcode(num_iterations, qs_cache_header.global_rd_ptr, opcode, operand_0, operand_1);
            log_trace(tt::LogLoader, "\tUpdated qs_cache for {} global_rdptr: {} for EpochCmdVarinst (was: {})", queue_info.name, qs_cache_header.global_rd_ptr, orig);
            break;
        }
        case tt_queue_header_field::LocalRdptr:
        {
            auto orig = qs_cache_header.local_rd_ptr;
            update_var_for_varinst_cmd_opcode(num_iterations, qs_cache_header.local_rd_ptr, opcode, operand_0, operand_1);
            log_trace(tt::LogLoader, "\tUpdated qs_cache for {} local_rdptr: {} for EpochCmdVarinst (was: {})", queue_info.name, qs_cache_header.local_rd_ptr, orig);
            break;
        }
        case tt_queue_header_field::GlobalWrptr:
        {
            auto orig = qs_cache_header.global_wr_ptr;
            update_var_for_varinst_cmd_opcode(num_iterations, qs_cache_header.global_wr_ptr, opcode, operand_0, operand_1);
            log_trace(tt::LogLoader, "\tUpdated qs_cache for {} global_wr_ptr: {} for EpochCmdVarinst (was: {})", queue_info.name, qs_cache_header.global_wr_ptr, orig);
            break;
        }
        case tt_queue_header_field::ZeroSetting:
        {
            auto orig = qs_cache_header.zero;
            uint16_t temp_zero = qs_cache_header.zero; // Copy since cannot pass bitfield by reference.
            update_var_for_varinst_cmd_opcode(num_iterations, temp_zero, opcode, operand_0, operand_1);
            qs_cache_header.zero = temp_zero;
            log_trace(tt::LogLoader, "\tUpdated qs_cache for {} zero: {} for EpochCmdVarinst (was: {})", queue_info.name, (int) qs_cache_header.zero, orig);

            // Restriction: Check the neighbor fields in same 16b container, EpochCmdVarinst doesn't support operation on less than 8 bit fields, safely.
            qs_cache_header_wrap.header.zero = 0;
            auto field_val_16b = qs_cache_header_wrap.get_16b_word(get_16b_header_position_for_field(update_field));
            log_assert(field_val_16b == 0x0, "16b word in queue {} header containing 'zero' is 0x{:x} but must be 0x0 for varinst-on-device.", queue_info.name, field_val_16b);
            break;
        }
        default:
            log_fatal("Unupported varinst-on-device type for qs_cache updated");
            break;
    }
}

// Ensure Host and Device are in sync, to verify assumptions in EpochCmdVarinst generation at end-of-loop. Ideally, the looping on-device feature
// could be statically checked during initalization for netlist and only enabled when requirements are satisified, but that doesn't exist, yet.
void tt_epoch_loader::check_io_queue_rdptrs_varinst_on_device(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings,
    const unordered_map<string, int> &vars, const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams, const std::string &check_name) {

    perf::record_scope record(perf::HostEventType::QUEUE_CHECK_VARINST);

    // This matches run_execute instruction.
    tt_queue_header_mask header_mask = {tt_queue_header_mask::GLOBAL_RD_PTR_MASK | tt_queue_header_mask::LOCAL_SETTINGS_MASK};

    for (auto &queue_setting : queue_settings) {

        const std::string queue_name        = queue_setting.name;
        const tt_queue_info &queue_info     = queues.at(queue_name).my_queue_info;
        tt_queue_header_wrap header_wrap    = get_queue_header_wrap(queue_info, queue_setting, vars);
        auto &qs_cache_header               = epoch_ctrl.at(queue_info.target_device)->get_cached_io_qs(queue_info);

        // Same condition to set gwrptr mask bit as updating settings on device.
        tt_queue_header_mask update_mask = header_mask;
        if (queue_info.type == tt::IO_TYPE::RandomAccess and (not get_variable_from_map(queue_setting.read_only, vars))) {
            update_mask.set_global_wr_ptr();

            // If writer view of aliased queue, do not compare global_rdptr/autoinc fields which may be updated already
            // and mismatch here if both dual-view ram views are used by the same graph.
            bool is_dual_view_ram = dual_view_rams.find(queue_name) != dual_view_rams.end();
            if (is_dual_view_ram) {
                update_mask.clear_global_rd_ptr();
            }
        }

        log_assert(header_wrap.matches(qs_cache_header, update_mask),
            "Mismatch at {} queue_name: {} between host and device. Not suitable for program looping on device. Host: {} Device: {}",
            check_name, queue_name, header_wrap.header, qs_cache_header);
    }
}


// Insert beginning of loop command for program looping on device.
void tt_epoch_loader::send_loop_start_command(uint64_t num_loops) {
    perf::record_scope record(__FUNCTION__);
    vector<uint32_t> qcmd = tt_epoch_control::get_loopstart_qcmd(num_loops);

    for (int device: target_devices) {
        tt_epoch_control &device_epoch_ctrl = *epoch_ctrl[device];
        if (!device_epoch_ctrl.is_epoch_queue_wc_enabled) {
            while (!device_epoch_ctrl.all_not_full_dram()) { /* still full, try again */}
        }
        // Record wrptr slot where LoopStart cmd will be placed. For later checking to find if insufficient space for entire loop's cmds.
        device_epoch_ctrl.record_wr_ptr_device_loop_start_cmds();
        device_epoch_ctrl.push_command_to_all_workers_and_update_l1_wptr(qcmd);
    }
}

// Insert end of loop command for program looping on device.
void tt_epoch_loader::send_loop_end_command(std::string prog_name) {
    perf::record_scope record(__FUNCTION__);
    vector<uint32_t> qcmd = tt_epoch_control::get_loopend_qcmd();

    for (int device: target_devices) {
        tt_epoch_control &device_epoch_ctrl = *epoch_ctrl[device];
        if (!device_epoch_ctrl.is_epoch_queue_wc_enabled) {
            while (!device_epoch_ctrl.all_not_full_dram()) { /* still full, try again */}
        }
        device_epoch_ctrl.push_command_to_all_workers_and_update_l1_wptr(qcmd);
        device_epoch_ctrl.clear_wr_ptr_device_loop_start_cmds(prog_name);
    }
}

// Transition in/out of looping on device, used for binary cache checking purposes. Clear binary cache pins when done looping on device.
void tt_epoch_loader::set_in_loop_on_device(bool in_loop_on_device) {
    state.in_loop_on_device = in_loop_on_device;

    if (enable_write_combine_epoch_cmds) {
        set_epoch_queues_write_combine_ena(in_loop_on_device);
    }

    if (!in_loop_on_device) {
        log_debug(tt::LogLoader, "Clearing pinned binaries at end of final loop on device now...");
        for (auto it = epoch_ctrl.begin(); it != epoch_ctrl.end(); ++it) {
            it->second->cache.clear_pinned_binaries();
            for (auto &ioq_cache : it->second->io_queue_update_caches) {
                ioq_cache.clear_pinned_binaries();
            }
        }
    }
}

bool tt_epoch_loader::get_in_loop_on_device() {
    return state.in_loop_on_device;
}


void tt_epoch_loader::wait_for_epoch_progress(tt_epoch_control &ctrl, int cmds_remaining) {
    const std::lock_guard<std::mutex> lock(epoch_ctrl_mutex);
    perf::record_scope record(perf::HostEventType::WAIT_FOR_EPOCH_COMPLETE, ctrl.associated_chip);
    int curr_gen_id = ctrl.get_curr_gen_id();
    int sync_gen_id = ctrl.get_sync_gen_id();
    bool epoch_in_progress = ctrl.is_epoch_in_progress();
    if (epoch_in_progress) {
        log_debug(tt::LogLoader, "\tEpochs sync on device {} waiting for epoch generation={} to complete, last sync'd on generation={}", ctrl.associated_chip, curr_gen_id, sync_gen_id);
        bool epoch_done = false;
        ctrl.wait_for_cmds_remaining(cmds_remaining);
        if (cmds_remaining == 0) {
            ctrl.clear_all_queues_in_use();
        }
        event_counters["epoch_barrier"]++;
    } else {
        log_debug(tt::LogLoader, "\tEpochs sync on device {} skipped since last sync'd generation={} is already the latest", ctrl.associated_chip, sync_gen_id);
        log_assert(sync_gen_id == curr_gen_id, "Sync Gen should match Curr Gen after waiting for Epoch Progress.");
    }
}

void tt_epoch_loader::wait_for_epoch_queues_ready(tt_epoch_control &ctrl, tt_epoch_program_info& info) {
    bool allow_epoch_aliasing = parse_env("TT_BACKEND_ALLOW_EPOCH_ALIASING", false);
    bool epoch_queues_not_full = false;
    if (!epoch_queues_not_full) {
        perf::record_scope record(perf::HostEventType::WAIT_FOR_EPOCH_QUEUES_READY, info.target_device);
        while (!epoch_queues_not_full) {
            epoch_queues_not_full = ctrl.all_not_full_dram();
        }
    }

    // Issue #885: Cannot push commands for epochs with ids that match those of active epochs when wrapped to [0, max_offset_between_epoch_ids):
    // If avoid_epoch_aliasing is set:  Check if the incoming epoch aliases with an epoch that had active commands the last time rd ptrs were updated by checking
    // num_cmds_per_epoch_id map. If aliased epoch with active commands found, hazard avoidance depends on whether program-looping or write-combine featues enabled.
    //  - If write-combine or program-looping are enabled (#2181) a full-grid-sync set of cmds are inserted as needed based on last-sync state.
    //  - otherwise host stalls on device progress until previous epochs being aliased with are finished.
    if(!allow_epoch_aliasing) {
        int wrapped_epoch_id            = info.epoch_id % epoch_window_size;
        auto &sync_tracker              = get_epoch_id_aliasing_sync_tracker(ctrl.associated_chip);
        bool avoid_via_full_grid_sync   = state.in_loop_on_device || ctrl.is_epoch_queue_wc_enabled;
        bool has_epoch_id_alias_hazard  = ctrl.has_epoch_id_alias_hazard_with_device(sync_tracker, wrapped_epoch_id, info.epoch_id, info.name, avoid_via_full_grid_sync);

        if (has_epoch_id_alias_hazard) {
            event_counters["epoch_id_alias_hazards"]++;

            if (avoid_via_full_grid_sync) {
                insert_full_grid_sync(ctrl);
                sync_tracker.last_sync_idx = sync_tracker.curr_idx - 1; // Sync does not include current epoch
                if (state.in_loop_on_device) sync_tracker.loop_on_device_requires_sync = true;
            } else {
                perf::record_scope record(perf::HostEventType::WAIT_FOR_EPOCH_UNALIASED, info.target_device);
                while(ctrl.has_epoch_id_alias_hazard_with_device(sync_tracker, wrapped_epoch_id, info.epoch_id, info.name, false)) {
                    ctrl.update_rd_ptr_for_queues_with_valid_cmds();
                }
            }
        }
        sync_tracker.wrapped_epoch_id_to_last_idx_map[wrapped_epoch_id] = sync_tracker.curr_idx++;
        sync_tracker.wrapped_epoch_id_to_last_epoch_id_map[wrapped_epoch_id] = info.epoch_id;
    }
}

// When looping on device, if aliasing was found within first iteration of loop, must avoid some cores starting
// next iteration and potentially hitting aliasing hazard with some cores on prev iter, by inserting another sync.
void tt_epoch_loader::avoid_loop_on_device_epoch_id_aliasing() {
    for (int device : target_devices) {
        auto &sync_tracker = get_epoch_id_aliasing_sync_tracker(device);
        if (sync_tracker.loop_on_device_requires_sync) {
            log_debug(tt::LogLoader, "{} - Inserting full-grid-sync for device: {}", __FUNCTION__, device);
            auto &ctrl = *epoch_ctrl[device];
            insert_full_grid_sync(ctrl);
            sync_tracker.last_sync_idx = sync_tracker.curr_idx - 1; // Update state for correctness.
            sync_tracker.loop_on_device_requires_sync = false;  // Clear state, sync is inserted.
        }
    }
}


void tt_epoch_loader::wait_for_epoch_complete(std::string name) {
    int target_device = get_epoch_program_info(name).target_device;
    tt_epoch_control &ctrl = *epoch_ctrl[target_device];
    wait_for_epoch_progress(ctrl, 0);
    log_debug(tt::LogLoader, "\tEpochs complete for device = {}", target_device);
}

void tt_epoch_loader::wait_for_epoch_queue_slot(tt_epoch_queue *target_queue, tt_cluster *cluster) {
    if (target_queue->is_full_dram(cluster)) {
        perf::record_scope record(perf::HostEventType::WAIT_FOR_QUEUE_READY, target_queue->d_chip_id);
        while (target_queue->is_full_dram(cluster)) {
            log_trace(tt::LogLoader, "Epoch queue at chip={} chan={} sub_ch={} addr=0x{:x} for worker/eth core {} is full (not ready for push)",
                target_queue->d_chip_id, target_queue->d_chan, target_queue->d_subchannel, target_queue->d_addr, target_queue->associated_routing_core.str());
        }
    }
}

void tt_epoch_loader::dump_epoch_binary_cache_report(const std::string &output_dir) {
    bool enable_profiler = parse_env("TT_BACKEND_BINARY_CACHE_PROFILER_EN", false);
    if (enable_profiler) {
        std::string output_report = output_dir + "/runtime_cache_report.json";
        if (fs::exists(output_report)) {
            fs::remove(output_report);
        }
        for (int device : target_devices) {
            print_binary_cache_report(output_report, epoch_ctrl.at(device) -> cache, epoch_ctrl.at(device) -> io_queue_update_caches, cluster -> get_soc_desc(device), device);
        }
    }
}

// Insert on-device sync between cores, split into smaller groups for better perf.
void tt_epoch_loader::insert_sync_on_cores(tt_epoch_control& ctrl, const std::unordered_set<tt_xy_pair>& cores_to_sync) {
    std::unordered_set<tt_xy_pair> sync_cores = cores_to_sync;
    buda_soc_description &sdesc = cluster->get_soc_desc(ctrl.associated_chip);
    if(!sync_cores.size()) {
        sync_cores = std::unordered_set<tt_xy_pair>(sdesc.workers.begin(), sdesc.workers.end());
        sync_cores.insert(sdesc.ethernet_cores.begin(), sdesc.ethernet_cores.end());
    }

    log_info(tt::LogLoader, "Inserting on-device sync on device_id: {} for {} cores.", ctrl.associated_chip, sync_cores.size());

    const int num_buffers                   = 1;
    const tt_queue_header_mask header_mask  = {tt_queue_header_mask::NULL_MASK};
    const vector<uint32_t> header_vec       = {0,0,0,0,0,0,0,0};
    tt_queue_header_wrap header_wrap        = {header_vec};

    // External sync loc. FW Sync logic requires min one buffer, so re-use 32B sync slot as target.
    std::pair<uint, uint> &sync_flag    = get_current_sync_flag_loc(ctrl.associated_chip);
    tt_xy_pair qstride_sync_loc         = sdesc.get_core_for_dram_channel(sync_flag.first, 0);
    const uint64_t update_dram_addr     = epoch_queue::get_epoch_alloc_queue_sync_addr() + (sync_flag.second * 32);
    tt_xy_pair update_dram_core         = qstride_sync_loc;
    header_wrap.val[0]                  = qstride_sync_loc.x;
    header_wrap.val[1]                  = qstride_sync_loc.y;
    header_wrap.val[2]                  = sync_flag.second;

    log_debug(tt::LogLoader, "{} -  num_sync_cores: {} sync_flag_dram_ch: {} sync_flag_slot: {} update_dram_addr: 0x{:x}",
        __FUNCTION__, sync_cores.size(), sync_flag.first, sync_flag.second, update_dram_addr);

    int sync_core_index = 0;
    for (const auto& sync_core: sync_cores) {
        const int queue_index                   = sync_core.y*ctrl.grid_shape[1] + sync_core.x;
        tt_epoch_queue* target_queue            = ctrl.get_q_ptr(queue_index);
        log_assert(target_queue->is_active(), "Can only insert sync on active worker or ethernet cores!");
        std::vector<uint32_t> update_command    = get_q_update_read_qcmd(update_dram_core, update_dram_addr, num_buffers, sync_core_index, sync_cores.size(), header_wrap, header_mask);
        wait_for_epoch_queue_slot(target_queue, cluster);
        ctrl.push_command_and_update_l1_wptr(target_queue, std::make_shared<tt_hex>(update_command, tt_hex_type::Misc, sync_core, "full-grid-sync"));
        sync_core_index++;
    }

    incr_sync_flag_loc(ctrl.associated_chip, sdesc);
    
}

// Insert on-device full grid sync. Useful to workaround various race/hazards.
void tt_epoch_loader::insert_full_grid_sync(tt_epoch_control &ctrl) {
    perf::record_scope record("insert_full_grid_sync");
    event_counters["full_grid_syncs"]++;
    insert_sync_on_cores(ctrl); // Do not specify cores to sync on -> default picks up all workers and ethernet
}


// Flush all devices WC buffers.
void tt_epoch_loader::flush_all_wc_epoch_queues_to_dram() {

    log_assert(!target_devices.empty(), "target_devices was empty");
    for (int device: target_devices) {
        tt_epoch_control* ctrl = epoch_ctrl[device];
        ctrl->flush_all_wc_epoch_queues_to_dram();
    }
}

// Enable/Disable WC per Queue. Instead of global enablement, this can en/dis for specific cases (program looping, MMIO chips)
void tt_epoch_loader::set_epoch_queues_write_combine_ena(bool enable_wc) {

    perf::record_scope record(__FUNCTION__);
    PROFILE_SCOPE(microseconds);

    std::vector<int> devices_toggled;
    log_assert(!target_devices.empty(), "target_devices was empty");
    for (int device: target_devices) {
        tt_epoch_control* ctrl = epoch_ctrl[device];

        // By default, limit to non-mmio-chips (higher latency) for now.
        if (!ctrl->epoch_queue_wc_enable_for_mmio_chips && cluster->get_cluster_desc()->is_chip_mmio_capable(device)) {
            continue;
        }

        // For safety/generality - When disabling WC, must flush remaining commands.
        if (!enable_wc) {
            ctrl->flush_all_wc_epoch_queues_to_dram();
        }

        // Determine window size to be programmed per queue, also set a per-chip state variable for avoiding wait+l1 update.
        const int wc_window_size = enable_wc ? ctrl->epoch_queue_wc_window_size_target : 0;

        // Purely for debug/info purposes.
        if (ctrl->epoch_queue_wc_window_size_target > 0) {
            devices_toggled.push_back(device);
        }

        // Lastly, set WC window size per queue, and enable/disable WC per chip.
        for (auto &q_ptr : ctrl->get_active_queues()) {
            q_ptr->set_wc_window_size(wc_window_size);
        }
        ctrl->is_epoch_queue_wc_enabled = wc_window_size > 0;
    }

    if (devices_toggled.size() > 0) {
        log_info(tt::LogLoader, "{} Epoch Cmd Queue Write-Combine for {} devices ({}).", enable_wc ? "Enabled" : "Disabled", devices_toggled.size(), devices_toggled);
    }

}


// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
std::ostream &operator<<(std::ostream &os, tt_epoch_program_info const &epoch) {
    os << "Epoch GraphName = " << epoch.name << ", CompileEpochId = " << epoch.epoch_id << ", TargetDevice = " << epoch.target_device << std::endl;
    return os;
}
