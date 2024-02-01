// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <mutex>
#include "model/model.hpp"
#include "common/cache_lib.hpp"
#include "tt_cluster.hpp"
#include "common/tt_queue_ptr.hpp"
#include "device/device_api.h"
#include "tt_hexfile.h"
#include "netlist/netlist_info_types.hpp"
#include "netlist/tt_digraph.hpp"
#include "runtime/runtime_types.hpp"
#include "epoch_q.h"
#include "epoch_utils.hpp"
#include "runtime/runtime_workload.hpp"

class tt_epoch_queue;

static constexpr bool varinst_cmd_debug = false; // Gaurd some more costlier debug/perf-analytics code

struct tt_epoch_id_aliasing_sync_tracker {
    int64_t curr_idx = 0;
    int64_t last_sync_idx = -1; // Most recent idx synced on.
    std::unordered_map<int64_t, int64_t> wrapped_epoch_id_to_last_idx_map;
    std::unordered_map<int64_t, int64_t> wrapped_epoch_id_to_last_epoch_id_map;

    // For program looping + epoch aliasing hazard avoidance
    bool loop_on_device_requires_sync = false;

};

/**
 * DRAM memory manager
 * 
 * Contains per-chip per-dram-channel allocator for device DRAM.
 * It is used to allocate space for runtime binaries such as fw, blob, config
 */
struct tt_epoch_dram_manager
{
    private:
    tt_cluster* cluster;

    // Per-chip, per-channel allocation structure
    //std::map<chip_id_t, std::vector<tt_chan_alloc_struct>> chan_struct_map;
    vector <tt_chan_alloc_struct> chan_struct_vec;
    map<int, vector<tt_xy_pair>> dram_channel_to_worker;

    public:

    static constexpr int QUEUE_HEADER_SIZE_BYTES = static_cast<uint32_t>(sizeof(tt_queue_header));

    tt_epoch_dram_manager(chip_id_t chip, const buda_soc_description &sdesc);

    void allocate_epoch_queue(tt_epoch_queue* queue, bool distribute_tables);

    static uint64_t get_top_of_binaries(int t6_cores_per_chan, int eth_cores_per_chan);
    static uint64_t get_bin_size_q_slot(int t6_cores_per_chan, int eth_cores_per_chan);
    static uint64_t get_top_of_q_update_blobs(int t6_cores_per_chan, int eth_cores_per_chan);
    static uint64_t get_top_of_kernel_cache(int t6_cores_per_chan, int eth_cores_per_chan);
    static uint64_t get_top_of_epoch0_start_table();

    // Get dram chan based on physical core location, corresponds to hardcoding in risc_chip_specific.cc
    static std::tuple<int, int> get_dram_chan(
        tt::ARCH arch, tt_xy_pair physical_core, const std::unordered_map<tt_xy_pair, std::tuple<int, int>> &dram_chan_map);

    // Get number of cores that is mapped to each dram chan/subchan
    static std::vector<uint16_t> get_num_cores_per_dram_channel(
        tt::ARCH arch, const std::vector<tt_xy_pair> &cores, const std::unordered_map<tt_xy_pair, std::tuple<int, int>> &dram_chan_map, int dram_chan_cnt);

    vector<tt_chan_alloc_struct>& get_dram_allocators() {
        //log_assert(chan_struct_map.count(chip_id)>0, "chip_id not in chan_struct_map");
        return chan_struct_vec;
    }
    uint get_worker_idx_in_channel(tt_xy_pair worker, int dram_channel);
    chip_id_t associated_chip;
    buda_soc_description sdesc;
};

