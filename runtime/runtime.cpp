// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime.hpp"

#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include "common/cache_lib.hpp"
#include "common/param_lib.hpp"
#include "common/tt_parallel_for.h"
#include "compile_trisc/compile_trisc.hpp"
#include "netlist_utils.hpp"
#include "runtime_utils.hpp"
#include "runtime_io.hpp"
#include "utils/scoped_timer.hpp"
#include "perf_lib/analyse.hpp"
#include "debuda_server.hpp"
#include "netlist/netlist_program.hpp"
#include "tt_cluster.hpp"
#include "epoch_loader.hpp"
#include "device/cpuset_lib.hpp"
#include "common/model/tt_core.hpp"
#include "perf_lib/perf_base.hpp"
#include "perf_lib/memory_profiler.hpp"

namespace fs = std::experimental::filesystem; // see comment above

// ---------------------------------------------------------------------------
// Runtime object
// ---------------------------------------------------------------------------

extern perf::tt_backend_perf backend_profiler;

void tt_runtime::generate_soc_descriptor() {

    // If running a TTI (already unzipped) and it comes with soc desc, use it, and update backend config.
    if (config.tti && fs::exists(config.output_dir + "/device_desc.yaml")) {
        config.soc_descriptor_path = config.output_dir + "/device_desc.yaml";
        this->soc_descriptor_path = config.output_dir + "/device_desc.yaml";
        return;
    }

    // For runs where the tt_build directory is not deleted (Pybuda), the state encoded in the previous SOC descriptors must be cleared
    cleanup_stale_soc_descriptors();
    //Set SOC descriptor path to always be in the output dir to ensure consistency
    bool use_full_soc_desc = parse_env("FORCE_FULL_SOC_DESC", false);

    if (fs::exists(config.soc_descriptor_path)) {
        fs::copy(config.soc_descriptor_path, config.output_dir + "/device_desc.yaml");
        this->soc_descriptor_path = config.output_dir + "/device_desc.yaml";

    } else {
        if ((target_type != TargetDevice::Versim || use_full_soc_desc) and !fs::exists(config.output_dir + "/device_desc.yaml")) {
            fs::copy(get_soc_description_file(arch_name, target_type, config.output_dir), config.output_dir + "/device_desc.yaml");
        }
        this->soc_descriptor_path = config.output_dir + "/device_desc.yaml";
        if (config.soc_descriptor_path != "") log_warning(tt::LogRuntime, "Config.soc_descriptor_path='{}' doesn't exist, defaulting to '{}'", config.soc_descriptor_path, get_soc_description_file(arch_name, target_type, config.output_dir));
    }
    // Ability to skip this runtime opt, since trimmed SOC desc limits which DRAM channels are available.

    if (target_type == TargetDevice::Versim && !use_full_soc_desc) {
        // For Versim runs, to save on runtime, we "trim" the device to only instantiate the cores needed to run time model,
        // Otherwise compute cycles are wasted simulating unused cores.
        std::unique_ptr<buda_soc_description> default_full_soc_desc_ptr = get_default_soc_desc(arch_name);
        generate_soc_description(default_full_soc_desc_ptr.get(), soc_descriptor_path, workload.compute_max_grid(arch_name, default_full_soc_desc_ptr.get()));
    }
}

void tt_runtime::initialize_concurrent_perf_mode() {
    cluster->perf_state = postprocess::PerfState(config.output_dir, config.perf_desc, target_type);
    uint32_t thread_dump_size = config.perf_desc.get_host_thread_dump_size();
    uint16_t host_channel = 0; // This must be 0
    std::function<void(tt_cxy_pair, vector<uint32_t>)> update_host_queue_ptr = [this](tt_cxy_pair core, vector<uint32_t> vec) {
        this->cluster->write_core_vec(vec, core, l1_mem::address_map::PERF_QUEUE_PTRS);
    };
    std::unordered_map<uint, uint32_t*> device_id_to_scratch_buf_base;
    std::unordered_map<uint, set<uint> > mmio_device_to_mapped_devices;
    const std::unordered_map<chip_id_t, buda_SocDescriptor> &device_id_to_sdesc = cluster->get_sdesc_for_all_devices();
    tt_ClusterDescriptor *cluster_desc = cluster->get_cluster_desc();
    for (const auto &[device_id, sdesc]: device_id_to_sdesc) {
        uint mapped_chip_id;
        if (cluster_desc->is_chip_mmio_capable(device_id)) {
            mapped_chip_id = device_id;
            if (device_id_to_scratch_buf_base.find(device_id) == device_id_to_scratch_buf_base.end()) {
                log_debug(tt::LogPerfInfra, "Host Scratch Buffer base address for device id {} = {}", device_id, host_mem::address_map::HOST_PERF_SCRATCH_BUF_START);
                device_id_to_scratch_buf_base.insert({
                    device_id, reinterpret_cast<uint32_t *>(cluster->host_dma_address(host_mem::address_map::HOST_PERF_SCRATCH_BUF_START, host_channel, device_id))});
            }
        } else {
            log_assert(false, "Currently only mmio capable devices support concurrent perf mode");
            mapped_chip_id = cluster_desc->get_closest_mmio_capable_chip(device_id);
        }
        if (mmio_device_to_mapped_devices.find(mapped_chip_id) == mmio_device_to_mapped_devices.end()) {
            mmio_device_to_mapped_devices.insert({mapped_chip_id, {}});
        }
        mmio_device_to_mapped_devices.at(mapped_chip_id).insert(device_id);
    }
    log_assert(mmio_device_to_mapped_devices.size() == device_id_to_scratch_buf_base.size(),
            "Inconsistent number of mmio devices initialized for concurrent perf trace mode");

    perf_report_generator = std::make_unique<postprocess::PerfHostGenerateReport>(&(cluster->perf_state));
    perf_postprocess_queue = std::make_unique<postprocess::PerfHostPostprocessQueue>(&(cluster->perf_state), thread_dump_size, perf_report_generator.get());
    perf_host_scratch_buffer = std::make_unique<postprocess::PerfHostScratchBuffer>(
                                device_id_to_sdesc, mmio_device_to_mapped_devices, perf_postprocess_queue.get(), device_id_to_scratch_buf_base, thread_dump_size, update_host_queue_ptr);
}

void tt_runtime::initialize_perf_state() {
    if (config.perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent) {
        initialize_concurrent_perf_mode();
    } 
    else if (config.perf_desc.device_perf_mode == perf::PerfDumpMode::SingleDumpPerEpoch or config.perf_desc.device_perf_mode == perf::PerfDumpMode::IntermediateDump) {
        cluster->perf_state = postprocess::PerfState(config.output_dir, config.perf_desc, target_type);
    }
}

void tt_runtime::reset_dram_perf_buffers() {
    if (config.perf_desc.reset_dram_perf_buffer) {
        for (auto device_it: cluster->get_sdesc_for_all_devices()) {
            for (vector<tt_xy_pair> dram_core: device_it.second.dram_cores) {
                int perf_buf_size = dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_SIZE;
                int perf_buf_base = dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_BASE;
                std::vector<uint32_t> vec(perf_buf_size/4, 0xffffffff);
                cluster->write_dram_vec(vec, tt_cxy_pair(device_it.first, dram_core.at(0)), perf_buf_base);
            }
        }
    }
}

void tt_runtime::store_device_aiclk() {
    if (config.do_run() && cluster && config.perf_desc.enabled()) {
        const std::map<int, int> all_aiclks = cluster->get_device()->get_clocks();
        for (const auto &freq_it: all_aiclks) {
            cluster->perf_state.update_chip_id_to_aiclk(freq_it.first, freq_it.second);
        }
    }
}

void tt_runtime::modify_perf_dram_reset_mailbox(bool toggle_one) {
    for (auto device_it: cluster->get_sdesc_for_all_devices()) {
        for (tt_xy_pair worker_core: device_it.second.workers) {
            std::vector<uint32_t> target_word;
            if (toggle_one) {
                target_word.push_back(0xffffffff);
            } else {
                target_word.push_back(0);
            }
            cluster->write_dram_vec(target_word, tt_cxy_pair(device_it.first, worker_core), l1_mem::address_map::PERF_RESET_PTR_MAILBOX_ADDR);
        }
    }
}

void tt_runtime::initialize_perf_overlay_decouple_epoch_command_mailbox() {
    for (auto device_it: cluster->get_sdesc_for_all_devices()) {
        const uint device_id = device_it.first;
        tt_xy_pair first_core = cluster->get_first_active_worker(device_id);
        size_t translated_x = first_core.x;
        size_t translated_y = first_core.y;
        cluster->translate_to_noc_table_coords(device_id, translated_y, translated_x);
        tt_xy_pair translated(translated_x, translated_y);
        cluster->perf_state.update_device_to_first_core(device_id, first_core);
        cluster->perf_state.update_device_to_epoch_id(device_id, 0);
        std::vector<uint32_t> target_word;
        uint64_t addr = NOC_XY_ADDR(translated_x, translated_y, l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR);
        std::vector<uint32_t> zeros = {0};
        std::vector<uint32_t> selected = {0x12345678};
        target_word.push_back(addr & 0xffffffff);
        target_word.push_back((addr >> 32) & 0xffffffff);
        log_trace(tt::LogPerfInfra, "Initializing overlay-decouple epoch command sync ptrs for device {}. First_code = {}, translated_addr = {}, value_addr = {}, first_core_mailbox_addr = {}, first_core_addr = {}",
                device_id, first_core.str(), translated.str(), l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR, l1_mem::address_map::PERF_ANALYZER_COMMAND_START_PTR_ADDR, addr);
        for (tt_xy_pair worker_core: device_it.second.workers) {
            if (worker_core == first_core) {
                cluster->write_dram_vec(selected, tt_cxy_pair(device_id, worker_core), l1_mem::address_map::PERF_ANALYZER_COMMAND_START_PTR_ADDR);
            } else {
                cluster->write_dram_vec(target_word, tt_cxy_pair(device_id, worker_core), l1_mem::address_map::PERF_ANALYZER_COMMAND_START_PTR_ADDR);
            }
            cluster->write_dram_vec(zeros, tt_cxy_pair(device_id, worker_core), l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR);
        }
    }
}

void tt_runtime::perf_overlay_decouplings_update_epoch_command_start(uint device_id) {
    tt_xy_pair first_core = cluster->perf_state.get_first_core_for_device_id(device_id);
    uint current_epoch_id = cluster->perf_state.get_epoch_id_for_device_id(device_id);
    current_epoch_id++;
    log_trace(tt::LogPerfInfra, "Updating epoch command start signal for overlay-decoupling mode for device {}, core {}, epoch_id {}", device_id, first_core.str(), current_epoch_id);
    cluster->perf_state.update_device_to_epoch_id(device_id, current_epoch_id);
    std::vector<uint32_t> target_word = {current_epoch_id};
    cluster->write_dram_vec(target_word, tt_cxy_pair(device_id, first_core), l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR);
}

void tt_runtime::initialize_device_perf() {
    perf::ScopedEventProfiler profile("initialize-performance-trace");
    log_info(tt::LogPerfInfra, "Initializing device state for performance trace");
    reset_dram_perf_buffers();
    store_device_aiclk();
    modify_perf_dram_reset_mailbox(false);
    if (config.perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent) {
        perf_host_scratch_buffer->initialize();
        perf_host_scratch_buffer->start_device_trace_poll();
    }
    if (cluster && config.perf_desc.overlay_decouplings.size() > 0) {
        initialize_perf_overlay_decouple_epoch_command_mailbox();
    }
}

tt_runtime::tt_runtime(const tt_runtime_config &config_in, const std::set<chip_id_t> &device_ids_in) :
    workload(), config(config_in),
    workload_target_device_ids(device_ids_in),
    runtime_state(tt_runtime_state::Uninitialized)
{
    const auto host_name = get_tt_runtime_hostname();
    log_info(tt::LogRuntime, "Running tt_runtime on host: '{}'", host_name);

    if (backend_profiler.is_enabled()) {
        log_info(tt::LogPerfInfra, "Backend profiler is enabled");
    } else {
        log_info(tt::LogPerfInfra, "Backend profiler is disabled");
    }
    if (config.perf_desc.enabled() && (config.perf_desc.run_perf_analyzer || config.perf_desc.overlay_decouplings.size() > 0)) {
        log_warning(tt::LogRuntime, "Setting backend optimization_level to 0, since performance_analyzer/overlay_decouplings is enabled");
        config.optimization_level = 0;
    }
    if (config.l1_profiler_en) {
        log_info(tt::LogPerfInfra, "Memory profiler is enabled");
    }
    else {
        log_info(tt::LogPerfInfra, "Memory profiler is disabled");
    }
    tt::io::info.output_dir = config.output_dir;
    std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log_debug(tt::LogRuntime, "{} - Created tt_runtime on pid: {}", strtok(std::ctime(&time), "\n"), getpid());

    if (!fs::exists(config.output_dir))
        fs::create_directories(config.output_dir);

    this->arch_name = static_cast<tt::ARCH>(config.arch);
    this->target_type = static_cast<TargetDevice>(config.type);
    this->netlist_path = config.netlist_path;

    if (fs::exists(config.netlist_path)) {
        workload = tt_runtime_workload(config.netlist_path, config);
        workload_target_device_ids = workload.compute_target_device_ids();
        arch_name = static_cast<tt::ARCH>(workload.device_info.arch);
    }

    generate_soc_descriptor();
    generate_cluster_descriptor();
    log_assert(workload_target_device_ids.size() > 0, "Must define at least one target device.");

    // Runtime allows all archs to be harvested. tt_cluster::open_device has more restrictions
    arch_supports_harvesting = (arch_name == tt::ARCH::GRAYSKULL || arch_name == tt::ARCH::WORMHOLE_B0 || arch_name == tt::ARCH::WORMHOLE);

    if (config.do_run()) {
        bool clean_system_resources = true; // Main runtime thread is responsible for clearing corrupted state when initializing driver
        std::unordered_map<chip_id_t, uint32_t> harvesting_masks = {};
        if(using_sw_harvesting(config)) {
            // Parse harvested rows if set in config. Otherwise keep this map empty -> Silicon Driver will apply harvesting based on device
            harvesting_masks = parse_harvested_rows_from_config(config, workload_target_device_ids);
        }
        cluster = std::make_unique<tt_cluster>();
        cluster->open_device(arch_name, target_type, workload_target_device_ids, soc_descriptor_path, cluster_descriptor_path, !config.valid_device_desc(), false, clean_system_resources, config.output_dir, harvesting_masks);
    }
    perform_harvesting();

    if (cluster) {  // harvesting successful
        // CpuSetAllocator is only used for Silicon, create it here early and capture main threadID for IO API binding checking purposes.
        if (target_type == TargetDevice::Silicon && config.concurrent_mode){
            tt::cpuset::tt_cpuset_allocator::set_main_thread_id();
        }
        loader = std::make_unique<tt_epoch_loader>(cluster.get(), config.output_dir, workload_target_device_ids);
        log_server = std::make_unique<tt_log_server>(cluster.get(), config.output_dir, workload_target_device_ids);
    }

    // Put objects in the cache for other threads to use
    log_assert(!tt_object_cache<tt_cluster>::exists(config.netlist_path), "Existing cluster found!");
    log_assert(!tt_object_cache<tt_runtime_workload>::exists(config.netlist_path), "Existing workload found!");
    tt_object_cache<tt_cluster>::set(config.netlist_path, cluster.get());
    tt_object_cache<tt_runtime_workload>::set(config.netlist_path, &workload);
    // Create Debuda server object (this will not run the socket server unless requested. See tt_debuda_server::tt_debuda_server)
    debuda_server = std::make_unique<tt_debuda_server>(this);

    if (config.do_run()) {
        initialize_perf_state();
    }

    if (config.l1_profiler_en) {
        initialize_memory_profiler();
    }
    
}

