// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <chrono>
#include "device/device_api.h"
#include "common/model/test_common.hpp"
#include "perf_lib/postprocess.hpp"
#include "host_mem_address_map.h"
#include "l1_address_map.h"
#include "dram_address_map.h"
#include "eth_interface.h"
#include "eth_l1_address_map.h"

using buda_soc_description = buda_SocDescriptor;
using tt_cluster_description = tt_ClusterDescriptor;
using std::chrono::high_resolution_clock;

using tt_target_dram = std::tuple<int, int, int>;
extern std::string cluster_desc_path;

struct tt_cluster
{
    friend class DebudaIFC; // Allowing access for Debuda
    private:
    std::shared_ptr<tt_device> device;
    std::unordered_map<chip_id_t, buda_soc_description> sdesc_per_chip;
    high_resolution_clock::time_point device_reset_time;
    std::set<chip_id_t> target_device_ids;
    std::vector<std::thread> background_threads;
    enum class tt_cluster_state {
        Idle = 0,
        Running = 1,
        PendingTerminate = 2,
    };

    tt_cluster_state state;

    tt_device_l1_address_params l1_fw_params = {l1_mem::address_map::NCRISC_FIRMWARE_BASE, l1_mem::address_map::FIRMWARE_BASE,
                                                l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE,
                                                l1_mem::address_map::TRISC_BASE, l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 
                                                eth_l1_mem::address_map::FW_VERSION_ADDR};

    tt_device_dram_address_params dram_fw_params = {dram_mem::address_map::DRAM_BARRIER_BASE};

    tt_driver_host_address_params host_address_params = {host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, host_mem::address_map::ETH_ROUTING_BUFFERS_START};

    tt_driver_eth_interface_params eth_interface_params = {NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS, ETH_RACK_COORD_WIDTH, CMD_BUF_SIZE_MASK, MAX_BLOCK_SIZE,
                                    REQUEST_CMD_QUEUE_BASE, RESPONSE_CMD_QUEUE_BASE, CMD_COUNTERS_SIZE_BYTES, REMOTE_UPDATE_PTR_SIZE_BYTES,
                                    CMD_DATA_BLOCK, CMD_WR_REQ, CMD_WR_ACK, CMD_RD_REQ, CMD_RD_DATA, CMD_BUF_SIZE, CMD_DATA_BLOCK_DRAM, ETH_ROUTING_DATA_BUFFER_ADDR,
                                    REQUEST_ROUTING_CMD_QUEUE_BASE, RESPONSE_ROUTING_CMD_QUEUE_BASE, CMD_BUF_PTR_MASK, CMD_ORDERED, CMD_BROADCAST};
    
    // Broadcast Grid Metadata for GS and WH
    std::set<uint32_t> rows_to_exclude_for_grayskull_tensix_broadcast = {0, 6};
    std::set<uint32_t> cols_to_exclude_for_grayskull_tensix_broadcast = {0};
    std::set<uint32_t> rows_to_exclude_for_wormhole_tensix_broadcast = {0, 6};
    std::set<uint32_t> cols_to_exclude_for_wormhole_tensix_broadcast = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_wormhole_eth_broadcast = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
    std::set<uint32_t> cols_to_exclude_for_wormhole_eth_broadcast = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_grayskull_dram_broadcast = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
    std::set<uint32_t> cols_to_exclude_for_grayskull_dram_broadcast = {0, 2, 3, 5, 6, 8, 9, 11, 12};
    std::set<uint32_t> cols_to_exclude_for_wormhole_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    std::chrono::seconds get_device_timeout();
    std::chrono::seconds get_device_duration();
    void monitor_device_timeout();
    void generate_soc_descriptors_and_get_harvesting_info(const tt::ARCH& arch, const std::string& output_dir);
   public:
    tt::ARCH cluster_arch = tt::ARCH::Invalid;
    TargetDevice type;
    postprocess::PerfState perf_state;
    bool deasserted_risc_reset;
    bool performed_harvesting = false;
    std::unordered_map<chip_id_t, uint32_t> harvested_rows_per_target = {};
    bool noc_translation_en = false;

    tt_cluster() :
        device(nullptr), state(tt_cluster_state::Idle), type(TargetDevice::Invalid), deasserted_risc_reset(false){};

    int get_num_chips();
    std::unordered_set<chip_id_t> get_all_chips();

    inline bool is_idle() const {return state == tt_cluster_state::Idle;}
    buda_soc_description& get_soc_desc(chip_id_t chip){return sdesc_per_chip.at(chip);}
    inline const std::unordered_map<chip_id_t, buda_soc_description> get_sdesc_for_all_devices() const {return sdesc_per_chip;}
    inline const std::shared_ptr<tt_device> get_device() const {return device;};

    tt_cluster_description *get_cluster_desc() { return device -> get_cluster_description(); }

    tt_xy_pair get_routing_coordinate(int core_r, int core_c, chip_id_t device_id) const;
    map<tt_cxy_pair, vector<uint32_t>> get_perf_events();