/**
 * Epoch binary cache class
 * 
 * This is derived from the tt_recency_cache class and can be programmed to use an LRU replacement policy or approximate MRU
 * Additional functionality added on top of basic recency cache:
 *      full() : Return if the cache is full or not
 *      policy_based_binary_slot(): Return the cache list slot, at an offset from the most recently used for MRU. Return the LRU slot for LRU
 *                                 This slot can be used by the new binary that gets placed in the cache, if no commands are using the binary at this slot.
 *      policy_based_binary_name(): Return the name of the binary at the slot computed with the replacement policy and offset
 *      get_slot_for_binary(): Return true cache hit status. Place the binary in a slot determined by the policy and return the slot by reference
 *      clear_pinned_binaries() : Clears the set of pinned binary names. Pinnned binaries are those that cannot be evicted.
 */

class tt_epoch_binary_cache: public tt_recency_cache
{
    public:
    bool full();
    bool use_lru;
    std::string cache_name;
    unordered_set<std::string> pinned_binary_names;
    int policy_based_binary_slot(uint32_t offset);
    string policy_based_binary_name(uint32_t offset);
    bool get_slot_for_binary(const std::string& binary_name, std::unordered_map<std::string, int>& num_cmds_per_bin, tt_queue_ptr& binary_q_ptr, int& slot,
        std::vector<tt_epoch_queue*>& command_qs, tt_cluster* cluster, bool binary_preload, bool pin_binary, bool io_queue_update_cmd = false);
    void clear_pinned_binaries();
    tt_epoch_binary_cache(std::string name, uint32_t capacity, bool enable_mru_with_backtrace_bin_cache);
};

/**
 * Epoch binary class
 * 
 * Binary hexes for all cores in an epoch, which implies a single chip (epoch does not span chips).
 * All the binaries with the exception of runtime config are loaded from disk, and assigned with dram
 * memory by the runtime, finally loaded onto device by the loader. This can be done ahead-of-time or on-the-fly
 */
class tt_epoch_binary
{
    public:
    int chip_id;

    // Statically Loaded FW
    inline static vector <uint32_t> ncrisc_vec = {};
    inline static vector <uint32_t> brisc_vec = {};
    inline static vector <uint32_t> erisc_vec = {};
    inline static vector<uint32_t> erisc_iram_vec = {};
    inline static vector <uint32_t> blob_bin_init = {};
    // Use this mutex when initializing static binaries 
    // since these are shared resources across threads
    inline static std::mutex static_binary_mutex;
    // FW loaded during runtime
    vector <tt_hex> trisc0_bin_vec;
    vector <tt_hex> trisc1_bin_vec;
    vector <tt_hex> trisc2_bin_vec;
    vector <tt_hex> blob_bin_vec;
    vector <tt_hex> runtime_config_vec;
    vector <tt_hex> ethernet_blob_bin_vec;
    vector <tt_hex> ethernet_runtime_config_vec;

    vector<std::shared_ptr<tt_hex>> tensix_hex_vec;
    vector<std::shared_ptr<tt_hex>> eth_hex_vec;

    // For kernel cache, identify which globally unique op trisc binaries belong to.
    vector<string> op_base_path_vec;
    inline static std::string SKIP_KERNELS_FLAG(){ return "";}

    vector <uint32_t> get_risc_binary(string path, uint32_t id);
    vector <uint32_t> get_trisc_binary(string path, uint32_t id);
    vector <uint32_t> get_overlay_binary(string path);
    vector <uint32_t> get_empty_trisc_binary();
    vector <uint32_t> get_empty_overlay_binary();
    vector <uint32_t> get_empty_ethernet_overlay_binary();
    vector <uint32_t> get_runtime_config_binary(const tt_op_info &op_info);
    vector <uint32_t> get_empty_runtime_config_binary();

    static void create_build_dir(const std::string &output_dir);

    tt_epoch_binary(const int chip_id) : chip_id(chip_id) {};
    ~tt_epoch_binary() {};

    int number_of_tensix_hex_images();
    int number_of_ethernet_hex_images();
    tt_xy_pair get_logical_coord(int hex_id);
    tt_xy_pair get_routing_coord(int hex_id);