tt_runtime::tt_runtime(const std::string &netlist_path, const tt_runtime_config &config) :
    tt_runtime(get_runtime_config(config, netlist_path)) {}

void setup_perf_output_dir(perf::PerfDesc perf_desc, string test_output_dir) {
    if (perf_desc.device_perf_mode != perf::PerfDumpMode::Disable) {
        postprocess::get_perf_out_directory(test_output_dir, perf_desc.override_perf_output_dir, true);
        postprocess::get_device_perf_out_directory(test_output_dir, perf_desc, true);
        postprocess::get_host_perf_out_directory(test_output_dir, perf_desc.override_perf_output_dir, true);
        postprocess::get_graph_descriptor_out_directory(test_output_dir, perf_desc.override_perf_output_dir, true);
        postprocess::get_queue_descriptor_out_directory(test_output_dir, perf_desc.override_perf_output_dir, true);
        postprocess::get_metadata_out_directory(test_output_dir, perf_desc.override_perf_output_dir, true);
    } else {
        postprocess::remove_perf_output_directory(test_output_dir, perf_desc.override_perf_output_dir);
     }
 }

tt_runtime::~tt_runtime() {
    cleanup_runtime_and_close_device();
}

void tt_runtime::set_runtime_state(tt_runtime_state state) {
    const std::lock_guard<std::mutex> lock(runtime_state_mutex);
    if (state == tt_runtime_state::RunBusy) {
        log_assert(runtime_state != tt_runtime_state::Uninitialized, "Previous state cannot be 'Uninitialized', initialize() may be missing");
    }
    else if (state == tt_runtime_state::RunComplete) {
        log_assert(runtime_state != tt_runtime_state::Uninitialized, "Previous state cannot be 'Uninitialized', initialize() may be missing");
    }
    else if (state == tt_runtime_state::Uninitialized) {
        log_assert(runtime_state != tt_runtime_state::RunBusy, "Previous state cannot be 'RunBusy', wait_for_completion() may be missing");
    }
    this->runtime_state = state;
}

//! initialize - Must be called once and only once before any running of programs
tt::DEVICE_STATUS_CODE tt_runtime::initialize() {
    return this->initialize(nullptr);
}