    //! device driver and misc apis
    static std::string get_cluster_desc_path(const std::string& root);
    static std::vector<tt::ARCH> detect_available_devices(const TargetDevice &target_type, bool only_detect_mmio = true);

    void open_device(
        const tt::ARCH &arch,
        const TargetDevice &target_type,
        const std::set<int> &target_devices,
        const std::string &sdesc_path = "",
        const std::string &ndesc_path = "",
        const bool& perform_harvesting = true,
        const bool &skip_driver_allocs = false,
        const bool &clean_system_resources = false,
        const std::string& output_dir = "",
        const std::unordered_map<chip_id_t, uint32_t>& harvesting_masks = {});

    void start_device(const tt_device_params &device_params);
    void close_device();
    void record_device_runtime_start(int device_id);
    void deassert_risc_reset();
    void memory_barrier(tt::MemBarType barrier_type, const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores = {});
    void memory_barrier(tt::MemBarType barrier_type, const chip_id_t chip, const std::unordered_set<uint32_t>& target_channels);
    void wait_for_completion(std::string output_dir);
    void dump_debug_mailbox(std::string output_dir);
    void dump_perf_counters(std::string output_dir);
    map<uint, pair<uint64_t, uint64_t>> get_elapsed_time(std::string output_dir);
    vector<tt::EpochPerfInfo> get_perf_info_all_epochs();
    void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
    tt_version get_ethernet_fw_version();
    //! read/write access
    void send_hex_to_dram(tt_hex *hex, bool small_access = false, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true);
    void read_hex_from_dram(tt_hex *hex);
    void check_hex_from_dram(tt_hex *hex, std::string name = "");
    void send_hex_to_core(tt_hex *hex, tt_cxy_pair core);
    void read_hex_from_core(tt_hex *hex, tt_cxy_pair core);
    void wait_for_non_mmio_flush();
    inline tt_cxy_pair get_dram_core(const tt_target_dram &dram);
    void write_dram_vec(const void* ptr, const uint32_t size, tt_cxy_pair dram_core, uint64_t addr,  bool small_access = false, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool register_txn = false, bool ordered_with_prev_remote_write = false);
    void write_dram_vec(const void* ptr, const uint32_t size, const tt_target_dram &dram, uint64_t addr,  bool small_access = false, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool register_txn = false, bool ordered_with_prev_remote_write = false);
    void write_rolled_dram_vec(vector<uint32_t> &vec, uint32_t unroll_count, const tt_target_dram &dram, uint64_t addr, bool small_access = false, bool register_txn = false);
    void write_dram_vec(vector<uint32_t> &vec, const tt_target_dram &dram, uint64_t addr, bool small_access = false, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool register_txn = false, bool ordered_with_prev_remote_write = false);
    void read_dram_vec(vector<uint32_t> &vec, const tt_target_dram &dram, uint64_t addr, uint32_t size, bool small_access = false, bool register_txn = false);
    void broadcast_write_to_all_tensix_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access = false, bool register_txn = false);
    void broadcast_write_to_all_eth_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access = false, bool register_txn = false);
    void broadcast_write_to_all_dram_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access = false, bool register_txn = false);
    void broadcast_write_to_cluster(const vector<uint32_t>& vec, uint64_t addr, std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, bool small_access = false, bool register_txn = false);
    void write_rolled_dram_vec(vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair dram_core, uint64_t addr, bool small_access = false, bool register_txn = false);
    void write_dram_vec(vector<uint32_t> &vec, tt_cxy_pair dram_core, uint64_t addr, bool small_access = false, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool register_txn = false, bool ordered_with_prev_remote_write = false);
    void read_dram_vec(vector<uint32_t> &vec, tt_cxy_pair dram_core, uint64_t addr, uint32_t size, bool small_access = false, bool register_txn = false);

    void write_core_vec(vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr);
    void read_core_vec(vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size);
    void write_sysmem_vec(const void* mem_ptr, uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    void write_sysmem_vec(vector<uint32_t> &vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    void read_sysmem_vec(vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);

    //! address translation
    void *channel_0_address(std::uint32_t offset, std::uint32_t device_id) const;
    void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;

    std::map<int, int> get_all_device_aiclks();
    int get_device_aiclk(const chip_id_t &chip_id);
    tt_xy_pair get_first_active_worker(const chip_id_t& device_id);
    uint64_t get_core_timestamp(tt_cxy_pair target_core);
};

std::ostream &operator<<(std::ostream &os, tt_target_dram const &dram);
std::unique_ptr<buda_soc_description> load_soc_descriptor_from_file(const tt::ARCH &arch, std::string file_path);

// DebudaIFC provides access to internals that are normally hidden
class DebudaIFC {
        tt_cluster *m_cluster;
    public:
        DebudaIFC (tt_cluster *cluster) : m_cluster(cluster) {}
        void bar_write32 (int logical_device_id, uint32_t addr, uint32_t data);
        uint32_t bar_read32 (int logical_device_id, uint32_t addr);
        bool is_chip_mmio_capable(int logical_device_id);
        std::string get_harvested_coord_translation(int logical_device_id);
};