    void assign_tensix_binaries_to_dram(int hex_id, int dram_channel, int dram_subchannel, uint64_t dram_start_addr, uint64_t dram_start_addr_kernels);
    void assign_ethernet_binaries_to_dram(int hex_id, int dram_channel, int dram_subchannel, uint64_t dram_start_addr);

    void get_epoch_binaries(const std::string &output_dir, const std::string &graph_name, const map<string, tt_op_info> &op_map, const buda_soc_description& sdesc);
    void update_overlay_binary(const std::string &output_dir, const std::string &graph_name, int temporal_epoch, int chip_id, const buda_soc_description *sdesc, tt_cluster* cluster);
};

/**
 * Epoch program info
 */
struct tt_epoch_program_info
{
    std::shared_ptr<tt_epoch_binary> binary;
    std::string name = "";       // graph used to generate this epoch
    uint32_t epoch_id = 0;       // compiled temporal epoch id
    uint32_t target_device = 0;  // target chip id in a cluster
    bool device_perf_trace_en = false;
    std::unordered_map<tt_xy_pair, uint16_t> overlay_decouple_mask;

    inline const uint16_t get_overlay_decouple_mask(const tt_xy_pair &core_coord) const {
        if (overlay_decouple_mask.find(core_coord) == overlay_decouple_mask.end()) {
            return 0;
        } else {
            return overlay_decouple_mask.at(core_coord);
        }
    }
};

/**
 * Epoch queue
 */
class tt_epoch_queue : public tt_hex
{
    deque <std::shared_ptr<tt_hex>> hexq;
    std::vector<std::string> binaries_in_use; // contains a hex name for each command sent to this epoch queue.
                                              // used to track the number of active (still in use) epoch binaries 
                                              // with EpochValid CMDs per device

    std::vector<int> epoch_ids_in_queue; // Same size as epoch command queue. Tracks the epoch_id's being consumed by each CMD
    int slots;              // number of slots
    int slot_size;          // size in bytes
    bool mmio_chip;         // is this on a mmio mapped chip. Different syncing requirements.
    int wc_window_size = 0; // write combine feature window size in num entries. 0=Dis
    bool flushed_cmds = false;
    
    unordered_map<std::string, int>* num_cmds_per_binary_ptr;
    unordered_map<int, int>* num_cmds_per_epoch_id_ptr;
    unordered_map<std::string, int>* num_cmds_per_q_update_blob_ptr;
    uint64_t shadow_l1_addr = 0;
    bool disable_shadow_l1_wrptr = false;

    void push_to_dram(tt_cluster *cluster, std::shared_ptr<tt_hex> hex_ptr = nullptr, bool send_wrptr_update = true, bool last_send_epoch_cmd = true);

    public:
    std::vector<std::string> external_blobs_in_use;
    tt_queue_ptr dram_ptrs;
    uint32_t num_valid_cmds = 0;
    int max_offset_between_epoch_ids = 31; // 5 bits for epoch_id with 0x1f representing dummy phase -> Cannot send commands for a new epoch_id if:
                                           // new_epoch_id % max_offset_between_epoch_ids == active_epoch_id % max_offset_between_epoch_ids and (active_epoch_id != new_epoch_id)
                                           // See stall_active_cmd_for_epoch() and wait_for_epoch_queues_ready()
    bool valid_cmd_sent = false;
    int wr_ptr_device_loop_start_cmd = -1;  // For checking that loop-on-device cmds fit in epoch queue
    int device_loop_stack_depth = 0;
    tt_epoch_queue();

    tt_epoch_queue(int islots, int slot_size, tt_xy_pair core, bool immio_chip, unordered_map<std::string, int>* num_cmds_per_binary, unordered_map<int, int>* num_cmds_per_epoch_id, unordered_map<std::string, int>* num_cmds_per_q_update_blob);
    tt_epoch_queue(const tt_epoch_queue &queue);

    int size_bytes();
    bool is_active();