tt::DEVICE_STATUS_CODE tt_runtime::initialize(tt_compile_result *result) {
    PROFILE_SCOPE_MS();
    try {
        if (result) {
            // Make sure the result success and failure type are properly initialized before compilation.
            result->success = true;
            result->failure_type = COMPILE_FAILURE::Invalid;
        }

        log_debug(tt::LogRuntime, "runtime initialize called (skip_device_init={}) on pid {}", config.skip_device_init, getpid());
        // Initialize profiler
        if (!config.skip_device_init) {
            backend_profiler.setup_host_perf_profiler(config.output_dir, config.perf_desc.override_perf_output_dir, getpid(), false);
            setup_perf_output_dir(config.perf_desc, config.output_dir);
            if (config.do_run() && config.type == tt::DEVICE::Silicon) {
                // check system level settings for backend compatibility
                check_system_params(config.output_dir);
            }
        }

        // Static checks on netlist and graphs
        check_runtime_data();
        check_netlist_constraints();
        // Handle eager runtime, check for new graphs and add them to the profiler on the fly

        add_graphs_to_memory_profiler();
        profile_l1_binary_buffer_reserved_sizes();

        backend_profiler.record_loader_event("COMPILE");
        if(config.do_compile() or config.perf_desc.always_compile() or need_overlay_recompile_during_run or need_risc_recompile_during_run) {
            log_info(tt::LogRuntime, "Compiling Firmware for TT device");
        }

        // Firmware compilation data (thread, compile_result).
        std::thread fw_compilation_thread;
        tt_fw_compile_result fw_compile_result;

        fw_compilation_thread = std::thread([&] {
            compile_firmware(fw_compile_result);
        });
        
        // Overlay compilation data (thread, compile_result).
        std::thread overlay_compilation_thread;
        tt_overlay_compile_result overlay_compile_result;

        overlay_compilation_thread = std::thread([&] {
            compile_overlay(overlay_compile_result);
        });

        fw_compilation_thread.join();

        overlay_compilation_thread.join();

        merge_compile_results(result, fw_compile_result, overlay_compile_result);

        if (fw_compile_result.success) {
            log_debug(tt::LogRuntime, "Firmware compile result : {}", fw_compile_result.get_string());
        } else {
            log_fatal("Firmware compilation failed: {}", fw_compile_result.get_string());
        }

        if (overlay_compile_result.success) {
            log_debug(tt::LogRuntime, "Overlay compile result : {}", overlay_compile_result.get_string());
        } else {
            log_fatal("Overlay compilation failed: {}", overlay_compile_result.get_string());
        }

        if (result) {
            if (config.do_compile()) {
                uint num_temporal_epochs = this->workload.get_number_of_temporal_graphs();
                // the global epoch start id for this workload is the number of epochs we've compiled across all workloads minus number of temporal epochs of this workload
                log_assert(compiled_epochs >= num_temporal_epochs, "Unexpected {} epochs compiled < {} epochs current workload.", compiled_epochs, num_temporal_epochs);
            }
            else {
                log_debug(tt::LogCompileTrisc, "Compile skipped");
            }
        }

        backend_profiler.record_loader_event("COMPILE");

        // Start device cluster and send static data
        if (config.do_run()) {
            ensure_devices_present(target_type);

            // Initialize loader settings
            set_loader_settings(config.optimization_level);
            start_device_cluster();
            verify_eth_fw_version();
            log_server->start();

            query_all_device_aiclks("runtime initialize()");

            // Initialize workload on device
            create_graphs_and_init_queues();
        }
        set_runtime_state(tt_runtime_state::Initialized);

        if (cluster && config.do_run() && config.perf_desc.enabled()) {
            initialize_device_perf();
        }

        profile_l1_binary_buffer_consumed_sizes();

        // At this point, all buffers for all graphs should have been added to the l1 profiler
        finish_l1_profiling_for_graphs();

        export_runtime_data_to_yaml();
        return tt::DEVICE_STATUS_CODE::Success;
    }  catch (const std::exception &e) {
        // If result success hasn't been set to false, but we have an exception, we have to
        // still set success to false and copy exception message to compile result.
        if (result && result->success) {
            result->success = false;
            result->failure_type = COMPILE_FAILURE::Invalid;
            result->failure_message = e.what();
            match_failure_target(result->failure_target, e.what());
        }
        log_error("{}", e.what());
        cleanup_runtime_and_close_device();
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

void tt_runtime::verify_eth_fw_version() {
    if (target_type == TargetDevice::Silicon) {
        // MT Initial BH - skip eth FW verif on Blackhole as well
        if (arch_name != tt::ARCH::GRAYSKULL and arch_name != tt::ARCH::BLACKHOLE) {        // Skip eth fw version 
            // Only check ethernet FW version for Silicon backend (not on GS devices)
            tt_version cluster_eth_fw_version = cluster -> get_ethernet_fw_version();
            tt_version min_eth_fw_version = tt_version(6, 8, 0);
            log_assert(cluster_eth_fw_version >= min_eth_fw_version, "The minimum ethernet Firmware Version for the cluster is expected to be 6.8.0 for compatibility with Buda Runtime.");
        }
    }
}

//! Queue Queries
const tt::tt_dram_io_desc tt_runtime::get_queue_descriptor(const std::string &queue_name) {
    const std::lock_guard<std::mutex> lock(get_queue_descriptor_mutex);
    // RMW operation on queue_descriptor_cache. Ensure that this is thread safe
    if (queue_descriptor_cache.find(queue_name) == queue_descriptor_cache.end()) {
        log_assert(workload.queues.find(queue_name) != workload.queues.end(), "get_queue_desc cannot find queue={}", queue_name);
        queue_descriptor_cache.insert({queue_name, workload.get_io_desc_for_queue(queue_name)});
        tt_dram_io_desc& io_desc = queue_descriptor_cache.at(queue_name);
        io_desc.netlist_path = netlist_path;
        io_desc.backend_type = config.type;

        if(config.tti) {
            // Get stride descriptor populated from TTI metadata
            auto prestride_per_io = config.tti -> get_io_prestride_map();
            if (prestride_per_io.find(queue_name) != prestride_per_io.end()) {
                io_desc.s_descriptor.stride = std::get<3>(prestride_per_io.at(queue_name));
                for (int x = 0; x < io_desc.s_descriptor.stride; x++) {
                    for (int y = 0; y < io_desc.s_descriptor.stride; y++) {
                        io_desc.s_descriptor.xy_offsets.push_back({x, y});
                    }
                }
            }
        }
    }
    return queue_descriptor_cache.at(queue_name);
}

tt_runtime_workload* tt_runtime::get_workload() {
    return &workload;
}

const std::vector<std::string>& tt_runtime::get_programs() {
    return workload.get_programs();
}

void tt_runtime::generate_cluster_descriptor() {
    // Check if user provided path for cluster descriptor exists
    if (fs::exists(config.cluster_descriptor_path)) {
        log_debug(tt::LogRuntime, "Using user provided cluster descriptor file at path='{}'", config.cluster_descriptor_path);
        this->cluster_descriptor_path = config.cluster_descriptor_path;
        // Allow previously generated cluster desc to be reused for tests in loops. Don't copy to itself otherwise error.
        if (config.cluster_descriptor_path != (config.output_dir + "/cluster_desc.yaml")) {
            fs::copy(config.cluster_descriptor_path, config.output_dir + "/cluster_desc.yaml", fs::copy_options::overwrite_existing);
        }
    }
    else {
        tt_runtime_workload &workload               = *get_workload();
        std::vector<tt::ARCH> available_devices     = tt_cluster::detect_available_devices(this->target_type);
        auto available_devices_for_arch             = std::count(available_devices.begin(), available_devices.end(), arch_name);

        // Check if there are at least 1 wormhole silicon devices present - if so generate cluster description file and set path
        if (available_devices_for_arch >= 1 && (arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0)) {
            generate_cluster_desc_yaml(config.output_dir);
            this->cluster_descriptor_path = config.output_dir + "/cluster_desc.yaml";
        } else {
            log_debug(tt::LogRuntime, "Setting cluster_descriptor_path to null");
            this->cluster_descriptor_path = "";
        }
    }
}

void tt_runtime::perform_harvesting() {
    // Compile-Time Mode: Populate device specific metadata (noc translation + harvesting state) so that binaries are setup correctly for deployment.
    //                    If the state is incorrectly populated (ex: if compile and run harvesting config mismatch), runtime will assert.
    //                    Generate NOC Translated/Harvested SOC descriptors, depending on whether a valid descriptor was passed in or if translation is enabled.

    // Runtime Mode: Query Device state and generate harvested descriptors if a valid descriptor was not passed in. Perform NOC translation.

    if(!config.do_run()) {
        // Assume NOC translation is enabled by default for WH compiles. This is the case for production.
        bool wh_arch = (arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0);
        noc_translation_enabled = (wh_arch and target_type == TargetDevice::Silicon);
        performed_harvesting = using_sw_harvesting(config);

        // Parse harvested rows set by user and convert to masks used to create updated SOC descriptors
        std::unordered_map<chip_id_t, uint32_t> harvesting_masks = parse_harvested_rows_from_config(config, workload_target_device_ids);
        bool harvest_soc_descs = performed_harvesting and !config.valid_device_desc(); // GS Only: Do not generate harvested descriptors if a valid one is passed in

        if(harvest_soc_descs or noc_translation_enabled) {
            // For GS this step is performed only if harvesting needs to be performed
            log_assert(target_type == TargetDevice::Silicon, "Harvesting is only supported for Silicon devices.");
            // Defaults for GS: Runtime and overlay descriptors are identical. Stored in device_descs/
            std::string runtime_soc_desc_dir = config.output_dir + "/device_descs/";
            std::string overlay_soc_desc_dir = "";

            if(!config.valid_device_desc() or noc_translation_enabled) {
                // Even if a valid device descriptor is passed into runtime, perform NOC translation during compile
                if(noc_translation_enabled) {
                    // Runtime and overlay descriptors use different coordinate systems
                    runtime_soc_desc_dir = config.output_dir + "/device_desc_runtime/";
                    overlay_soc_desc_dir = config.output_dir + "/device_descs/";
                    if(!fs::exists(overlay_soc_desc_dir)) fs::create_directories(overlay_soc_desc_dir);
                }
                // Common directory generation step between GS and WH. Done for GS only if harvested SOC descriptors are
                // generated.
                if(!fs::exists(runtime_soc_desc_dir)) fs::create_directories(runtime_soc_desc_dir);
            }
            log_assert(harvesting_masks.size() == workload_target_device_ids.size(),
                        "Number of entries in the comma seperated harvesting config should match the number of devices in the netlist. Num Devices: {} Num Entries: {}",
                        workload_target_device_ids.size(), harvesting_masks.size());
            for(const auto& chip : workload_target_device_ids) {
                rows_to_harvest.insert({chip, harvesting_masks[chip]});
                if(!config.valid_device_desc() or noc_translation_enabled) {
                    generate_soc_descriptors_for_compile(config, noc_translation_enabled, soc_descriptor_path, chip, harvesting_masks[chip], arch_name, runtime_soc_desc_dir, overlay_soc_desc_dir);
                }
            }
            // Point Overlay generation code to harvested/translated SOC descriptors.
            generate_sdesc_yaml_for_overlay_compile(workload_target_device_ids, arch_name, noc_translation_enabled);
        }
    }
    else {
        noc_translation_enabled = cluster -> noc_translation_en;
        performed_harvesting = cluster -> performed_harvesting;
        rows_to_harvest = cluster -> harvested_rows_per_target;
        // User can specify a device descriptor file to use instead of harvesting
        if (performed_harvesting and !config.valid_device_desc())
        {

            if(!parse_env<bool>("TT_BACKEND_HETEROGENOUS_HARVESTING", false)) {
                auto const consistent_grid_sizes =  [&] (std::pair<chip_id_t, std::uint32_t> harv_for_chip) {
                    return std::bitset<32>(rows_to_harvest.begin() -> second).count() == std::bitset<32>(harv_for_chip.second).count();
                };
                bool same_grid_size = std::all_of(rows_to_harvest.begin(), rows_to_harvest.end(), consistent_grid_sizes);
                if(!same_grid_size) {
                    log_warning(tt::LogRuntime, "Cluster is heterogeneously harvested");
                }
            }
            soc_descriptor_path = config.output_dir + "/device_descs/";
            generate_sdesc_yaml_for_overlay_compile(workload_target_device_ids, arch_name, noc_translation_enabled);
        }
        else {
            if(noc_translation_enabled) {
                soc_descriptor_path = config.output_dir + "/device_descs/";
                generate_sdesc_yaml_for_overlay_compile(workload_target_device_ids, arch_name, noc_translation_enabled);
            }
        }
    }
}

void tt_runtime::start_device_cluster() {
    if (config.skip_device_init) {
        config.device_params.init_device = false;
    }
    cmd_result_t result = run_function_with_timeout([&](){ cluster->start_device(config.device_params); }, get_api_timeout(tt_timeout_api_type::StartDevice), false);
    log_assert(result.success, "{}: {}", __FUNCTION__, result.message);
}

tt::DEVICE_STATUS_CODE tt_runtime::compile_netlist(const std::string &netlist_path) {
    try {
        log_info(tt::LogRuntime, "Compile netlist: {}", netlist_path);
        log_assert(fs::exists(netlist_path), "Netlist path does not exist: {}", netlist_path);

        this->netlist_path = netlist_path;

        workload = tt_runtime_workload(netlist_path, config);
        tt_object_cache<tt_runtime_workload>::set(netlist_path, &workload);
        log_assert(this->arch_name == static_cast<tt::ARCH>(workload.device_info.arch), "Netlist arch does not match runtime arch");

        // perform device init only if the runtime is uninitialized
        if (!config.skip_device_init) {
            config.skip_device_init = runtime_state != tt_runtime_state::Uninitialized;
        }
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
    return this->initialize();
}

tt::DEVICE_STATUS_CODE tt_runtime::compile_and_run_netlist(const std::string &netlist_path, const std::map<std::string, std::string> &parameters) {
    if (this->compile_netlist(netlist_path) != tt::DEVICE_STATUS_CODE::Success) {
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
    for (const auto &program_name : workload.program_order) {
        if (this->run_program(program_name, parameters) != tt::DEVICE_STATUS_CODE::Success)
            return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
    return tt::DEVICE_STATUS_CODE::Success;
}

void tt_runtime::set_loader_settings(int opt_level) {
    // Initialize loader config settings
    loader->set_optimization_level(opt_level);
    loader->skip_device_init = config.skip_device_init;
    loader->skip_io_init = config.skip_io_init;
}

void tt_runtime::create_graph_program(const tt_graph_info &graph_info) {
    uint32_t target_device = graph_info.target_device;
    shared_ptr<tt_epoch_binary> binary = std::make_shared<tt_epoch_binary>(target_device);
    binary->get_epoch_binaries(config.output_dir, graph_info.name, graph_info.op_map, cluster -> get_soc_desc(graph_info.target_device));
    bool perf_en = config.perf_desc.is_perf_enabled_for_graph(graph_info.name);
    std::unordered_map<tt_xy_pair, uint16_t> all_decouple_masks = tt::get_input_output_overlay_decouple_mask_graph(
        graph_info, config.perf_desc, cluster -> get_soc_desc(graph_info.target_device));
    tt_epoch_program_info epoch_info = {
        .binary = binary,
        .name = graph_info.name,
        .target_device = target_device,
        .device_perf_trace_en = perf_en,
        .overlay_decouple_mask = all_decouple_masks};

    log_debug(tt::LogRuntime, "Assembled epoch program: {} ", epoch_info);
    const std::lock_guard<std::mutex> lock(insert_epoch_program_mutex);
    loader->insert_epoch_program(std::move(epoch_info));
}

tt_overlay_compile_result tt_runtime::create_graph_overlay_binaries() {
    PROFILE_SCOPE_MS();
    int num_threads = tt::cpuset::get_allowed_num_threads();
    log_debug(tt::LogRuntime, "create_graph_overlay_binaries() -- num_threads: {}", num_threads);

    // assign unique epoch id to each compiled set of temporal graphs to uniquify stream phases for deadlock avoidance
    int num_temporal_epochs = this->workload.get_number_of_temporal_graphs();
    perf::ScopedEventProfiler profile(perf::HostEventType::PIPEGEN_RUNTIME);
    std::unordered_map<chip_id_t, buda_soc_description> sdesc_per_chip = load_soc_descriptors_per_chip();

    std::vector<tt_compile_result_per_epoch> compile_results_per_epoch(num_temporal_epochs);
    tt::parallel_for(0, num_temporal_epochs, [=, &sdesc_per_chip, &compile_results_per_epoch](int temporal_epoch) {
        tt_compile_result_per_epoch &compile_result_for_current_epoch = compile_results_per_epoch[temporal_epoch];
        this->create_temporal_epoch_overlay_binaries(temporal_epoch, sdesc_per_chip, compile_result_for_current_epoch);
    }, num_threads);

    tt_overlay_compile_result compile_result;
    compile_result.failed_compile_results_per_epoch.clear();

    for (tt_compile_result_per_epoch& compile_result_epoch : compile_results_per_epoch) {
        if (!compile_result_epoch.success) {
            compile_result.failed_compile_results_per_epoch.push_back(compile_result_epoch);
        }
    }

    if (!compile_result.failed_compile_results_per_epoch.empty()) {
        compile_result.success = false;
        compile_result.failure_message = "Overlay compilation failed";
        compile_result.failure_type = compile_result.failed_compile_results_per_epoch[0].failure_type;
        compile_result.failure_target = compile_result.failed_compile_results_per_epoch[0].failure_target;
    }

    return compile_result;
}

void tt_runtime::update_graph_overlay_binaries() {
    int num_threads = tt::cpuset::get_allowed_num_threads();
    log_debug(tt::LogRuntime, "update_graph_overlay_binaries() -- num_threads: {}", num_threads);

    // assign unique epoch id to each compiled set of temporal graphs to uniquify stream phases for deadlock avoidance
    int num_temporal_epochs = this->workload.get_number_of_temporal_graphs();
    perf::ScopedEventProfiler profile(perf::HostEventType::PIPEGEN_RUNTIME);
    std::unordered_map<chip_id_t, buda_soc_description> sdesc_per_chip = load_soc_descriptors_per_chip();

    tt::parallel_for(0, num_temporal_epochs, [=, &sdesc_per_chip](int temporal_epoch) {
        this->update_temporal_epoch_overlay_binaries(temporal_epoch, sdesc_per_chip);
    }, num_threads);
}

std::unordered_map<chip_id_t, buda_soc_description> tt_runtime::load_soc_descriptors_per_chip(bool runtime_descriptor) const
{
    std::unordered_map<chip_id_t, buda_soc_description> sdesc_per_chip;
    for (const auto &chip : workload_target_device_ids) {
        sdesc_per_chip.insert({chip, *load_soc_descriptor_from_yaml(get_soc_desc_path(chip, runtime_descriptor))});
    }

    return sdesc_per_chip;
}

void tt_runtime::create_temporal_epoch_overlay_binaries(
    int temporal_epoch,
    const std::unordered_map<chip_id_t, buda_soc_description>& sdesc_per_chip,
    tt_compile_result_per_epoch& compile_result) {

    // compute global epoch id from workload's local epoch id
    int global_epoch_id = compiled_epochs + temporal_epoch;

    const std::unordered_set<std::string> &graph_names = workload.get_graphs_of_temporal_graph(temporal_epoch);
    std::vector<chip_id_t> chip_ids;
    for (const string &graph_name : graph_names) {
        chip_id_t chip_id = this->workload.get_graph_chip(graph_name);
        chip_ids.push_back(chip_id);
        // Even though threads should be accessing different keys, lock a mutex to avoid hash collision/optimization bugs.
        const std::lock_guard<std::mutex> lock(this->global_epoch_device_to_graph_mutex);
        global_epoch_device_to_graph[global_epoch_id][chip_id] = graph_name;
    }

    // Interfacing harvesting functionality with pipegen:
    // use default descriptor unless the chip is harvested with multiple different SOC desc,
    // where device_descs_for_pipegen.yaml contains the soc desc path per chip
    string file_to_use = fs::exists(tt::io::info.output_dir + "/device_descs_for_pipegen.yaml") ?
                         tt::io::info.output_dir + "/device_descs_for_pipegen.yaml" :
                         soc_descriptor_path;

    run_pipegen_and_blobgen(config.output_dir, *(graph_names.begin()), global_epoch_id, chip_ids, config.perf_desc,
                            file_to_use, sdesc_per_chip, compile_result, memory_profiler.get(), this->global_epoch_device_to_graph);
}

void tt_runtime::update_temporal_epoch_overlay_binaries(int temporal_epoch, const std::unordered_map<chip_id_t, buda_soc_description>& sdesc_per_chip) {
    for (const string &graph_name : workload.get_graphs_of_temporal_graph(temporal_epoch)) {
        tt_epoch_program_info &epoch_info = loader->get_epoch_program_info(graph_name);
        int chip_id = this->workload.get_graph_chip(graph_name);
        epoch_info.epoch_id = graph_to_epoch_id.at(graph_name);
        epoch_info.binary->update_overlay_binary(config.output_dir, graph_name, epoch_info.epoch_id, chip_id, &sdesc_per_chip.at(chip_id),
                                                 cluster.get());
    }
}

// For multichip netlists, for all ops in graphs whose inputs are input-queues in DRAM, ensure their target_devices matches graph's target_device. Just for Grayskull.
void tt_runtime::check_input_queue_target_devices(){
    if (workload_target_device_ids.size() == 1){
        return;
    }
    bool remote_queue_consumers = false;

    for (const auto &graph : workload.graphs) {
        const tt_graph_info &graph_info = graph.second.my_graph_info;
        for (const auto &op: graph_info.op_map){
            for (const auto &input_name: op.second.input_names){
                if (workload.queues.find(input_name) != workload.queues.end()){
                    tt_queue_info &queue_info = workload.queues.at(input_name).my_queue_info;
                    if (queue_info.loc == QUEUE_LOCATION::DRAM){
                        if (queue_info.target_device != graph_info.target_device){
                            log_debug(tt::LogRuntime, "op {} has input queue {} target_device: {} that does not match graph's target_device: {}", op.first, queue_info.name, queue_info.target_device, graph_info.target_device);
                            remote_queue_consumers = true;
                        }
                    }
                }
            }
        }
    }
    if (remote_queue_consumers) {
        log_assert(workload.device_info.arch != tt::ARCH::GRAYSKULL, "Found op input queues on different target_device than their graphs, this is not allowed on the given ARCH");
    }
}


// For multichip netlists, must ensure that the remote queue DRAM addr is in upper 256MB range if it is on different device than producer op. Just for Grayskull.
void tt_runtime::check_queue_remote_addresses(){

    if ((workload_target_device_ids.size() == 1) || (workload.device_info.arch != tt::ARCH::GRAYSKULL)){
        return;
    }

    bool remote_addr_violations = false;

    for (auto &graph : workload.graphs) {
        auto graph_info                     = graph.second.my_graph_info;
        auto target_device_graph            = graph_info.target_device;
        for (auto &op_name : graph.second.op_name_to_output_queue_map){
            auto queue_info             = op_name.second.my_queue_info;
            auto target_device_queue    = queue_info.target_device;

            if (queue_info.loc != QUEUE_LOCATION::DRAM){
                continue;
            }

            if (target_device_queue != target_device_graph){
                for (auto &alloc : queue_info.alloc_info){
                    if (alloc.channel != DRAM_PEER2PEER_CHANNEL){
                        remote_addr_violations = true;
                        log_error("Remote queue: {} alloc must be on ch:0 when queue is on different target_device than producer op.", queue_info.name);
                    }else if (alloc.address < tt::DRAM_PEER2PEER_REGION_START){
                        remote_addr_violations = true;
                        log_error("Remote queue: {} alloc must have addr >= DRAM_PEER2PEER_REGION_START when queue is on different target_device than producer op.", queue_info.name);
                    }

                }
            }
        }
    }
    log_assert(!remote_addr_violations, "Found remote queues in unsupported DRAM address range, see preceding errors.");
}

void tt_runtime::check_host_mapped_queues_only_on_memory_mapped_devices() {
    bool found_violations = false;

    if (cluster_descriptor_path == "") {
        return; // devices are all mmio mapped
    }

    auto ndesc = tt_cluster_description::create_from_yaml(cluster_descriptor_path);
    for (const auto &graph : workload.graphs) {
        auto graph_info                     = graph.second.my_graph_info;
        auto target_device_graph            = graph_info.target_device;
        for (const auto &[op_name, output_queue_map] : graph.second.op_name_to_output_queue_map){
            auto queue_info             = output_queue_map.my_queue_info;
            auto target_device_queue    = queue_info.target_device;

            if (queue_info.loc == QUEUE_LOCATION::HOST){
                if (!ndesc->is_chip_mmio_capable(target_device_graph)) {
                    log_error("queue {} has a host memory mapping, but its producer is mapped to device {}, which is not memory mappable", queue_info.name, target_device_graph);
                }
            }

        }
    }

    log_assert(!found_violations, "Found host mapped queues that are mapped to devices that don't have memory mapping abilities themselves. Check that the cluster description and netlist are compatible");
}

// Host queue addresses must be in addressable range.
void tt_runtime::check_host_mapped_queue_addresses() {

    bool found_violations = false;

    for (const auto &graph : workload.graphs) {
        auto graph_info                     = graph.second.my_graph_info;
        auto target_device_graph            = graph_info.target_device;
        for (const auto &[op_name, output_queue_map] : graph.second.op_name_to_output_queue_map){
            auto queue_info             = output_queue_map.my_queue_info;

            if (queue_info.loc == QUEUE_LOCATION::HOST){
                // Address is actually an offset into host region.
                for (auto &alloc : queue_info.alloc_info){
                    if (alloc.address >= host_mem::address_map::DEVICE_TO_HOST_MMIO_SIZE_BYTES){
                        found_violations = true;
                        log_error("Host queue: {} alloc address: {} exceeds DEVICE_TO_HOST_MMIO_SIZE_BYTES: {} addressable size.", queue_info.name, alloc.address, host_mem::address_map::DEVICE_TO_HOST_MMIO_SIZE_BYTES);
                    }
                }
            }

        }
    }

    log_assert(!found_violations, "Found host queue size violations, see previous errors.");
}

void tt_runtime::check_dram_queues_do_not_overlap_reserved() {
    // Avoid using tt_backend_params here by rederiving top_of_q_update_blob.
    std::unique_ptr<buda_soc_description> sdesc;
    unordered_map<chip_id_t, int> num_chans_per_chip;
    unordered_map<chip_id_t, vector<uint16_t>> t6_cores_per_dram_channel_per_device;
    unordered_map<chip_id_t, vector<uint16_t>> eth_cores_per_dram_channel_per_device;
    for(auto device_id = workload_target_device_ids.begin(); device_id != workload_target_device_ids.end(); device_id++){
        sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(*device_id, true)); // Use Soc Desc passed into loader here.
        num_chans_per_chip[*device_id] = sdesc->get_dram_chan_map().size();
        t6_cores_per_dram_channel_per_device[*device_id] = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch_name, sdesc->workers, sdesc->dram_core_channel_map, num_chans_per_chip[*device_id]);
        eth_cores_per_dram_channel_per_device[*device_id] = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch_name, sdesc->ethernet_cores, sdesc->dram_core_channel_map, num_chans_per_chip[*device_id]);
    }

    for (auto &queue : workload.queues){
        const tt_queue_info &queue_info = queue.second.my_queue_info;


        if (queue_info.loc == QUEUE_LOCATION::DRAM) {
            for (auto &alloc : queue_info.alloc_info) {
                uint64_t top_of_q_update_blob = tt_epoch_dram_manager::get_top_of_q_update_blobs(t6_cores_per_dram_channel_per_device[queue_info.target_device][alloc.channel], eth_cores_per_dram_channel_per_device[queue_info.target_device][alloc.channel]);
                if (alloc.address < top_of_q_update_blob){
                    log_fatal(
                        "DRAM IO queue {} (addr: 0x{:x}) must be above reserved space ending at: 0x{:x} for channel {}",
                        queue.first, alloc.address, top_of_q_update_blob, alloc.channel);
                }
            }
        }
    }
}

void tt_runtime::check_dram_buffers_do_not_exceed_memory(){
    const string arch = get_arch_str(arch_name);
    for(auto &queue : workload.queues){
        auto& params = tt::param::tt_backend_params::get(get_soc_desc_path(queue.second.my_queue_info.target_device, true), this->cluster_descriptor_path);
        auto dram_capacity = std::stoull(params.get_param(tt::param::get_lookup_key({arch, "dram", "channel_capacity"})));
        if(queue.second.my_queue_info.loc == QUEUE_LOCATION::DRAM){
            auto& queue_buffers = workload.queues.at(queue.first).my_queue_info.alloc_info;
            for(size_t buf_id = 0; buf_id < queue_buffers.size(); buf_id++){
                buffer_range_t range = workload.get_buffer_range(queue.first, buf_id);
                stringstream ss1, ss2;
                ss1 << "The start address of buffer " << buf_id << " in queue " << queue.first << " is 0x" << std::hex << range.start_addr << ". This exceeds the DRAM range of 0x" << dram_capacity;
                ss2 << "The end address of buffer " << buf_id << " in queue " << queue.first << " is 0x" << std::hex << range.end_addr << ". This exceeds the DRAM range of 0x" << dram_capacity;
                log_assert(range.start_addr < dram_capacity, "{}", ss1.str());
                log_assert(range.end_addr <= dram_capacity, "{}", ss2.str());
            }
        }
    }
}

void tt_runtime::check_graphs_fit_on_device() {
    std::unordered_map<chip_id_t, tt_xy_pair> chip_to_grid_size;
    for(auto &chip : workload_target_device_ids) {
        auto sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(chip));
        chip_to_grid_size.insert({chip, sdesc->worker_grid_size});
    }

    for(const auto &graph : workload.graphs) {
        auto op_map = graph.second.my_graph_info.op_map;
        auto chip_id = graph.second.my_graph_info.target_device;
        auto worker_grid_size = chip_to_grid_size[chip_id];
        for(const auto &it_op : op_map) {
            if (netlist_utils::is_non_tensix_op(it_op.second.type)) {
                continue;
            }

            auto op = it_op.second.my_op;
            for(int rr = 0; rr < op -> get_grid_shape().r; rr++) {
                for(int cc = 0; cc < op -> get_grid_shape().c; cc++) {
                    log_assert(op -> cores[rr][cc]->get_logical_absolute_col_id() < worker_grid_size.x && op -> cores[rr][cc]->get_logical_absolute_row_id() < worker_grid_size.y, "Op {} for graph {} does not fit on device!", it_op.first, graph.first);
                }
            }
        }
    }
}

// Various constraints that must be respected, check them early.
void tt_runtime::check_netlist_constraints(){

    // Multichip: check target_device requirements
    check_input_queue_target_devices();

    // Multichip: Check multichip p2p addr translation requirements for each queue.
    check_queue_remote_addresses();

    check_host_mapped_queues_only_on_memory_mapped_devices();
    check_host_mapped_queue_addresses();

    check_dram_buffers_do_not_exceed_memory();
    // For each Op in each graph, check that it fits on device (given grid location and shape)
    check_graphs_fit_on_device();
    // For each target device, check that IO queues don't overlap with binaries reserved space in DRAM.
    if (!config.skip_overlap_checks) {
        check_dram_queues_do_not_overlap_reserved();
        workload.check_queues_do_not_overlap();
    }
}

void tt_runtime::create_graphs_and_init_queues() {

    log_assert(!loader->enable_epoch_preloading || loader->enable_epoch_caching, "Epoch cache preloading feature requires epoch-caching to be enabled.");
    int num_threads = tt::cpuset::get_allowed_num_threads();
    log_debug(tt::LogRuntime, "create_graphs_and_init_queues() -- num_threads: {}", num_threads);

    // Create graph resources for unique graphs in the netlist workload
    // note netlist program may run graphs in arbitrary order, not matching compiled graph id ordering
    tt::parallel_for(
        workload.graph_order.begin(), workload.graph_order.end(),
        [&](const std::string &graph_name) {
            const tt_digraph &graph = this->workload.graphs.at(graph_name);
            log_assert(graph_name == graph.my_graph_info.name, "Graph name mismatch.");
            create_graph_program(graph.my_graph_info);
        }, num_threads);

    update_graph_overlay_binaries();

    log_assert(loader->graph_to_epoch_map.size() >= workload.graphs.size(), "Expected graph_to_epoch_map size >= number of graphs in workload");
    log_debug(tt::LogRuntime, "create_graphs_and_init_queues created {} unique graph programs", workload.graph_order.size());
    for (auto &graph : workload.graph_order) {
        tt_epoch_program_info &epoch_program = loader->graph_to_epoch_map.at(graph);
        log_trace(tt::LogRuntime, "\t(graph-to-epoch) {} \t-> compiled epoch_id {}", epoch_program.name, epoch_program.epoch_id);
    }

    loader->send_static_binaries();
    loader->create_and_allocate_epoch_queues(this->distribute_epoch_tables);
    loader->create_and_allocate_io_queues(workload.queues);
    loader->populate_unique_epoch_trisc_binaries_map(workload.graphs);
    loader->preload_epoch_queues(workload.graphs, workload.graph_order);
    workload.identify_all_dual_view_rams();

    if (loader->enable_hw_io_queue_update) {
        loader->populate_queue_to_core_map_from_net2pipe(workload.get_dual_view_rams_map(), config.output_dir, this->workload.get_number_of_temporal_graphs(), true /* populate output queue producers */);
        loader->populate_queue_to_core_map_from_net2pipe(workload.get_dual_view_rams_map(), config.output_dir, this->workload.get_number_of_temporal_graphs(), false /* populate input queue consumers */);
    }

    if (cluster) {
        if (cluster->perf_state.get_perf_desc().is_dram_decouplings_en()) {
            log_assert(!(loader->enable_epoch_caching), "Backend opt level 0 should be used for dram decouplings");
            loader->populate_dram_decouple_config_map(workload);
        }
    }

    if (!config.skip_device_init) {
        cluster->deassert_risc_reset();

        // Wait until NCRISC initializes epoch queues in L1 to 0x0 and they are safe to use.
        // Only use timeout for silicon. Versim will be slower, especially with VCD dump.
        bool enable_timeout = (target_type == TargetDevice::Silicon || target_type == TargetDevice::Emulation) ? true : false;
        loader->wait_for_ncrisc_init_all_epoch_queues(enable_timeout);
    } else {
        cluster->deasserted_risc_reset = true; // assume already deasserted
        for (const auto& device_id : workload_target_device_ids) {
            cluster->record_device_runtime_start(device_id);
        }
    }
    if (config.tti) {
        load_parameter_and_constant_queues();
    }
}

void tt_runtime::load_parameter_and_constant_queues() {
    PROFILE_SCOPE_MS();
    log_assert(config.tti != nullptr, "TTI must be initialized to load parameter and constant queues");
    for (const auto &io : config.tti->get_graph_constant_and_parameter_names()) {
        std::vector<float> data;
        auto q_desc = get_queue_descriptor(io);
        tt_PytorchTensorDesc pytorch_tensor_desc;
        tt_TilizedTensorDesc tilized_tensor_desc;
        bool tilized = tensor_meta_to_tensor_desc(config.tti->get_tensor_meta(io), data, pytorch_tensor_desc, tilized_tensor_desc, q_desc.bufq_target_format);
        tt::io::translate_addresses(q_desc);
        if(tilized) {
            tt::io::push_input_to_device(q_desc, tilized_tensor_desc, -1, 0);
        }
        else {
            tt::io::push_input_to_device(q_desc, pytorch_tensor_desc, true, -1, 0);
        }
    }
}

void tt_runtime::drain_perf_dram_buffers(bool last_drain) {
    perf::ScopedEventProfiler profile("drain-performance-buffers");
    if (config.mode != DEVICE_MODE::CompileOnly &&
        config.perf_desc.enabled() &&
        cluster &&
        config.perf_desc.device_perf_mode != perf::PerfDumpMode::Concurrent) {

        if (config.optimization_level == 0) {
            if (!last_drain) {
                wait_for_idle({}, "drain_perf_buffers");
                log_debug(tt::LogPerfInfra, "Draining all dram perf buffers for the current program since opt-level is set to 0");
                cluster->dump_perf_counters(config.output_dir);
                modify_perf_dram_reset_mailbox(true);
            } else {
                log_warning(tt::LogPerfInfra,
                        "Skipping the finish() performance trace dump since opt-level is set to zero. In this mode postprocessor runs after every program");
            }
        } else {
            if (last_drain) {
                wait_for_idle({}, "drain_perf_buffers");
                log_debug(tt::LogPerfInfra, "Draining all dram perf buffers since all programs have finished");
                cluster->dump_perf_counters(config.output_dir);
            }
        }
    }
}


// Send end-epoch command to all tensix cores
// Wait for all ncrisc/brisc fw's to finish
// Close device and set device state to LongIdle
void tt_runtime::close_device() {
    if (cluster && config.do_shutdown())
    {
        std::function<void()> handle_close_device = [&]() {
            if (loader) {
                log_assert(!cluster->is_idle(), "Attempting to stop tensix fw when cluster device state is set to idle");
                loader->send_end_program_commands();
                // For pybuda early exit/cleanup during compile-errors, no need for wait.
                if (cluster->deasserted_risc_reset){
                    loader->flush_all_wc_epoch_queues_to_dram();
                    log_info(tt::LogRuntime, "Waiting for cluster completion");
                    cluster->wait_for_completion(config.output_dir);
                    set_runtime_state(tt_runtime_state::RunComplete);
                }
            }
            stop_debuda_server();
            stop_log_server();
            perf_set_device_end();
            cluster->close_device();
        };

        // if a debuda server has started, don't set a timeout for close device
        // as we need to wait indefinitely for the user to close the debuda server
        if (debuda_server and debuda_server->started_server()) {
            handle_close_device();
        }
        else {
            cmd_result_t result = run_function_with_timeout(handle_close_device, get_api_timeout(tt_timeout_api_type::CloseDevice), false);
            log_assert(result.success, "{}: {}", __FUNCTION__, result.message);
        }
    }
}

// Invalidate cached objects and dealloc hw tilizers for the current netlist
void tt_runtime::clear_runtime_cache() {
    tt_object_cache<tt_cluster>::clear(config.netlist_path);
    tt_object_cache<tt_runtime_workload>::clear(config.netlist_path);
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::destroy();
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::destroy();
}

// If debug server is running, allow it to continue running until user is done debugging
void tt_runtime::stop_debuda_server() {
    if (config.do_shutdown())
    {
        if (debuda_server.get() != nullptr) {
            debuda_server->wait_terminate();
        }
    }
}

// If debug server is running, stop it before closing the cluster
void tt_runtime::stop_log_server() {
    if (config.do_shutdown())
    {
        if (log_server.get() != nullptr) {
            log_server->stop();
        }
    }
}


// Read the dram performance buffers from all chips and run the postprocessor
void tt_runtime::finish_performance_trace() {

    if (config.mode != DEVICE_MODE::CompileOnly) {

        if (config.perf_desc.enabled()) {
            if (config.perf_desc.device_perf_mode != perf::PerfDumpMode::Concurrent) {
                drain_perf_dram_buffers(true);
            } else {
                wait_for_idle({}, "stop_concurrent_perf_trace");
                perf_host_scratch_buffer->stop_device_trace_poll();
                perf_report_generator->generate_all_reports();
                cluster->perf_state.set_postprocessor_executed();
                bool perf_check_passed = perf_report_generator->run_concurrent_performance_check();
                cluster->perf_state.update_perf_check(perf_check_passed);
            }
            if (config.perf_desc.run_perf_analyzer) {
                postprocess::run_performance_analyzer(cluster->perf_state, config.output_dir);
            }
        }
    }
}

void tt_runtime::run_performance_check() {
    if (config.do_run()) {
        log_assert(postprocess::check_performance_host_events(config.output_dir, cluster->perf_state), "Performance check failed for one or more instructions. Check for errors in test log for more detail");
    }
    if (config.do_run() && config.perf_desc.enabled()) {
        log_assert(cluster->perf_state.get_perf_check(), "Performance check failed for one or more instructions. Check for errors in test log for more detail");
    }
}

void tt_runtime::initialize_memory_profiler() {
    if (config.l1_profiler_en) {
        memory_profiler = std::make_unique<perf::MemoryProfiler>(load_soc_descriptors_per_chip(true), config.l1_profiler_en);
    }
}

void tt_runtime::add_graphs_to_memory_profiler() {
    if (config.l1_profiler_en and memory_profiler) {
        const unordered_map<chip_id_t, buda_soc_description> sdesc_per_chip = load_soc_descriptors_per_chip(true);
        for (uint temporal_epoch_id = 0; temporal_epoch_id < workload.get_number_of_temporal_graphs(); temporal_epoch_id++) {
            uint global_epoch_id = compiled_epochs + temporal_epoch_id;
            for (const string &graph_name : workload.get_graphs_of_temporal_graph(temporal_epoch_id)) {
                log_assert(workload.graphs.find(graph_name) != workload.graphs.end(), "{}: Unexpected non-existant graph {} in workload", __FUNCTION__, graph_name);
                const tt_digraph &graph = workload.graphs.at(graph_name);
                const chip_id_t target_device = graph.my_graph_info.target_device;
                log_assert(sdesc_per_chip.find(target_device) != sdesc_per_chip.end(), "{}: Unexpected non-existant soc descriptor for graph {} device {}", __FUNCTION__, graph_name, target_device);
                memory_profiler->add_graph(sdesc_per_chip.at(target_device), graph, global_epoch_id);
            }
        }
        memory_profiler->update_profile_stage_all_graphs_l1(perf::L1ProfileStage::Initialized);
    }
}

void tt_runtime::profile_l1_binary_buffer_reserved_sizes() {
    if (config.l1_profiler_en and memory_profiler) {
        // we don't know the actual consumed size of the binaries yet
        int placeholder_consumed_size = perf::buffer_size_undefined;
        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "NCRISC_FW",
            l1_mem::address_map::NCRISC_FIRMWARE_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::NCRISC_FIRMWARE_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );
    
        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "BRISC_FW",
            l1_mem::address_map::FIRMWARE_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::BRISC_FIRMWARE_SIZE + l1_mem::address_map::ZEROS_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );

        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "TRISC0",
            l1_mem::address_map::TRISC0_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::TRISC0_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );

        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "TRISC1",
            l1_mem::address_map::TRISC1_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::TRISC1_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );

        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "TRISC2",
            l1_mem::address_map::TRISC2_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::TRISC2_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );

        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            "RUNTIME_CONFIG",
            l1_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE,
            placeholder_consumed_size,
            l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE,
            perf::L1ProfileStage::ReservedBinaries
        );

        // if we don't compile overlay, then we don't know the actual reserved sizes of the overlay blobs
        // the sizes in op info and TT_BACKEND_OVERLAY_MAX_EXTRA_BLOB_SIZE can get overriden by pipegen if they're too large
        // keep consistent with runtime.cpp::compile_overlay
        bool overlay_compile = (config.do_compile() or need_overlay_recompile_during_run);

        memory_profiler->broadcast_add_buffer_l1(
            perf::L1BufferType::BinaryBuffer,
            perf::overlay_buffer_name,
            l1_mem::address_map::OVERLAY_BLOB_BASE,
            placeholder_consumed_size,
            // reserve default overlay size here if we will compile overlay
            // extra size will be appended accordingly when profiling pipegen
            overlay_compile ? l1_mem::address_map::OVERLAY_BLOB_SIZE : perf::buffer_size_undefined,
            perf::L1ProfileStage::ReservedBinaries
        );
        memory_profiler->update_profile_stage_all_graphs_l1(perf::L1ProfileStage::ReservedBinaries);
    }
}

