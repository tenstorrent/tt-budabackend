// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <mutex>

#include "netlist/tt_backend.hpp"
#include "tt_log_server.hpp"
#include "runtime/runtime_utils.hpp"
#include "runtime/runtime_config.hpp"
#include "runtime/runtime_workload.hpp"
#include "loader/epoch_loader.hpp"

class tt_debuda_server;
class tt_cluster;
class netlist_program;

namespace perf {
    class MemoryProfiler;
}

/**
 * Buda runtime
 *
 */
class tt_runtime : public tt_backend
{
    public:
    tt_runtime(const tt_runtime_config &config, const std::set<chip_id_t> &target_device_ids = {});
    tt_runtime(const std::string &netlist_path, const tt_runtime_config &config);
    ~tt_runtime();

    //! Pre-run static init which includes epoch programs creation
    //! must be called once and only once before any running of programs
    tt::DEVICE_STATUS_CODE initialize() override;
    tt::DEVICE_STATUS_CODE initialize(tt_compile_result *result) override;

    //! Post-run teardown, dumping and reporting
    //! must be called once and only once after all programs are run
    tt::DEVICE_STATUS_CODE finish() override;

    //! Run user specified program
    tt::DEVICE_STATUS_CODE run_program(const std::string &program_name, const std::map<string, string> &parameters) override;

    //! Wait-for-idle (WFI) - Called to block until all launched programs are complete
    tt::DEVICE_STATUS_CODE wait_for_idle() override;

    //! Memory Barrier (MEMBAR) - Called to insert memory fence between prior and subsequent instructions
    tt::DEVICE_STATUS_CODE memory_barrier(tt::MemBarType barrier_type, chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores = {}) override;

    //! Queue Queries
    const tt::tt_dram_io_desc get_queue_descriptor(const std::string &queue_name) override;

    //! Eager mode incremental netlist compile
    tt::DEVICE_STATUS_CODE compile_netlist(const string &netlist_path) override;

    //! Eager mode incremental netlist compile and run all programs in the netlist
    tt::DEVICE_STATUS_CODE compile_and_run_netlist(const string &netlist_path, const std::map<std::string, std::string> &parameters) override;

    const std::vector<std::string>& get_programs() override;

    private:
    tt::ARCH arch_name;
    TargetDevice target_type;
    tt_runtime_workload workload;
    tt_runtime_state runtime_state;
    string soc_descriptor_path;
    string cluster_descriptor_path;
    std::unordered_map<chip_id_t, uint32_t> rows_to_harvest = {};
    std::unordered_map<string, uint32_t> graph_to_epoch_id = {};
    std::unordered_map<string, tt::tt_dram_io_desc> queue_descriptor_cache = {};
    // maps {global_epoch_id, device_id} to graph_name
    std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>> global_epoch_device_to_graph;
    std::set<chip_id_t> workload_target_device_ids;
    tt_compile_result compile_result;

    std::mutex insert_epoch_program_mutex;
    std::mutex runtime_state_mutex;
    std::mutex global_epoch_device_to_graph_mutex;
    std::mutex get_queue_descriptor_mutex;
    int compiled_epochs = 0;

    bool arch_supports_harvesting = false;
    bool performed_harvesting = false;
    bool noc_translation_enabled = false;
    bool need_overlay_recompile_during_run = false;
    bool need_risc_recompile_during_run = false;
    bool distribute_epoch_tables = !parse_env("TT_BACKEND_NON_DISTRIBUTED_EPOCH_TABLE", false);
    //! Pre-run static creation and init methods
    void verify_eth_fw_version();
    void create_graphs_and_init_queues();
    void create_graph_program(const tt_graph_info &graph_info);
    tt_overlay_compile_result create_graph_overlay_binaries();
    void update_graph_overlay_binaries();
    std::unordered_map<chip_id_t, buda_soc_description> load_soc_descriptors_per_chip(bool runtime_descriptor = false) const;
    void create_temporal_epoch_overlay_binaries(int temporal_epoch, const std::unordered_map<chip_id_t, buda_soc_description>& sdesc_per_chip, tt_compile_result_per_epoch& compile_result);
    void update_temporal_epoch_overlay_binaries(int temporal_epoch, const std::unordered_map<chip_id_t, buda_soc_description>& sdesc_per_chip);
    void load_parameter_and_constant_queues();
    