    // Wrapper
    void push_command(std::shared_ptr<tt_hex> hex_ptr, tt_cluster *cluster, bool last_send_epoch_cmd = true);
    void wait_for_free_space(tt_cluster* cluster, int num_slots);

    void init_queue_ptrs(tt_cluster *cluster);
    void init_queue_ptrs_l1(tt_cluster *cluster);
    void update_write_ptr_in_dram(tt_cluster *cluster, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    void update_write_ptr_in_l1(tt_cluster *cluster, bool last_send_epoch_cmd = true);

    void update_read_ptr(tt_cluster *cluster);
    bool is_full_dram(tt_cluster *cluster);
    bool is_empty_dram(tt_cluster *cluster);
    uint32_t get_rd_ptr_dram(tt_cluster *cluster);
    uint32_t get_wr_ptr();
    int occupancy();
    int occupancy_dram(tt_cluster *cluster);
    int free_space_dram(tt_cluster *cluster);

    void push(std::shared_ptr<tt_hex> hex_ptr);
    void pop(int amt=1);

    void flush_cmds(tt_cluster* cluster, bool last_send_epoch_cmd = true);
    void flush_wrptr(tt_cluster* cluster, bool ordered_write = true, bool last_send_epoch_cmd = true);
    int get_num_slots();

    void assign_epoch_info_to_queue_slot(const tt_epoch_program_info& info);
    void update_num_commands_per_binary(tt_cluster* cluster, bool io_queue_update_cmd);
    bool is_totally_empty();
    std::shared_ptr<tt_hex> get_hex_ptr();
    tt_xy_pair get_associated_routing_core();
    tt_cxy_pair get_chip_and_associated_routing_core();
    void wait_for_ncrisc_init_queue(tt_cluster *cluster, int timeout_in_seconds);

    void set_l1_shadow_addr(uint64_t addr){
        shadow_l1_addr = addr;
    }

    void set_disable_shadow_l1_wrptr(bool dis) {
        disable_shadow_l1_wrptr = dis;
    }

    bool is_shadow_l1_wrptr_enabled() {
        return !disable_shadow_l1_wrptr;
    }

    void set_wc_window_size(int size){
        log_assert(size <= slots && size >= 0, "WC Buffer size must be positive and less than the number of slots in the Epoch Cmd Queue.");
        wc_window_size = size;
    }

    ~tt_epoch_queue();
};

/**
 * Epoch control
 * 
 * State keeping structure for each chip to manage it's epoch execution graph
 * The execution may involve loops and jumps, as specified by the netlist program
 */
struct tt_epoch_control
{
    private:
    tt_cluster *cluster;
    int curr_gen_id;
    int sync_gen_id;
    unordered_map<string, tt_queue_header> cached_io_qs;
    unordered_set<string> in_use_io_qs;
    vector<tt_epoch_queue*> active_hexqs;
    vector<tt_epoch_queue*> active_hexqs_wth_shadow_l1_wrptr;
    bool mmio_chip;

    void update_sync_gen_id(int gen_id);

    public:
    vector<tt_epoch_queue *> hexqs;
    unordered_map<std::string, int> num_cmds_per_binary = {};
    unordered_map<int, int> num_cmds_per_epoch_id = {};
    vector<unordered_map<string, int>> num_cmds_per_q_update_blob = {};
    tt_queue_ptr bin_q_ptrs;
    int associated_chip;
    tt_grid_shape grid_shape;

    tt_epoch_binary_cache cache;
    vector<tt_epoch_binary_cache> io_queue_update_caches;
    vector<tt_queue_ptr> io_q_update_ptrs;

    // Kernel cache : split unique kernels out of combined epoch binaries.
    std::unordered_map<std::string, uint32_t> op_path_to_unique_op_idx;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, bool>> unique_op_idx_per_ch_binary_preloaded;