void tt_runtime::profile_l1_binary_buffer_consumed_sizes() {
    if (config.l1_profiler_en and loader and memory_profiler) {
        // All hexes have physical coordinates
        perf::CoordType coord_type = perf::CoordType::Physical;
        
        // All binaries have the same FW
        // epoch_loader:send_static_binaries
        const tt_epoch_program_info &first_info = loader->graph_to_epoch_map.begin()->second;
        const std::shared_ptr<tt_epoch_binary> first_binary = first_info.binary;
        memory_profiler->broadcast_update_buffer_consumed_size_l1(
            l1_mem::address_map::NCRISC_FIRMWARE_BASE,
            first_binary->ncrisc_vec.size() * sizeof(uint32_t),
            perf::L1ProfileStage::ActualBinaries
        );

        // BRISC hex contains zeros, compare against l1_mem::address_map::BRISC_FIRMWARE_SIZE + l1_mem::address_map::ZEROS_SIZE
        memory_profiler->broadcast_update_buffer_consumed_size_l1(
            l1_mem::address_map::FIRMWARE_BASE,
            first_binary->brisc_vec.size() * sizeof(uint32_t),
            perf::L1ProfileStage::ActualBinaries
        );

        for (const string &graph_name : workload.graph_order) {
            tt_hex *hex;
            chip_id_t target_device = workload.get_graph_chip(graph_name);
            log_assert(loader->graph_to_epoch_map.find(graph_name) != loader->graph_to_epoch_map.end(), "{}: loader->graph_to_epoch_map does not have graph {}", __FUNCTION__, graph_name);
            const tt_epoch_program_info &info = loader->graph_to_epoch_map.at(graph_name);
            const std::shared_ptr<tt_epoch_binary> bin = info.binary;
            for (int hex_id = 0; hex_id < bin->number_of_tensix_hex_images(); hex_id++) {
                hex = &(bin->trisc0_bin_vec[hex_id]);
                // Part of trisc binaries get written into trisc local mem region
                // thus directly compare trisc binary size against reserved region
                memory_profiler->update_buffer_consumed_size_l1(
                    graph_name, 
                    tt_cxy_pair(target_device, hex->associated_routing_core),
                    l1_mem::address_map::TRISC0_BASE,
                    hex->hex_vec.size() * sizeof(uint32_t),
                    coord_type,
                    perf::L1ProfileStage::ActualBinaries
                );
                

                hex = &(bin->trisc1_bin_vec[hex_id]);
                memory_profiler->update_buffer_consumed_size_l1(
                    graph_name, 
                    tt_cxy_pair(target_device, hex->associated_routing_core),
                    l1_mem::address_map::TRISC1_BASE,
                    hex->hex_vec.size() * sizeof(uint32_t),
                    coord_type,
                    perf::L1ProfileStage::ActualBinaries
                );

                hex = &(bin->trisc2_bin_vec[hex_id]);
                memory_profiler->update_buffer_consumed_size_l1(
                    graph_name, 
                    tt_cxy_pair(target_device, hex->associated_routing_core),
                    l1_mem::address_map::TRISC2_BASE,
                    hex->hex_vec.size() * sizeof(uint32_t),
                    coord_type,
                    perf::L1ProfileStage::ActualBinaries
                );
                
                hex = &(bin->runtime_config_vec[hex_id]);
                memory_profiler->update_buffer_consumed_size_l1(
                    graph_name, 
                    tt_cxy_pair(target_device, hex->associated_routing_core),
                    l1_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE,
                    hex->hex_vec.size() * sizeof(uint32_t),
                    coord_type,
                    perf::L1ProfileStage::ActualBinaries
                );

                hex = &(bin->blob_bin_vec[hex_id]);
                memory_profiler->update_buffer_consumed_size_l1(
                    graph_name, 
                    tt_cxy_pair(target_device, hex->associated_routing_core),
                    l1_mem::address_map::OVERLAY_BLOB_BASE,
                    hex->hex_vec.size() * sizeof(uint32_t),
                    coord_type,
                    perf::L1ProfileStage::ActualBinaries
                );
            }
            memory_profiler->update_graph_profile_stage_l1(graph_name, perf::L1ProfileStage::ActualBinaries);
        }
    }
}