    //! Netlist checks for validity
    void check_input_queue_target_devices();
    void check_queue_remote_addresses();
    void check_host_mapped_queues_only_on_memory_mapped_devices();
    void check_host_mapped_queue_addresses();
    void check_dram_queues_do_not_overlap_reserved();
    void check_dram_buffers_do_not_exceed_memory();
    void check_graphs_fit_on_device();
    void check_netlist_constraints();
    //! Run instruction level methods
    void run_instruction(netlist_program &program);
    void run_execute_instrn(netlist_program &program, tt_instruction_info &instrn);
    void update_queue_header_dram_decouplings(string graph_name, bool reset);

    //! Run instruction level callbacks
    void execute_instruction_callback(netlist_program &program);
    void pre_instrn_instruction_callback(netlist_program &program);
    void post_instrn_instruction_callback(netlist_program &program);

    //! Misc methods
    void assign_global_epoch_ids(bool increment = true);
    void push_queue_side_effects(netlist_program &program);
    void set_runtime_state(tt_runtime_state state);
    void initialize_concurrent_perf_mode();
    void initialize_perf_state();
    void reset_dram_perf_buffers();
    void store_device_aiclk();
    void modify_perf_dram_reset_mailbox(bool toggle_one);
    void initialize_device_perf();
    void initialize_perf_overlay_decouple_epoch_command_mailbox();
    void perf_overlay_decouplings_update_epoch_command_start(uint device_id);
    void drain_perf_dram_buffers(bool last_drain);
    void finish_performance_trace();
    void initialize_memory_profiler();
    void add_graphs_to_memory_profiler();
    void profile_reserved_l1_binary_buffers();
    void profile_actual_l1_binary_buffers();
    void finish_l1_profiling_for_graphs();
    void create_memory_profiler_reports();
    void stop_debuda_server();
    void stop_log_server();
    void close_device();
    void run_performance_check();
    void clear_runtime_cache();
    void cleanup_runtime_and_close_device();
    void enable_loop_on_device_if_eligible(netlist_program &program, int loop_count);
    void check_for_dual_view_ram_rd_wr_overlap_in_graph(std::string &graph_name);
    void merge_compile_results(tt_compile_result* result, const tt_fw_compile_result& fw_compile_result,
                               const tt_overlay_compile_result& overlay_compile_result);
    void compile_firmware(tt_fw_compile_result& fw_compile_result);
    void compile_overlay(tt_overlay_compile_result& overlay_compile_result);
    public:
    std::unique_ptr<tt_cluster> cluster;
    std::unique_ptr<tt_epoch_loader> loader;
    std::unique_ptr<tt_log_server> log_server;
    tt_runtime_config config;
    std::vector<std::string> program_queue;
    std::unique_ptr<tt_debuda_server> debuda_server;
    std::unique_ptr<perf::MemoryProfiler> memory_profiler;

    std::unique_ptr<postprocess::PerfHostScratchBuffer> perf_host_scratch_buffer;
    std::unique_ptr<postprocess::PerfHostPostprocessQueue> perf_postprocess_queue;
    std::unique_ptr<postprocess::PerfHostGenerateReport> perf_report_generator;

    //! Getters
    tt_runtime_workload* get_workload();
    std::vector<tt_dram_io_desc> get_host_input_io_desc();
    std::vector<tt_dram_io_desc> get_device_output_io_desc();

    //! Setters
    void set_output_dir(const string &output_dir) { config.output_dir = output_dir; };
    void set_loader_settings(int opt_level);

    //! Device cluster methods
    void start_device_cluster();

    //! Utility methods
    void wait_for_idle(const std::set<int> &target_devices, string caller, int cmds_thresh=0);
    vector<tt::EpochPerfInfo> get_perf_info();
    unordered_map<tt_xy_pair, vector<uint64_t>> get_last_epoch_kernel_runtime();

    void ensure_devices_present(const TargetDevice &target_type);
    void check_epoch_metadata(const YAML::Node& runtime_data);
    void check_runtime_data();
    void export_runtime_data_to_yaml();
    std::string epoch_metadata_to_yaml();
    std::string runtime_data_to_yaml(); // Returns a string respresentation of runtime_data

    void perf_set_total_number_of_samples(int total_num_samples);
    void perf_set_device_end();

    private:
    void generate_soc_descriptor();
    void generate_cluster_descriptor();
    void perform_harvesting();
    void get_noc_translated_soc_desc();
    void query_all_device_aiclks(std::string loc);

    void sync_on_execute_dependencies(netlist_program &program, tt_instruction_info &instrn);
};