    // For WC, skip flush, waits, updates that are handled by flush-to-DRAM later.
    bool is_epoch_queue_wc_enabled = false;
    bool epoch_queue_wc_enable_for_mmio_chips = false;
    int epoch_queue_wc_window_size_target = 0;

    tt_epoch_control(tt_cluster *cluster, int chip_id);
    ~tt_epoch_control();

    void incr_wr_ptr();
    vector<tt_epoch_queue *> *get_hexqs_ptr();
    void init_epoch_ctrl(int cmd_slots, int cmd_size, int bin_slots, bool enable_mru_with_backtrace_bin_cache);

    int occupancy();
    bool all_empty();
    bool all_not_empty();
    bool all_not_full_dram();
    bool check_epoch_done();
    void update_rd_ptr_for_queues_with_valid_cmds();
    void wait_for_cmds_remaining(int num_cmds);
    bool has_epoch_id_alias_hazard_with_device(tt_epoch_id_aliasing_sync_tracker& sync_tracker, const int& wrapped_epoch_id, const int& epoch_id, const string& epoch_name, bool avoid_via_full_grid_sync);
    void push_command(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> cmd_hex, bool last_send_epoch_cmd = true);
    // void push_cmd_from_host_q_to_dram(tt_epoch_queue* q_ptr);
    void push_command_and_update_l1_wptr(tt_epoch_queue* q_ptr, std::shared_ptr<tt_hex> cmd_hex);
    void update_all_dram_wrptrs(vector<tt_epoch_queue*> active_occupied_queues);
    void update_all_shadow_l1_wrptrs();
    void push_command_to_all_workers_and_update_l1_wptr(const vector<uint32_t> &qcmd);
    
    // Write-Combined Epoch Queue
    void flush_all_wc_epoch_queues_to_dram();

    //void push_qcmd(const vector<uint32_t> &qcmd, const unordered_set<tt_epoch_queue*> &excluded);
    void update_curr_gen_id(int gen_id);
    std::shared_ptr<tt_hex> get_hex_ptr(int index);
    tt_epoch_queue *get_q_ptr(int index);
    vector<tt_epoch_queue*> &get_active_queues();
    vector<tt_epoch_queue *> &get_active_queues_with_shadow_l1_wrptr();

    void record_wr_ptr_device_loop_start_cmds();
    void clear_wr_ptr_device_loop_start_cmds(std::string prog_name);

    bool is_epoch_in_progress();

    int get_next_gen_id();
    int get_curr_gen_id();
    int get_sync_gen_id();

    static vector<uint32_t> get_valid_qcmd(tt_xy_pair dram_xy, uint64_t combined_binary_dram_addr, uint64_t trisc_binary_dram_addr, bool perf_en, uint16_t overlay_decouple_mask);
    static vector<uint32_t> get_invalid_qcmd();
    static vector<uint32_t> get_endprog_qcmd();

    bool get_slot_in_binary_cache(const tt_epoch_program_info& info, int& epoch_q_slot);
    static vector<uint32_t> get_loopstart_qcmd(uint32_t num_loops);
    static vector<uint32_t> get_loopend_qcmd();

    // For reading and writing to cache, automatically resolves queue aliasing.
    tt_queue_header& get_cached_io_qs(const tt_queue_info &queue_info); // Not const, user can modify fields.
    void set_cached_io_qs_wrap(const tt_queue_info &queue_info, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask);

    // For resource (queue) usage detection, automatically resolves queue aliasing.
    bool is_queue_in_use(const tt_queue_info &queue_info);
    void set_queue_in_use(const tt_queue_info &queue_info);
    void clear_all_queues_in_use();

};

struct tt_epoch_loader_state {
    // Cache of sync cores to avoid repeated lookups on the same alloc/dealloc/update commands (used for inline updates)
    std::unordered_map<string, std::unordered_set<tt_xy_pair>> sync_cores_cache;