void tt_runtime::finish_l1_profiling_for_graphs() {
    if (config.l1_profiler_en and memory_profiler) {
        memory_profiler->sort_all_graph_buffers_and_check_overlap_l1();
        memory_profiler->update_profile_stage_all_graphs_l1(perf::L1ProfileStage::Done);
    }
}

void tt_runtime::create_memory_profiler_reports() {
    if (config.l1_profiler_en and memory_profiler) {
        const string l1_profile_out_dir = config.output_dir + "/" + "l1_profile";
        log_info(LogPerfPostProcess, "Writing l1 profiler report to {}", l1_profile_out_dir);
        if (fs::exists(l1_profile_out_dir)) {
            fs::remove_all(l1_profile_out_dir);
        }
        fs::create_directories(l1_profile_out_dir);
        memory_profiler->create_reports_l1(l1_profile_out_dir);
    }
}

void tt_runtime::cleanup_runtime_and_close_device() {
    bool device_still_active = false;
    if (cluster) {
        device_still_active = !cluster->is_idle();
    }
    if (device_still_active) {
        log_info(tt::LogRuntime, "Cleaning up runtime and closing device.");
        clear_runtime_cache();
        close_device();
        log_info(tt::LogRuntime, "Finished clean up successfully");
    }
    // Undo pinning for this thread, and clear state in case thread continues with new compile work.
    tt::cpuset::tt_cpuset_allocator::clear_state_and_cpuset_pins();
}