    // Cache for on device queue allocation/updates (used for updates requiring external binaries)
    // Stores a tuple of std::vector<std::unordered_set<tt_xy_pair>> (sync cores per buffer group), std::vector<tt_hex> (update binaries per buffer group)
    // and std::vector<uint32_t> (num buffers in group)
    std::unordered_map<string, std::tuple<std::vector<std::unordered_set<tt_xy_pair>>, std::vector<tt_hex>, std::vector<uint32_t>>> external_binary_cache;
    
    // Tracking previous sync flag to allow the user to use non-overlapping locations
    std::map<uint, pair<uint, uint>> allocate_queue_sync_flag_tracker;
    // Tracking when syncs have occurred per chip to avoid epoch-id-aliasing hazards efficiently.
    std::map<uint, tt_epoch_id_aliasing_sync_tracker> epoch_alias_sync_tracker;
    // Make it easier for loader to be aware when runtime is within program loop on device.
    bool in_loop_on_device = false;
    
};

/**
 * Epoch loader
 * 
 * Provides epoch-to-device level APIs to the Buda runtime
 */
class tt_epoch_loader
{
    private:
    tt_cluster *cluster;
    std::string output_dir;
    std::mutex epoch_ctrl_mutex; // used to guard multi-threaded epoch_ctrl use
    tt_epoch_loader_state state;

    // Get these values from epoch_q header during init, to avoid recomputing
    uint64_t epoch_alloc_queue_sync_addr = epoch_queue::get_epoch_alloc_queue_sync_addr();
    bool kernel_cache_enabled = 0;

    public:
    int epoch_window_size;
    unordered_map<int, tt_epoch_control*> epoch_ctrl; // device_id-to-epoch map
    unordered_map<int, tt_epoch_dram_manager*> dram_mgr;  
    unordered_map<std::string, tt_epoch_program_info> graph_to_epoch_map;
    unordered_map<queue_buffer_info, std::unordered_set<tt_xy_pair>> queue_buffer_to_consumers_map;
    unordered_map<queue_buffer_info, set<tt_xy_pair>> queue_buffer_to_producers_map;
    unordered_map<string, set<string>> graph_name_to_queue_decouplings;
    set<int> target_devices; // all devices mapped to current workload
    unordered_map<std::string, int> event_counters;

    // Default to set_optimization_level(0)
    bool check_binaries = false;  // cannot reliably check due to PCIe RO reads issue #369
    bool enable_epoch_caching = false;
    bool enable_optimized_barriers = false;
    bool enable_epoch_preloading = false;
    bool enable_hw_io_queue_update = false;
    bool enable_queues_only_sync = false;
    bool enable_queue_settings_reuse = false;
    bool enable_mru_with_backtrace_bin_cache = false;
    bool enable_looping_on_device = false;
    bool enable_varinst_merge_io_queue_updates = false;
    bool disable_eq_shadow_l1_wrptrs = false;
    bool enable_runtime_hazard_checks = false;
    bool enable_write_combine_epoch_cmds = false;
    bool skip_io_init = false;
    bool skip_device_init = false;
    bool sent_end_program = false;

    unordered_map<chip_id_t ,std::unique_ptr<tt_epoch_dram_manager>> dram_mgr_per_chip;

    tt_epoch_loader(tt_cluster *cluster, string output_dir, std::set<int> target_device_ids);
    ~tt_epoch_loader();

    void finish_loader_profiler();

    //! epoch control
    tt_epoch_control& get_epoch_ctrl(const int device_id);
    void create_and_allocate_epoch_queues(bool distribute_tables);
    void set_epoch_queues_write_combine_ena(bool enable_wc = true);
    void flush_all_wc_epoch_queues_to_dram();
    void send_epoch_commands(const tt_epoch_program_info &info);
    void send_end_program_commands();
    void send_loop_start_command(uint64_t num_loops);
    void send_loop_end_command(std::string prog_name);
    void set_in_loop_on_device(bool in_loop_on_device);
    bool get_in_loop_on_device();