tt::DEVICE_STATUS_CODE tt_runtime::run_program(const string &program_name, const std::map<string, string> &parameters) {
    PROFILE_SCOPE(microseconds);

    try {
        perf::ScopedEventProfiler profile(perf::HostEventType::RUN_PROGRAM);
        log_info(tt::LogRuntime, "Running program '{}' with params {}", program_name, parameters);
        set_runtime_state(tt_runtime_state::RunBusy);

        log_assert(config.mode != DEVICE_MODE::CompileOnly, "CompileOnly mode cannot call run_program()!");
        netlist_program &program = workload.reset_program(program_name);

        if (cluster && config.do_run() && config.perf_desc.enabled()) {
            cluster->perf_state.initialize_for_program(
                    program ,cluster->get_sdesc_for_all_devices(),
                    workload.graphs, workload.op_to_outputs, workload.queues);
            
        }

        // Main program execution loop
        program.set_params(parameters);
        while (!program.done()) {
            run_instruction(program);
        }
        program_queue.push_back(program_name);

        // Resolve batched side effects at the end of a program
        if (config.optimization_level <= 1) {
            push_queue_side_effects(program);
        }

        // Flush Epoch CMD Queue Write Combine buffer if any cmds remain.
        loader->flush_all_wc_epoch_queues_to_dram();

        // Temporary hack for debug. wait for idle on all chips
        if (getenv("TT_BACKEND_WAIT_IDLE_END_OF_PROGRAM")){
            wait_for_idle({}, "end_of_run_program");
        }
        if (config.optimization_level == 0) {
            drain_perf_dram_buffers(false);
        }

        // End program events and checks
        workload.check_program(program_name);
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        cleanup_runtime_and_close_device();
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

void tt_runtime::perf_set_total_number_of_samples(int total_num_samples) {
    if (config.do_run()) {
        backend_profiler.set_total_input_count(total_num_samples);
        if (cluster) {
            cluster->perf_state.update_total_input_count(total_num_samples);
        }
    }
}

void tt_runtime::perf_set_device_end() {
    log_assert(!cluster->is_idle(), "Attempting to read tensix wallclock when cluster device state is set to idle");
    for (uint device_id: workload_target_device_ids) {
        uint64_t device_timestamp = 0;
        uint64_t device_end_cycle_aligned = 0;
        uint64_t host_current_timestamp = backend_profiler.get_current_timestamp();
        const perf::tt_perf_device_alignment device_alignment_info = cluster->perf_state.get_device_alignment_info();

        if (cluster && config.do_run()) {
            device_timestamp = cluster->get_core_timestamp(tt_cxy_pair(device_id, cluster -> get_first_active_worker(device_id)));
            if (device_alignment_info.device_id_to_start_cycle.find(device_id) ==
                device_alignment_info.device_id_to_start_cycle.end()) {
                log_warning(tt::LogPerfInfra, "Device {} start time not initialized for perf alignment", device_id);
                continue;
            }
            uint64_t device_start_cycle = device_alignment_info.device_id_to_start_cycle.at(device_id);
            device_end_cycle_aligned = (device_timestamp >= device_start_cycle) ?
                                device_timestamp - device_start_cycle : (UINT64_MAX - (device_start_cycle - device_timestamp));

            cluster->perf_state.update_device_alignment_info_end(device_id, device_timestamp, host_current_timestamp);
        } else {
            cluster->perf_state.update_device_alignment_info_end(device_id, 0, 0);
        }
        backend_profiler.record_device_end(host_current_timestamp, device_timestamp, device_end_cycle_aligned, device_id);
    }
    backend_profiler.generate_device_alignment_report(
        config.output_dir,
        config.perf_desc.override_perf_output_dir,
        getpid());
}

tt::DEVICE_STATUS_CODE tt_runtime::finish() {
    PROFILE_SCOPE_MS();
    try {
        log_debug(tt::LogRuntime, "runtime finish called (skip_device_shutdown={}) on pid {}", config.skip_device_shutdown, getpid());

        clear_runtime_cache();
        finish_performance_trace();
        run_netlist_analyzer(arch_name, netlist_path, cluster_descriptor_path, config.output_dir);
        if (config.do_shutdown()) {
            query_all_device_aiclks("runtime finish()");
            loader->dump_epoch_binary_cache_report(config.output_dir);
        }
        close_device();
        create_memory_profiler_reports();
        run_performance_check();
        if (!config.skip_device_shutdown) {
            set_runtime_state(tt_runtime_state::Uninitialized);
        }
        return tt::DEVICE_STATUS_CODE::Success;

    } catch (const std::exception &e) {
        log_error("{}", e.what());
        cleanup_runtime_and_close_device();
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

void tt_runtime::run_instruction(netlist_program &program) {

    log_debug(tt::LogRuntime, "Starting run_instruction() with PC: {} OpCode: {}", program.get_current_pc(), program.get_current_instruction().opcode);

    // Run instruction with an execution callback which gets called whenever an execute command happens
    program.run_instruction_with_callbacks(
        [this](netlist_program &program) { this->pre_instrn_instruction_callback(program); },
        [this](netlist_program &program) { this->execute_instruction_callback(program); },
        [this](netlist_program &program) { this->post_instrn_instruction_callback(program); });

};

void tt_runtime::pre_instrn_instruction_callback(netlist_program &program) {
    std::string prog_name = program.get_name();
    if (cluster && config.do_run() && config.perf_desc.enabled()) {
        cluster->perf_state.update_executed_instr(program.get_name(), program.get_current_pc());
    }
}

void tt_runtime::execute_instruction_callback(netlist_program &program) {
    std::string prog_name = program.get_name();
    tt_instruction_info instrn = program.get_current_instruction();
    workload.bind_queue_field_vars(instrn.queue_settings, prog_name);
    this->run_execute_instrn(program, instrn);
}

void tt_runtime::post_instrn_instruction_callback(netlist_program &program) {
    std::string prog_name = program.get_name();
    tt_instruction_info instrn = program.get_current_instruction();

    if (instrn.opcode == INSTRUCTION_OPCODE::Execute) {
        std::map<string, std::vector<tt_queue_setting_info>> pending;
        tt_queue_header_mask update_mask = {tt_queue_header_mask::RD_PTR_MASK};
        std::unordered_map<int, int> devices_to_sync;
        if (!loader->enable_hw_io_queue_update) {
            std::set<string> out_e2e_qs = workload.collect_output_e2e_queues(instrn.graph_name);
            for (const string& e2e_queue : out_e2e_qs) {
                const auto &queue_wrap =  workload.queues.at(e2e_queue);
                // Check if consumer graphs have pending queue updates
                for (const string &e2e_consumer : queue_wrap.my_consumer_info.programs) {
                    tt_runtime_queue_ptrs_wrap &qptrs_wrap = workload.get_qptrs_wrap(e2e_consumer);
                    // Check if e2e queue has pending settings that can be pushed
                    log_trace(tt::LogRuntime, "{} output e2e_queue = {}, consumer = {}, pending settings={}", instrn.graph_name, e2e_queue, e2e_consumer, qptrs_wrap.pending_queue_settings.count(e2e_queue));
                    if (qptrs_wrap.pending_queue_settings.count(e2e_queue)) {
                        const int e2e_q_device = queue_wrap.my_queue_info.target_device;
                        const int instrn_device = loader->graph_to_epoch_map.at(instrn.graph_name).target_device;
                        if (e2e_q_device == instrn_device) {
                            log_trace(tt::LogRuntime, "\toutput e2e queue '{}' pop from '{}' is scheduled", e2e_queue, e2e_consumer);
                            if (loader->get_epoch_ctrl(e2e_q_device).is_queue_in_use(queue_wrap.my_queue_info)) {
                                devices_to_sync.insert({e2e_q_device, 1});
                            }
                            pending[e2e_consumer].push_back(qptrs_wrap.pending_queue_settings.at(e2e_queue));
                            qptrs_wrap.pending_queue_settings.erase(e2e_queue);
                        } else {
                            log_trace(tt::LogRuntime,
                                "Graph '{}' has a cross-chip e2e queue '{}' waiting to be popped. "
                                "Deferred to the consumer epoch to pop to avoid chip to chip serialization (will deadlock if not sufficiently buffered).",
                                instrn.graph_name, e2e_queue);
                        }
                    }
                }
            }
            for (const auto &d : devices_to_sync) {
                wait_for_idle({d.first}, "pop-e2e-queue", d.second);
            }
            for (const auto &p : pending) {
                auto &pending_program = workload.programs.at(p.first);
                auto &pending_settings = p.second;
                loader->update_io_queue_settings(workload.queues, workload.get_dual_view_rams_map(), pending_settings, pending_program.get_variables(), update_mask);

            }
        }

    }
    else if (instrn.opcode == INSTRUCTION_OPCODE::VarInst)
    {
        std::string var_name = get<0>(instrn.vars[0]);
        tt_runtime_queue_ptrs_wrap &qptrs_wrap = workload.get_qptrs_wrap(prog_name);

        // When looping on device, add varinst instruction to pending list for resolve/device-update later.
        if (program.get_curr_loop_is_on_device()  && (program.get_curr_loop_curr_iter() != -1)) {

            tt_queue_varinst_update_field_mask update_field_mask;
            auto field_types = qptrs_wrap.get_var_field_types(var_name);
            update_field_mask.set_fields(field_types);

            if (update_field_mask.not_empty()) {
                workload.add_pending_varinst_update_queues(prog_name, instrn, update_field_mask);
            }
        } else {
            if (qptrs_wrap.is_field_type_var(tt_queue_header_field::GlobalRdptr, var_name)) {
                workload.add_pending_update_queues(prog_name, var_name);

                // Immediately resolve side effects and push updates
                if (config.optimization_level == 0) {
                    push_queue_side_effects(program);
                }
            }
        }

    }
    else if (instrn.opcode == INSTRUCTION_OPCODE::AllocateQueue)
    {

        // The feature combo is untested, may have issues. Avoid for now.
        log_assert(!program.get_curr_loop_is_on_device(), "{} has Dynamic Queues, currently not supported with program looping on device.", prog_name);
        struct dynamic_alloc_info {
            std::unordered_set<string> qs_to_dealloc;
            std::map<string, tt_queue_wrap> qs_to_alloc;
        };
        // ordered for deterministic behavior
        std::set<int> devices_to_sync;
        std::map<int, dynamic_alloc_info> dynamic_alloc_info_map;

        bool allocate_on_host = !loader->enable_hw_io_queue_update;

        // Issue 602, cross-chip allocate not supported on device yet
        for (const auto &var : instrn.vars) {
            string q_name = std::get<0>(var);
            allocate_on_host |= workload.is_cross_chip_e2e_queue(q_name);
        }
        // Collect alloc and dealloc queues
        for (const auto &var : instrn.vars) {
            string q_name = std::get<0>(var);
            int target_device = workload.queues.at(q_name).my_queue_info.target_device;
            std::set<string> qs_to_sync = workload.collect_overlapped_queues(q_name);

            // Filter down alloc/dealloc sets by device to reduce the number of cores participating in the sync
            dynamic_alloc_info_map[target_device].qs_to_alloc.insert({q_name, workload.queues.at(q_name)});
            dynamic_alloc_info_map[target_device].qs_to_dealloc.insert(qs_to_sync.begin(), qs_to_sync.end());

            // Issue #2513 - Not supported currently. Unclear if this use case make sense.
            const auto &dual_view_rams = workload.get_dual_view_rams_map();
            log_assert(dual_view_rams.find(q_name) == dual_view_rams.end(), "Dynamically allocated dual view RAM currently not supported");

            // Use coarse-grain per device sync from host and create queue (performed later)
            if (qs_to_sync.size() > 0) {
                log_debug(tt::LogRuntime, "allocate queue = {} overlaps with dyamic queues: {}", q_name, fmt::join(qs_to_sync, ", "));
                devices_to_sync.insert(target_device);
            }
        }
        // Sync and allocate queues
        if (allocate_on_host) {
            if (devices_to_sync.size() > 0)
                wait_for_idle(devices_to_sync, "allocate-queue");
            for (const auto &d : dynamic_alloc_info_map) {
                const auto &info = d.second;
                loader->create_and_allocate_io_queues(info.qs_to_alloc);
            }
        } else {
            for (const auto &d : dynamic_alloc_info_map) {
                auto &info = d.second;
                // auto &ctrl = loader->get_epoch_ctrl(d.first);
                // while (!ctrl.all_not_full_dram()) { /* still full, try again */}
                loader->send_allocate_queue_commands(info.qs_to_alloc, info.qs_to_dealloc, true);
            }
        }
        // Safe to deallocate overlapped queues since alloc triggered a sync with device
        for (const auto &d : dynamic_alloc_info_map) {
            const auto &info = d.second;
            for (const auto &q : info.qs_to_dealloc)
                workload.deallocate_queue(q, prog_name, true);
            for (const auto &q : info.qs_to_alloc)
                workload.allocate_queue(q.first, prog_name);
        }
    }
    else if (instrn.opcode == INSTRUCTION_OPCODE::DeallocateQueue)
    {
        for (const auto &var : instrn.vars) {
            string q_name = std::get<0>(var);
            // Cannot deallocate until runtime is synchronized with device
            workload.deallocate_queue(q_name, prog_name, false);
        }
    }
    else if (instrn.opcode == INSTRUCTION_OPCODE::EndLoop)
    {
        // Resolve batched side effects at the end of a loop
        if (loader->enable_hw_io_queue_update) {
            push_queue_side_effects(program);
        }

        if (loader->enable_looping_on_device) {
            // Denote end of on-device looping by issuing endloop commands on very first loop iteration's EndLoop instr.
            if (program.is_loop_on_device_stack_first_iter()) {
                log_debug(tt::LogRuntime, "INSTRUCTION_OPCODE::EndLoop looping on device first iter, sending LoopEnd commands to epoch queue.");
                // If loop had epoch_id aliasing hazard that was avoided, need to also avoid it on transition between iterations
                // ie (saw epoch_id:0->epoch_id:31 within loop iter, now need to protect against epoch_id:31 (curr iter) -> epoch_id:0 (next iter)
                loader->avoid_loop_on_device_epoch_id_aliasing();
                loader->send_loop_end_command(prog_name);

                // Flush all Write Combine buffers as soon as possible, at first iteration's EndLoop.
                loader->flush_all_wc_epoch_queues_to_dram();
            }

            // On final iter of current loop, check varinst-on-device assumptions by ensuring host and device are in sync for queue header.
            if (program.is_loop_on_device_last_iter()) {
                loader->check_io_queue_rdptrs_varinst_on_device(workload.queues, instrn.queue_settings, program.get_variables(),
                workload.get_dual_view_rams_map(), prog_name + "_last_iter");
            }

            // If final iter, and no other loops on device, signal to loader that looping on device is finished.
            if (program.is_loop_on_device_stack_last_iter()) {
                loader->set_in_loop_on_device(false);
            }
        } else {
            loader->flush_all_wc_epoch_queues_to_dram();
        }

    }
    else if (instrn.opcode == INSTRUCTION_OPCODE::Loop)
    {
        // If looping on device is allowed, optionally enable it for this loop and send cmd to device.
        if (loader->enable_looping_on_device) {
            int loop_count = program.get_variable(instrn.loop_count).value;
            enable_loop_on_device_if_eligible(program, loop_count);

            // Denote start of on-device looping by issuing loop commands on very first loop iteration's Loop instr.
            if (program.is_loop_on_device_stack_first_iter()) {
                log_debug(tt::LogRuntime, "INSTRUCTION_OPCODE::Loop looping on device first iter, sending LoopStart commands to epoch queue.", loop_count);
                loader->set_in_loop_on_device(true);
                loader->send_loop_start_command(loop_count);
            }
        }
    }
}


void tt_runtime::sync_on_execute_dependencies(netlist_program &program, tt_instruction_info &instrn) {
    // atomicity guarantee of a temporal epoch requires us to only sync on the device we're about to exeucte on
    // ie. epochs within the same temporal epoch must launch together, else we may deadlock due to a launch and dataflow dependency loop
    this->wait_for_idle({workload.get_graph_chip(instrn.graph_name)}, "sync-on-execute");
}

void tt_runtime::run_execute_instrn(netlist_program &program, tt_instruction_info &instrn) {
    log_assert(instrn.opcode == INSTRUCTION_OPCODE::Execute, "Expected instruction op code Execute");
    std::string graph_name  = instrn.graph_name;
    std::string prog_name   = program.get_name();
    tt_queue_header_mask update_mask = {tt_queue_header_mask::GLOBAL_RD_PTR_MASK | tt_queue_header_mask::LOCAL_SETTINGS_MASK};

    // Skip dispatch if inside loop on device
    if (program.is_loop_on_device_stack_not_first_iter()) {
        log_debug(tt::LogRuntime, "\tSkipping Host launch of graph '{}' iter: {} since already looping on device.", graph_name, program.get_curr_loop_curr_iter());
        return;
    }

    perf::ScopedEventProfiler profile(perf::HostEventType::RUN_EXECUTE_INSTRUCTION);

    log_assert(workload.graphs.find(graph_name) != workload.graphs.end(), "Could not find graph to run.");

    int input_count     = workload.graphs[graph_name].my_graph_info.input_count;
    int device_id       = workload.graphs.at(graph_name).my_graph_info.target_device;
    int epoch_id        = loader->graph_to_epoch_map[graph_name].epoch_id;
    auto queue_settings = workload.collect_temporal_epoch_instance_queue_settings(program, device_id);

    log_debug(tt::LogRuntime, "\tRunning graph '{}', epoch_id = {}, input_count = {}, pc = {}, device_id = {}, queue_settings.size() = {}",
        graph_name, epoch_id, input_count, program.get_current_pc(), device_id, queue_settings.size());

    if (!loader->enable_hw_io_queue_update && loader->check_resource_overlap(workload.queues, queue_settings, program.get_variables())) {
        this->sync_on_execute_dependencies(program, instrn);
    }

    // Update queue headers with queue settings specific to the current 'Execute' graph instrn
    // For temporal epochs with re-use of queues among graphs, calling this multiple times should be safe
    // since the queue settings will be empty for the execute statements that aren't the first of the temporal
    // epoch - all queue settings for the whole temporal epoch are updated when visiting the first execute in the group
    loader->update_io_queue_settings(workload.queues, workload.get_dual_view_rams_map(), queue_settings, program.get_variables(), update_mask);

    loader->mark_io_queues_in_use(workload.queues, queue_settings);
    if (loader->graph_name_to_queue_decouplings.find(graph_name) != loader->graph_name_to_queue_decouplings.end()) {
        update_queue_header_dram_decouplings(graph_name, false);
    }

    // To enable checking for first loop in program, must be delayed until after IO queues updated, to support case where queues shared between programs.
    if (program.is_loop_on_device_stack_first_iter()) {
        loader->check_io_queue_rdptrs_varinst_on_device(workload.queues, queue_settings, program.get_variables(),
            workload.get_dual_view_rams_map(), graph_name + "_first_iter");
    }

    // If this single graph uses both views of dual view ram, ensure that their read and write patterns across inputs
    // do not overlap. Due to nature of program-looping-on-device currently (qs_cache updated for all iters) at once
    // this check will only check first loop iter. Can run at lower optimization level without program looping if needed.
    // #1887 - Disabling checker - it fires with pybuda KV Cache. Probably needs to consider data dependencies in graph.
    // if (loader->enable_runtime_hazard_checks) {
    //     check_for_dual_view_ram_rd_wr_overlap_in_graph(graph_name);
    // }

    loader->send_epoch_program(graph_name, false);

    if (loader->graph_name_to_queue_decouplings.find(graph_name) != loader->graph_name_to_queue_decouplings.end()) {
        update_queue_header_dram_decouplings(graph_name, true);
    }
    if (config.perf_desc.overlay_decouplings.size() > 0) {
        perf_overlay_decouplings_update_epoch_command_start(device_id);
    }
}

void tt_runtime::update_queue_header_dram_decouplings(string graph_name, bool reset) {
    if (loader->graph_name_to_queue_decouplings.find(graph_name) == loader->graph_name_to_queue_decouplings.end()) {
        return;
    }
    this->wait_for_idle({workload.get_graph_chip(graph_name)}, "sync-for-dram-decouplings");
    loader->write_dram_decouplings_to_queue_headers(workload, graph_name, reset);
}

void tt_runtime::push_queue_side_effects(netlist_program &program) {
    std::string prog_name = program.get_name();
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = workload.get_qptrs_wrap(prog_name);

    tt_queue_header_mask update_mask = {tt_queue_header_mask::RD_PTR_MASK};
    if (qptrs_wrap.has_pending()) {
        perf::ScopedEventProfiler profile(perf::HostEventType::PUSH_QUEUE_SIDE_EFFECTS);
        log_trace(tt::LogRuntime, "resolve and push {} queue side effects, pc = {}", qptrs_wrap.pending_queue_settings.size(), program.get_current_pc());
        std::unordered_map<int, std::vector<tt_queue_setting_info>> pending;
        std::set<int> devices;
        for (auto &it : qptrs_wrap.pending_queue_settings) {
            int device_id = workload.queues.at(it.second.name).my_queue_info.target_device;
            devices.insert(device_id);
            pending[device_id].push_back(it.second);
        }

        for (int device_id: devices) {
            bool sync_on_device = !loader->enable_hw_io_queue_update;
            if (sync_on_device && (loader->check_resource_overlap(workload.queues, pending.at(device_id), {}))) {
                wait_for_idle({device_id}, "push-queue-side-effects");
            }

            loader->update_io_queue_settings(workload.queues, workload.get_dual_view_rams_map(), pending.at(device_id), program.get_variables(), update_mask);
        }
        qptrs_wrap.pending_queue_settings.clear();
    }

    if (qptrs_wrap.has_pending_varinst()) {
        perf::ScopedEventProfiler profile(perf::HostEventType::PUSH_QUEUE_SIDE_EFFECTS);

        // If first loop on device, send cmds and update qs_cache for all iterations at once.
        if (program.is_loop_on_device_stack_first_iter()) {
            int num_iterations = program.get_curr_loop_last_iter() + 1;
            loader->update_io_queue_rdptrs_varinst_on_device(qptrs_wrap, workload, program.get_variables(), program.get_name(), num_iterations);
        }

        qptrs_wrap.pending_varinst_queue_updates.clear();
    }
}

std::vector<tt_dram_io_desc> tt_runtime::get_host_input_io_desc() {
    std::vector<tt_dram_io_desc> dram_io_desc;
    for (auto &q : workload.queues) {
        tt_queue_info &queue_info = q.second.my_queue_info;
        if (queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = get_queue_descriptor(queue_info.name);
            tt::io::translate_addresses(io_desc);
            dram_io_desc.push_back(io_desc);
        }
    }
    return dram_io_desc;
}

std::vector<tt_dram_io_desc> tt_runtime::get_device_output_io_desc() {
    std::vector<tt_dram_io_desc> dram_io_desc;
    for (auto &q : workload.queues) {
        tt_queue_info &queue_info = q.second.my_queue_info;
        if (queue_info.input != "HOST") {
            tt_dram_io_desc io_desc = get_queue_descriptor(queue_info.name);
            tt::io::translate_addresses(io_desc);
            dram_io_desc.push_back(io_desc);
        }
    }
    return dram_io_desc;
}

void tt_runtime::wait_for_idle(const std::set<int> &target_devices, string caller, int cmds_thresh) {
    log_assert(config.mode != DEVICE_MODE::CompileOnly, "CompileOnly mode cannot call wait_for_idle()!");

     // If looping on device, loop stack must be empty when wait_for_idle is called.
    for (const auto &it : workload.programs) {
        log_assert(!it.second.get_curr_loop_is_on_device(), "Cannot call wait_for_idle() while loop stack not empty for program: {}", it.first);
    }

    // Target all devices used by the netlist if unspecified by user
    std::set<int> &devices = const_cast<std::set<int>&>(target_devices);
    if (target_devices.size() == 0) {
        devices = loader->target_devices;
    }
    std::stringstream s;
    s << devices;
    log_debug(tt::LogRuntime, "\tWait for idle starting on devices {}, caller = {}", s.str(), caller);

    // Flush Write-Combined Epoch Cmd Queues if needed before WFI
    for (const int dev : devices) {
        auto &ctrl = loader->get_epoch_ctrl(dev);
        ctrl.flush_all_wc_epoch_queues_to_dram();
    }

    for (const int dev : devices) {
        auto &ctrl = loader->get_epoch_ctrl(dev);
        loader->wait_for_epoch_progress(ctrl, cmds_thresh);
        log_trace(tt::LogRuntime, "\twait_for_epoch complete on device {}", dev);
    }

    log_debug(tt::LogRuntime, "\tWait for idle complete on devices {}, caller = {}", s.str(), caller);
}

tt::DEVICE_STATUS_CODE tt_runtime::wait_for_idle() {
    try {
        // wait for epoch in progress to complete
        wait_for_idle({}, "wait-for-idle-api");

        // flush any side effects to devices
        for (const std::string &p : program_queue) {
            push_queue_side_effects(workload.programs.at(p));
        }
        // clear pending programs queue
        program_queue.clear();
        if (log_server.get() != nullptr) {
            log_server->dump_debug();
        }
        log_debug(tt::LogRuntime, "Waiting for idle done, all pending programs have completed");
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
};

tt::DEVICE_STATUS_CODE tt_runtime::memory_barrier(tt::MemBarType barrier_type, chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores) {
    try {
        log_assert(!(barrier_type == MemBarType::host_device_dram || barrier_type == MemBarType::host_device_l1), "Host to Device Dram and Host to Device L1 barriers are not exposed by runtime.");
        log_assert(config.mode != DEVICE_MODE::CompileOnly, "CompileOnly mode cannot call memory_barrier()!");
        log_assert(runtime_state == tt_runtime_state::Initialized or runtime_state == tt_runtime_state::RunBusy, "memory_barrier() can only be called after runtime is initialized!");

        if(barrier_type == tt::MemBarType::device_device) {
            loader -> insert_sync_on_cores(loader -> get_epoch_ctrl(chip), cores);
        }
        else if(barrier_type == tt::MemBarType::host_cluster) {
            wait_for_idle();
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}

vector<tt::EpochPerfInfo> tt_runtime::get_perf_info() {
    if (config.perf_desc.device_perf_mode == perf::PerfDumpMode::Disable) {
        return {};
    } else if (cluster) {
        if (config.perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent) {
            if (cluster->perf_state.is_postprocessor_executed()) {
                return perf_report_generator->get_all_epoch_perf_info();
            } else {
                log_error("Attempting to read perf info but Perf-Postprocessor has not been executed");
                return {};
            }
        } else {
            return cluster->get_perf_info_all_epochs();
        }
    }
    return {};
}

unordered_map<tt_xy_pair, vector<uint64_t>> tt_runtime::get_last_epoch_kernel_runtime() {
    if (cluster) {
        return cluster->perf_state.get_last_epoch_kernel_runtime();
    } else {
        return {};
    }
}

void tt_runtime::check_runtime_data() {
    string runtime_data_path = config.output_dir + "/runtime_data.yaml";

    fs::path runtime_data(runtime_data_path);
    if (config.mode == DEVICE_MODE::RunOnly) {
        if (fs::exists(runtime_data)) {
            auto runtime_data = get_runtime_data(runtime_data_path);
            // Populate compile and run gitinfo
            std::pair<string, string> cmp_info = {"null", "null"};
            std::pair<string, string> run_info = tt::get_git_info();

            // Update compile gitinfo if present
            if (runtime_data["branch_name"])
                cmp_info.first = runtime_data["branch_name"].as<std::string>();
            if (runtime_data["commit_hash"])
                cmp_info.second = runtime_data["commit_hash"].as<std::string>();

            // Warning if compile and run gitinfo mismatch
            if (cmp_info.first != run_info.first)
                log_warning(tt::LogRuntime, "Compiled branch name '{}' does not match current run branch name '{}'", cmp_info.first, run_info.first);
            if (cmp_info.second != run_info.second)
                log_warning(tt::LogRuntime, "Compiled commit hash '{}' does not match current run commit hash '{}'", cmp_info.second, run_info.second);

            // Warning if backend targets mismatch
            tt::DEVICE type = tt::get_device_from_string(runtime_data["backend_type"].as<std::string>());
            if (type != config.type) {
                log_warning(tt::LogRuntime, "Compiled backend type '{}' does not match current run target type '{}'", tt::get_string(type), tt::get_string(config.type));
            }

            if(runtime_data["noc_translation_enabled"].as<int>() != noc_translation_enabled) {
                log_debug(tt::LogRuntime, "Recompiling overlay binaries since NOC translation is enabled on this machine.");
                need_overlay_recompile_during_run = true;
            }
            if(arch_supports_harvesting) {
                if (runtime_data["worker_grid_sizes_per_chip"] and (arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0)) {
                    // Latest Flow for WH: Ensure that compile time grid sizes match runtime grid sizes per chip (virtual harvesting is supported here)
                    log_assert(workload_target_device_ids.size() == runtime_data["worker_grid_sizes_per_chip"].size(), "Number of devices between compile and run mismatch.");
                    auto soc_descs = load_soc_descriptors_per_chip();
                    for (const auto& chip : runtime_data["worker_grid_sizes_per_chip"]) {
                        log_assert(workload_target_device_ids.find(chip.first.as<int>()) != workload_target_device_ids.end(), "Device {} was present during compile but not during runtime.", chip.first.as<string>());
                        // Overlay recompile needed compile time grid size > runtime grid size (net2pipe may have placed relay buffers outside of op grid).
                        need_overlay_recompile_during_run = (chip.second["y"].as<int>() > soc_descs[chip.first.as<int>()].worker_grid_size.y || chip.second["x"].as<int>() > soc_descs[chip.first.as<int>()].worker_grid_size.x);
                        if (need_overlay_recompile_during_run) {
                            log_warning(tt::LogRuntime, "Compile time grid size for device {} is greater than runtime grid size. Recompiling overlay binaries.", chip.first.as<string>());
                            break;
                        }
                    }
                }
                else {
                    // WH Legacy checks (grid size not in runtime data)
                    // Grayskull checks (since we need to know exact harvesting mask, due to virtual harvesting not being supported)
                    log_assert(rows_to_harvest.size() == runtime_data["harvested_rows_per_chip"].size(), "Number of devices between compile and run mismatch.");
                    for (const auto& chip : runtime_data["harvested_rows_per_chip"]) {
                        log_assert(workload_target_device_ids.find(chip.first.as<int>()) != workload_target_device_ids.end(), "Device {} was present during compile but not during runtime.", chip.first.as<string>());
                        if (arch_name == tt::ARCH::GRAYSKULL) {
                            // Harvesting masks must match exactly between compile and run for GS
                            need_overlay_recompile_during_run = (chip.second.as<int>() != rows_to_harvest.at(chip.first.as<int>()));
                            if (need_overlay_recompile_during_run) {
                                log_warning(tt::LogRuntime, "The harvesting config for device {} does not match harvesting config in compile. Recompiling overlay binaries.", chip.first.as<string>());
                                break;
                            }
                        }
                        else {
                            // Compile time grid size must be lte than runtime grid size for WH ->  Number of bits in compile harvesting mask must be gte than runtime harvesting mask
                            need_overlay_recompile_during_run = (std::bitset<32>(chip.second.as<int>()).count() < std::bitset<32>(rows_to_harvest.at(chip.first.as<int>())).count());
                            if(need_overlay_recompile_during_run) {
                                log_warning(tt::LogRuntime, "The harvesting config for device {} does not match harvesting config in compile. Recompiling overlay binaries during run.", chip.first.as<string>());
                                break;
                            }
                        }
                    }
                }
            }
            check_epoch_metadata(runtime_data);
            log_assert(using_arm_host() ? (!need_overlay_recompile_during_run || need_risc_recompile_during_run) : true, "Recompiling overlay binaries or RISC Firmware is only supported on x86 hosts.");

            // Kernel Cache feature enablement changes NCRISC FW, epoch binary layout in DRAM. Number of slots impacts BE reserved region in DRAM. Ensure they match.
            uint32_t run_info_kc_slots = dram_mem::address_map::KERNEL_CACHE_NUM_SLOTS();
            uint32_t cmp_info_kc_slots = runtime_data["kernel_cache_num_slots"] ? runtime_data["kernel_cache_num_slots"].as<uint32_t>() : 0;
            log_assert(cmp_info_kc_slots == run_info_kc_slots, "Number of Kernel Cache slots differs between compile ({}) and run ({}), not allowed.", cmp_info_kc_slots, run_info_kc_slots);

        } else {
            log_fatal("RunOnly mode requires a valid pre-compiled output dir specified, '{}' doesn't exist!", runtime_data_path);
        }
    }
}

void tt_runtime::check_epoch_metadata(const YAML::Node& runtime_data) {
    if (runtime_data["epoch_metadata"]) {
        const auto& epoch_md = runtime_data["epoch_metadata"];
        need_risc_recompile_during_run = epoch_md["epoch_q_num_slots"].as<int>() != epoch_queue::get_epoch_q_num_slots() ||
                                         epoch_md["epoch_table_dram_addr"].as<int>() != epoch_queue::get_epoch_table_dram_addr() ||
                                         epoch_md["epoch_table_entry_size_bytes"].as<int>() != epoch_queue::get_epoch_table_entry_size_bytes() ||
                                         epoch_md["epoch_alloc_q_sync_addr"].as<int>() != epoch_queue::get_epoch_alloc_queue_sync_addr() ||
                                         epoch_md["epoch_q_slot_size"].as<int>() != epoch_queue::EPOCH_Q_SLOT_SIZE ||
                                         epoch_md["epoch_q_slot_offset"].as<int>() != epoch_queue::EPOCH_Q_SLOTS_OFFSET;
    }
    else {
        need_risc_recompile_during_run = true;
    }
    // If epoch metadata does not match between compile and run, we also need to recompile overlay due to NCRISC <--> NOC dependencies
    need_overlay_recompile_during_run |= need_risc_recompile_during_run;
    if (need_risc_recompile_during_run) {
        log_warning(tt::LogRuntime, "Epoch Metadata mismatches between compile and run. Recompiling Firmware.");
    }
}

std::string tt_runtime::epoch_metadata_to_yaml() {
    std::ostringstream f;
    f << "epoch_metadata:" << std::endl;
    f << "  epoch_q_num_slots: " << epoch_queue::get_epoch_q_num_slots() << endl;
    f << "  epoch_table_dram_addr: " << epoch_queue::get_epoch_table_dram_addr() << endl;
    f << "  epoch_table_entry_size_bytes: " << epoch_queue::get_epoch_table_entry_size_bytes() << endl;
    f << "  epoch_alloc_q_sync_addr: " << epoch_queue::get_epoch_alloc_queue_sync_addr() << endl;
    f << "  epoch_q_slot_size: " << epoch_queue::EPOCH_Q_SLOT_SIZE << endl;
    f << "  epoch_q_slot_offset: " << epoch_queue::EPOCH_Q_SLOTS_OFFSET << endl;
    return f.str();
}

std::string tt_runtime::runtime_data_to_yaml() {
    const auto soc_descs = load_soc_descriptors_per_chip();
    std::ostringstream f;
    f << "arch_name: '" << tt::get_string (workload.device_info.arch) << "'" << endl;
    f << "backend_type: '" << tt::get_string(config.type) << "'" << endl;
    f << "backend_mode: '" << tt::get_string(config.mode) << "'" << endl;
    f << "netlist_path: '" << this->netlist_path << "'" << endl;
    f << "cluster_descriptor_path: '" << this->cluster_descriptor_path << "'" << endl;
    f << "distribute_epoch_tables: " << this->distribute_epoch_tables << endl;
    f << "noc_translation_enabled: " << this->noc_translation_enabled << endl;
    auto gitinfo = tt::get_git_info();
    f << "branch_name: '" << gitinfo.first << "'" << endl;
    f << "commit_hash: '" << gitinfo.second << "'" << endl;

    if (soc_descs.size() > 0) {
        f << "worker_grid_sizes_per_chip:" << endl;
        for (const auto& chip : soc_descs) {
            f << "  " << chip.first << ": " << endl;
            f << "     x: " << chip.second.worker_grid_size.x << endl;
            f << "     y: " << chip.second.worker_grid_size.y << endl;
        }
    }

    if (loader) {
        f << "graph_to_epoch_map:" << endl;
        log_debug(tt::LogRuntime, "graph_to_epoch_map: size={}", loader->graph_to_epoch_map.size());
        for (const auto& graph : loader->graph_to_epoch_map) {
            log_debug(tt::LogRuntime, "graph_to_epoch_map: graph={}", graph.first);

            f << "  " << graph.first << ": " << endl;
            f << "    epoch_id: " << graph.second.epoch_id << endl;
            f << "    target_device: " << graph.second.target_device << endl;
        }
        f << "epoch_queue_num_slots: " << epoch_queue::get_epoch_q_num_slots() << endl;
        f << "dram_epoch_metadata_limit: " << epoch_queue::DRAM_EPOCH_METADATA_LIMIT << endl;
        f << "grid_size_row: " << epoch_queue::GridSizeRow << endl;
        f << "grid_size_col: " << epoch_queue::GridSizeCol << endl;
    }

    f << "kernel_cache_num_slots: " << dram_mem::address_map::KERNEL_CACHE_NUM_SLOTS() << endl;
    f << epoch_metadata_to_yaml();
    return f.str();
}

void tt_runtime::export_runtime_data_to_yaml() {
    log_debug(tt::LogRuntime, "Saving runtime_data.yaml");

    ofstream f(config.output_dir + "/runtime_data.yaml");
    f << runtime_data_to_yaml();
    if(arch_supports_harvesting){
        //Only write harvesting config to runtime_data if we run with GS or WHB0
        if(performed_harvesting){
            f << "harvested_rows_per_chip:" << endl;
            for (const auto& chip_info : rows_to_harvest){
                f <<  "  " << chip_info.first << ": " <<  chip_info.second << endl;
            }
        }
        else{
            f << "harvested_rows_per_chip:" << endl;
            for (const auto& chip_info : workload_target_device_ids){
                f <<  "  " << chip_info << ": " <<  0 << endl;
            }
        }
    }
}

// To avoid crashing with segfault, check and see if requested silicon device resources are installed.
void tt_runtime::ensure_devices_present(const TargetDevice &target_type) {
    log_assert(target_type == TargetDevice::Versim or target_type == TargetDevice::Silicon or target_type == TargetDevice::Emulation, "Expected Versim, Emulation or Silicon Backend");

    std::vector<tt::ARCH> available_devices       = tt_cluster::detect_available_devices(target_type);
    tt_runtime_workload &workload               = *get_workload();

    if (parse_env("TT_BACKEND_SKIP_ENSURE_DEVICES_PRESENT", false)) {
        return;
    }

    if (target_type == TargetDevice::Silicon) {
        auto available_devices_for_arch = std::count(available_devices.begin(), available_devices.end(), workload.device_info.arch);
        if (workload.device_info.arch == tt::ARCH::WORMHOLE || workload.device_info.arch == tt::ARCH::WORMHOLE_B0) {
            // Not all wh/wh_b0 devices have PCI, which means they won't be detected. In this case, checking FW versions
            // on all device ids is a better indication of available silicon devices.
            log_assert(available_devices_for_arch >= 1, "Must have at least one detected WORMHOLE/WORMHOLE_B0 device.");
        } else {
            // Ensure available devices installed can cover highest device_id requested by workload.
            auto ending_device_id = *workload_target_device_ids.rbegin();

            auto num_required_devices = ending_device_id + 1;
            if (num_required_devices > available_devices_for_arch){
                log_fatal("Insufficient available silicon devices matching requested netlist arch: {}. Available: {} Requested: {} (target_device_id: {})",
                workload.device_info.arch, available_devices_for_arch, num_required_devices, ending_device_id);
            }
        }
    }
    else if (target_type == TargetDevice::Versim) {

        // Slightly different checks/asserts here for versim, given we do not have "available devices per arch" for versim.
        auto available_verim_devices = available_devices.size();

        // Ensure available devices installed can cover highest device_id requested by workload. Versim only supports one chip, so safe to keep this for GS/WH.
        auto ending_device_id = *workload_target_device_ids.rbegin();
        auto num_required_devices = ending_device_id + 1;
        if (num_required_devices > available_verim_devices){
            log_fatal("Insufficient available versim devices matching requested netlist arch: {}. Available: {} Requested: {} (target_device_id: {})",
            workload.device_info.arch, available_verim_devices, num_required_devices, ending_device_id);
        }
    }

    return;
}

// For debug purposes to query/print and optionally check AICLK at certain points.
void tt_runtime::query_all_device_aiclks(std::string loc) {
    if (target_type == TargetDevice::Silicon){

        // MT Initial BH - Remove AICLK query due to lack of ARC message support
        if (arch_name != tt::ARCH::BLACKHOLE) {
            // Env-var for AICLK checking purposes only (not setting)
            int aiclk_target = parse_env("TT_BACKEND_CHECK_TARGET_AICLK", 0);

            std::map<int, int> freq_all_devices = cluster->get_all_device_aiclks();
            for (auto &device: freq_all_devices){
                auto device_id = device.first;
                auto aiclk = device.second;
                if (aiclk_target > 0 && aiclk < aiclk_target){
                    log_fatal("Observed AICLK: {} for device_id: {} less than target_aiclk: {} at {}", aiclk, device_id, aiclk_target, loc);
                }else{
                    log_debug(tt::LogRuntime, "Observed AICLK: {} for device_id: {} matches/exceeds target aiclk: {} at {}", aiclk, device_id, aiclk_target, loc);
                }
            }
        }
        
    }
}

void tt_runtime::assign_global_epoch_ids(bool increment) {
    for (int epoch = 0; epoch < workload.get_number_of_temporal_graphs(); epoch++) {
        int global_epoch_id = compiled_epochs + epoch;
        const auto &names = workload.get_graphs_of_temporal_graph(epoch);
        for (const auto &name : names) {
            if (graph_to_epoch_id.find(name) == graph_to_epoch_id.end())
            graph_to_epoch_id[name] = global_epoch_id;
        }
    }
    if (increment) {
        compiled_epochs += workload.get_number_of_temporal_graphs();
    }
}



// Set current loopstack loop as looping on device if conditions are met.
void tt_runtime::enable_loop_on_device_if_eligible(netlist_program &program, int loop_count){
    const int min_loop_iterations = 2;
    if ((loop_count >= min_loop_iterations) || loader->get_in_loop_on_device()) {
        program.set_curr_loop_is_on_device();
        log_debug(tt::LogRuntime, "Enabling program looping on device for Loop({}) in {} at pc: {}.", loop_count, program.get_name(), program.get_current_pc());
    }
}

// For graphs that use both read/write view of Dual View Rams, ensure read and write access patterns for a given execute of graph do not
// overlap in any way, otherwise it would be a potential race condition. Several pre-reqs here are computed at compile time, but actual
// check depends on state of io queue header values, so must be done at runtime, for dual view rams shared by single program if any exist.
void tt_runtime::check_for_dual_view_ram_rd_wr_overlap_in_graph(std::string &graph_name) {

    PROFILE_SCOPE(microseconds);

    const auto &queues = workload.queues;
    const auto &dual_view_rams = workload.get_dual_view_rams_map();
    const auto &single_graph_dual_view_ram_reader_writer = workload.get_single_graph_dual_view_ram_reader_writer(graph_name);
    const auto &input_count = workload.graphs.at(graph_name).my_graph_info.input_count;

    // Perform overlap checking between Reader view and Writer view on Reader view, to avoid doing it twice.
    if (!single_graph_dual_view_ram_reader_writer.first.empty() && !single_graph_dual_view_ram_reader_writer.second.empty()) {

        const auto &reader_q_name = single_graph_dual_view_ram_reader_writer.first;
        const auto &writer_q_name = single_graph_dual_view_ram_reader_writer.second;

        const auto &reader_queue_info   = queues.at(reader_q_name).my_queue_info;
        const auto &qs_cache_header     = loader->get_epoch_ctrl(reader_queue_info.target_device).get_cached_io_qs(reader_queue_info);
        const auto &writer_queue_info   = queues.at(writer_q_name).my_queue_info;
        uint32_t reader_entry_size      = get_entry_size_in_bytes(reader_queue_info, workload.has_tilized_data(reader_q_name));
        uint32_t writer_entry_size      = get_entry_size_in_bytes(writer_queue_info, workload.has_tilized_data(writer_q_name));

        log_trace(tt::LogRuntime,
            "{} : InputCount: {} [Reader: {} GlobalRdptr: {} Autoinc: {} EntrySize: {}]"
            " and [Writer: {} GlobalWrPtr: {} Autoinc: {} EntrySize: {}]",
            __FUNCTION__, input_count, reader_q_name, qs_cache_header.global_rd_ptr, qs_cache_header.global_rdptr_autoinc, reader_entry_size,
            writer_q_name, qs_cache_header.global_wr_ptr, qs_cache_header.global_wrptr_autoinc, writer_entry_size);

        std::vector<buffer_range_t> reader_ranges, writer_ranges;

        // Compute start and end regions per reader and write view entry.
        auto read_entry_start   = reader_entry_size * (qs_cache_header.global_rd_ptr);
        auto read_entry_end     = reader_entry_size * (qs_cache_header.global_rd_ptr + 1);
        auto read_autoinc_amt   = reader_entry_size * (qs_cache_header.global_rdptr_autoinc);
        auto write_entry_start  = writer_entry_size * (qs_cache_header.global_wr_ptr);
        auto write_entry_end    = writer_entry_size * (qs_cache_header.global_wr_ptr + 1);
        auto write_autoinc_amt  = writer_entry_size * (qs_cache_header.global_wrptr_autoinc);

        // Record range for this input's reader/writer view, and increment for next input.
        for (int i=0; i<input_count; i++) {
            reader_ranges.emplace_back(reader_q_name, 0, 0, 0, read_entry_start, read_entry_end);
            read_entry_start += read_autoinc_amt;
            read_entry_end += read_autoinc_amt;

            writer_ranges.emplace_back(writer_q_name, 0, 0, 0, write_entry_start, write_entry_end);
            write_entry_start += write_autoinc_amt;
            write_entry_end += write_autoinc_amt;
        }

        // Now that all ranges are calculated, check for overlap on potentnially non-contiguous rd/wr ranges.
        int found_overlaps = 0;
        for (int i=0; i<reader_ranges.size(); i++) {
            for (int j=0; j<writer_ranges.size(); j++) {
                if (workload.check_buffer_overlap(reader_ranges.at(i), writer_ranges.at(j))) {
                    found_overlaps += 1;
                    log_warning(tt::LogRuntime,
                        "Detected Dual View RAM single-graph: {} (input_count: {}) access pattern overlap between "
                        "Reader [{} grdptr: {} autoinc: {} input_idx: {} entry_size: {} bytes] and "
                        "Writer [{} gwrptr: {} autoinc: {} input_idx: {} entry_size: {} bytes]",
                        graph_name, input_count,
                        reader_q_name, qs_cache_header.global_rd_ptr, qs_cache_header.global_rdptr_autoinc, i, reader_entry_size,
                        writer_q_name, qs_cache_header.global_wr_ptr, qs_cache_header.global_wrptr_autoinc, j, writer_entry_size);
                    log_warning(tt::LogRuntime, "Reader Range: {}", reader_ranges.at(i));
                    log_warning(tt::LogRuntime, "Writer Range: {}", writer_ranges.at(j));

                }
            }
        }

        log_assert(!found_overlaps,
            "Detected {} overlaps in Dual View RAM access patterns shared by single graph {}. This is not allowed. reader: {} writer: {}. See above warnings.",
            found_overlaps, graph_name, reader_q_name, writer_q_name);

    }
}

void tt_runtime::merge_compile_results(tt_compile_result* result, const tt_fw_compile_result& fw_compile_result,
                                       const tt_overlay_compile_result& overlay_compile_result) {
    if (!result) {
        return;
    }
    result->overlay_compile_result = overlay_compile_result;
    result->fw_compile_result = fw_compile_result;
    if (!fw_compile_result.success) {
        result->success = false;
        result->failure_type = fw_compile_result.failure_type;
        result->failure_message = fw_compile_result.failure_message;
    } else if (!overlay_compile_result.success) {
        result->success = false;
        result->failure_type = overlay_compile_result.failure_type;
        result->failure_message = overlay_compile_result.failure_message;
        result->failure_target = overlay_compile_result.failure_target;
    }
}

void tt_runtime::compile_firmware(tt_fw_compile_result& fw_compile_result) {
    try {
        if (!(config.do_compile() or config.perf_desc.always_compile() or need_risc_recompile_during_run)) {
            return;
        }
        // When (re)compiling RISC binaries, we aren't compiling for perf decoupling runs.
        bool compile_for_perf_only = !(need_risc_recompile_during_run || config.do_compile());
        generate_all_fw(
            workload,
            get_arch_str(arch_name),
            config.perf_desc,
            compile_for_perf_only,
            config.output_dir);

        // Perf descriptor might have been modified inside generate_all_fw
        if (cluster) {
            cluster->perf_state.set_perf_desc(config.perf_desc);
        }
    } catch (std::exception &e) {
        fw_compile_result.success = false;
        fw_compile_result.failure_type = COMPILE_FAILURE::Firmware;
        fw_compile_result.failure_message = e.what();
    }
}

void tt_runtime::compile_overlay(tt_overlay_compile_result& overlay_compile_result) {
    try {
        // Static compile of firmware/kernel binaries and overlay
        if (config.do_compile() or need_overlay_recompile_during_run) {
            // (Re)compile overlay binaries
            // For Grayskull, pass in the full device descriptor (net2pipe will not allocate any memory/structures outside of op grid, which is guaranteed to fit inside the harvested grid)
            // For multichip WH, pass in SOC descriptors per chip (similar to Pipegen). Net2pipe can allocate ethernet relay buffers outside of op grid and needs accurate SOC dimensions here.
            std::string n2p_soc_desc_for_harvested_wh = config.output_dir + "/device_descs_for_net2pipe.yaml";
            std::string n2p_soc_desc_for_unharvested_wh_or_gs = config.output_dir + "/device_desc.yaml";
            string sdesc_to_use = (arch_name == tt::ARCH::GRAYSKULL or !fs::exists(n2p_soc_desc_for_harvested_wh)) ? n2p_soc_desc_for_unharvested_wh_or_gs : n2p_soc_desc_for_harvested_wh;
            run_net2pipe(netlist_path, config.output_dir, compiled_epochs, sdesc_to_use, cluster_descriptor_path, overlay_compile_result);
            
            if (!overlay_compile_result.success) {
                // net2pipe compilation failed, we don't want to proceed further
                return;
            }

            overlay_compile_result = create_graph_overlay_binaries();
        } 
        assign_global_epoch_ids(true);
    } catch (const std::exception &e) {
        // We are only going to catch exceptions here that are not something we 
        // expected - net2pipe, pipegen and blobgen runs should fill the structure
        // overlay_compile_result properly even in case of exception
        overlay_compile_result.success = false;
        overlay_compile_result.failure_type = COMPILE_FAILURE::Invalid;
        overlay_compile_result.failure_message = e.what();
    }
}