    //! varinst-on-device
    void update_io_queue_rdptrs_varinst_on_device(const tt_runtime_queue_ptrs_wrap &qptrs_wrap, const tt_runtime_workload &workload, const unordered_map<string, int> &vars, std::string program_name, int num_iterations);
    void check_io_queue_rdptrs_varinst_on_device(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings, const unordered_map<string, int> &vars,
        const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams, const std::string &check_name);
    void send_epoch_cmd_varinst(const epoch_queue::VarinstCmdInfo &cmd, int device_id, const tt_xy_pair &reader);

    void wait_for_ncrisc_init_all_epoch_queues(bool enable_timeout = true);

    void wait_for_epoch_complete(std::string name);
    void wait_for_epoch_progress(tt_epoch_control &ctrl, int cmds_remaining);
    void wait_for_epoch_queues_ready(tt_epoch_control &ctrl, tt_epoch_program_info& info);
    void wait_for_epoch_queue_slot(tt_epoch_queue *target_queue, tt_cluster *cluster);
    bool check_resource_overlap(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings, const std::unordered_map<string, int> &vars);
    void avoid_loop_on_device_epoch_id_aliasing();
    tt_epoch_id_aliasing_sync_tracker& get_epoch_id_aliasing_sync_tracker(uint target_device);

    //! epoch programs
    void insert_epoch_program(tt_epoch_program_info &&epoch);
    void send_epoch_program(std::string name, bool epoch_binary_preload);
    tt_epoch_program_info& get_epoch_program_info(std::string name);

    //! epoch binaries
    bool lay_out_binaries(const tt_epoch_program_info &info, bool epoch_binary_preload);
    void send_epoch_binaries(const tt_epoch_program_info &info);
    void send_static_binaries();
    void load_and_send_padding_constants();

    static vector<uint32_t> get_q_header_binaries(const tt_queue_allocation_info &alloc_info, const tt_xy_pair &core, const buda_soc_description &soc_descriptor);
    static vector<uint32_t> get_q_update_read_binaries(const tt_queue_info &queue_info, const buda_soc_description &soc_descriptor);
    static vector<uint32_t> get_buffer_update_read_binaries(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, const map<string, tt_queue_wrap> &queues, const buda_soc_description &sdesc);
    //! epoch io
    void create_and_allocate_io_queues(const map<string, tt_queue_wrap> &queues);
    void send_allocate_queue_commands(const map<string, tt_queue_wrap> &queues, const unordered_set<string> &queues_to_dealloc, const bool wait_for_eq = true);
    pair<uint, uint>& get_current_sync_flag_loc(uint target_device);
    void incr_sync_flag_loc(uint target_device, buda_soc_description &sdesc);

    void write_io_queue_header(vector<uint32_t> &vec, uint32_t offset, const tt_queue_info &queue_info);

    void update_io_queue_settings(const map<string, tt_queue_wrap> &queues, const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams, const vector<tt_queue_setting_info> &queue_settings, const std::unordered_map<string, int> &vars, const tt_queue_header_mask &header_mask);
    void update_io_queue_setting_from_device(const tt_queue_info &queue_info, bool is_dual_view_ram, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask, const map<string, tt_queue_wrap> &queues);
    void update_io_queue_setting_from_host(const tt_queue_info &queue_info, const tt_queue_header_wrap &header_wrap, const tt_queue_header_mask &header_mask);
    void mark_io_queues_in_use(const map<string, tt_queue_wrap> &queues, const vector<tt_queue_setting_info> &queue_settings);
    void populate_queue_to_core_map_from_net2pipe(const std::unordered_map<std::string, dual_view_ram_info_t> &dual_view_rams, string build_dir_path, int num_temporal_epochs, bool queue_to_producer);
    void populate_dram_decouple_config_map(const netlist_workload_data &workload);
    void write_dram_decouplings_to_queue_headers(const netlist_workload_data &workload, string graph_name, bool reset);
    void get_unique_producer_cores_from_buffers(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set= false);
    void get_unique_consumer_cores_from_buffers(const std::vector<std::tuple<std::string, tt_queue_allocation_info>>& buffers, std::unordered_set<tt_xy_pair>& all_readers, bool allow_empty_set = false);
    void get_unique_consumer_cores_from_queues(const unordered_set<string> &queues, std::unordered_set<tt_xy_pair>& all_readers, bool allow_empyt_set = false);
    void get_unique_producer_cores_from_queues(const unordered_set<string> &queues, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set = false);
    void get_unique_consumer_cores_from_queue_buffer(const string& queue_name, const tt_queue_allocation_info& alloc_info, uint target_device, std::unordered_set<tt_xy_pair>& all_readers, bool allow_empty_set = false);
    void get_unique_producer_cores_from_queue_buffer(const string& queue_name, const tt_queue_allocation_info& alloc_info, std::unordered_set<tt_xy_pair>& all_writers, bool allow_empty_set = false);
    void preload_epoch_queues(const map<string, tt_digraph> &graphs, const std::vector<string> &graph_order);
    void get_unique_epoch_trisc_binaries(const map<string, tt_digraph> &graphs, const std::vector<string> &graph_order);
    tt_queue_header_wrap get_queue_header_wrap(tt_queue_info queue_info, tt_queue_setting_info queue_setting, const unordered_map<string, int> &vars);
    void insert_sync_on_cores(tt_epoch_control& ctrl, const std::unordered_set<tt_xy_pair>& cores_to_sync = {});
    void insert_full_grid_sync(tt_epoch_control &ctrl);

    //! misc
    void dump_epoch_binary_cache_report(const std::string &output_dir);
    void set_optimization_level(int level);


    private:

    std::vector<tt_varinst_queue_update_cmd_info> varinst_cmd_info_list_generate_from_instrns(const tt_runtime_queue_ptrs_wrap &qptrs_wrap, const netlist_workload_data &workload, 
        const unordered_map<string, int> &vars, std::string program_name, int num_iterations);
    void generate_allocate_queue_binaries(const map<string, tt_queue_wrap> &queues, const unordered_set<string> &queues_to_dealloc, const buda_soc_description& sdesc, std::vector<std::unordered_set<tt_xy_pair>>& sync_cores_per_group, std::vector<tt_hex>& external_binaries_per_group, std::vector<uint32_t>& num_buffers_per_group, std::string& key);
    void generate_queue_update_external_binaries(const std::set<std::string>& queue_names, std::string& cache_key, const map<string, tt_queue_wrap> &queues, const tt_queue_settings_sync_type &sync_type, const buda_soc_description& sdesc, std::vector<std::unordered_set<tt_xy_pair>>& sync_cores_per_group, std::vector<tt_hex>& external_binaries_per_group, std::vector<uint32_t>& num_buffers_per_group, bool loop_on_device);
    void varinst_cmd_info_list_merge_commutative(std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos);
    void varinst_cmd_info_list_merge_local_global(std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos);
    void generate_and_send_varinst_cmds_from_cmd_info_list(const std::vector<tt_varinst_queue_update_cmd_info> &varinst_cmd_infos, const tt_runtime_workload &workload);
    void generate_and_send_varinst_cmds_inline(const tt_varinst_queue_update_cmd_info &info, const tt_runtime_workload &workload, std::unordered_set<std::string> queue_names);
    void generate_and_send_varinst_cmds_external(const tt_varinst_queue_update_cmd_info &info, const tt_runtime_workload &workload, const std::set<std::string>& queue_names);
    void update_qs_cache_for_epoch_cmd_varinst(uint16_t opcode, uint32_t operand_0, uint32_t operand_1, tt_queue_header_field update_type, const tt_queue_info &queue_info, int num_iterations);
};

std::ostream &operator<<(std::ostream &os, tt_epoch_program_info const &epoch);
