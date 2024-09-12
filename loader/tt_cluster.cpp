// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_cluster.hpp"

#include <algorithm>

#include "common/error_types.hpp"
#include "common/io_lib.hpp"
#include "common/env_lib.hpp"
#include "device/tt_silicon_driver_common.hpp"
#include "l1_address_map.h"
#include "runtime/runtime_types.hpp"
#include "runtime/runtime_utils.hpp"
#include "tlb_config.hpp"

extern perf::tt_backend_perf backend_profiler;
std::string cluster_desc_path = "";
std::chrono::seconds tt_cluster::get_device_timeout() {
    int default_timeout = (type == TargetDevice::Silicon) ? 500 : 3600;  // seconds
    int device_timeout = tt::parse_env("TT_BACKEND_TIMEOUT", default_timeout);
    return std::chrono::seconds{device_timeout};
}

std::chrono::seconds tt_cluster::get_device_duration() {
    high_resolution_clock::time_point device_current_time;
    device_current_time = high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(device_current_time - device_reset_time);
    return duration;
}

int tt_cluster::get_num_chips() {
    return device -> get_number_of_chips_in_cluster();
}

std::unordered_set<chip_id_t> tt_cluster::get_all_chips() {
    return device -> get_all_chips_in_cluster();
}

tt_xy_pair tt_cluster::get_routing_coordinate(int core_r, int core_c, chip_id_t device_id) const {
    log_assert(sdesc_per_chip.size(), "Descriptor must be loaded. Try open_device()");
    tt_xy_pair coord(0,0);
    coord.x = sdesc_per_chip.at(device_id).worker_log_to_routing_x.at(core_c);
    coord.y = sdesc_per_chip.at(device_id).worker_log_to_routing_y.at(core_r);

    return coord;
}

static bool check_dram_core_exists(const std::vector<std::vector<tt_xy_pair>> &all_dram_cores, tt_xy_pair target_core) {
    bool dram_core_exists = false;
    for (const auto &dram_cores_in_channel : all_dram_cores) {
        for (auto dram_core : dram_cores_in_channel) {
            if (dram_core.x == target_core.x && dram_core.y == target_core.y) {
                return true;
            }
        }
    }
    return false;
}

map<tt_cxy_pair, vector<uint32_t>> tt_cluster::get_perf_events() {
    log_assert(!is_idle(), "Attempting to read the performance counters when cluster device state is set to idle");
    map<tt_cxy_pair, vector<uint32_t>> all_dram_events;
    for (auto device_id : target_device_ids) {
        log_debug(tt::LogRuntime, "Reading perf events from dram for device {}", device_id);
        const int num_bytes_per_event = 4;
        for (auto &dram_bank_to_workers : get_soc_desc(device_id).get_perf_dram_bank_to_workers()) {
            const int perf_buf_this_bank_addr = dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_BASE;
            int core_x = dram_bank_to_workers.first.x;
            int core_y = dram_bank_to_workers.first.y;
            tt_cxy_pair core = tt_cxy_pair(device_id, core_x, core_y);
            log_assert(check_dram_core_exists(get_soc_desc(device_id).dram_cores, dram_bank_to_workers.first), "DRAM core does not exist");

            int perf_buf_size = dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_SIZE;

            int num_events_total_this_bank = perf_buf_size / num_bytes_per_event;
            std::vector<uint32_t> dram_bank_events;
            read_dram_vec(
                dram_bank_events, core, perf_buf_this_bank_addr, perf_buf_size);
            log_assert(dram_bank_events.size() == num_events_total_this_bank, "Number of events in DRAM bank does not match expected value");
            log_assert(all_dram_events.find(core) == all_dram_events.end(), "Cannot find events for core {}", core.str());
            all_dram_events.insert({core, dram_bank_events});

        }
    }
    return all_dram_events;
}
// Check if the cluster descriptor is populated. If not, generate and cache it.
std::string tt_cluster::get_cluster_desc_path(const std::string& root) {
    std::string custom_cluster_desc_path = "";
    if (!root.size()) {
        custom_cluster_desc_path = tt::buda_home() + "/cluster_desc.yaml";
    }
    else {
        custom_cluster_desc_path = root + "/cluster_desc.yaml";
    }
    if (!fs::exists(custom_cluster_desc_path) or !cluster_desc_path.size()) {
        // Either the cluster descriptor has not been generated or not been populated in the dir specified by root.
        if (cluster_desc_path.size() && fs::exists(cluster_desc_path)) {
            // Cluster desc in root does not exist but has been previously generated and stored in cluster_desc_path.
            // Copy cluster desc to root.
            fs::copy(cluster_desc_path, custom_cluster_desc_path);
        }
        else {
            // Cluster desc has not been generated. 
            // Generate and store it in out_dir.
            if (detect_available_devices(tt::TargetDevice::Silicon)[0] == tt::ARCH::GRAYSKULL) {
                create_cluster_desc_for_gs(root);
            }
            // MT Initial BH - Use GS-like cluster desc
            else if (detect_available_devices(tt::TargetDevice::Silicon)[0] == tt::ARCH::BLACKHOLE) {
                // Use simple cluster desc with one chip, no eth
                create_cluster_desc_for_gs(root);
            }
            else {
                generate_cluster_desc_yaml(root);
            }
            cluster_desc_path = custom_cluster_desc_path;
        }
    }
    return custom_cluster_desc_path;
}

void tt_cluster::create_cluster_desc_for_gs(const std::string& root) {
    log_debug(tt::LogRuntime, "Creating custom cluster desc for GS");
    auto available_device_ids = tt_SiliconDevice::detect_available_device_ids();
    ofstream f(root + "/cluster_desc.yaml");

    f << "chips: {\n}\n";
    f << "ethernet_connections: [\n]\n";
    f << "harvesting: [\n]\n";
    f << "chips_with_mmio: [\n";

    for (int i = 0; i < available_device_ids.size(); i++) {
        f << std::to_string(i) << ": " << std::to_string(available_device_ids[i]) << ", \n";
    }
    f << "]";

    f.close();
}

// Return vector of device names detected, used by API function. Assumes all devices are the same arch.
std::vector<tt::ARCH> tt_cluster::detect_available_devices(const TargetDevice &target_type, bool only_detect_mmio){
    static std::vector<tt::ARCH> available_devices = {}; // Static to act as cache for repeat queries to avoid device interation.
    static std::vector<tt::ARCH> available_remote_devices = {}; // Cache the remote devices as well
    log_assert(target_type == TargetDevice::Versim or target_type == TargetDevice::Silicon or target_type == TargetDevice::Emulation, "Expected device type Silicon, Versim or Emulation.");

    if (target_type == TargetDevice::Silicon) {
        if (available_devices.size() == 0){
            log_trace(tt::LogRuntime, "Going to query MMIO mapped silicon device for detect_available_devices()");

            std::vector<chip_id_t> available_device_ids = tt_SiliconDevice::detect_available_device_ids();
            int num_available_devices = available_device_ids.size();

            if (num_available_devices > 0){
                auto detected_arch_name = detect_arch(available_device_ids.at(0));
                if (detected_arch_name != tt::ARCH::Invalid){
                    tt::ARCH arch_name = static_cast<tt::ARCH>(detected_arch_name);
                    available_devices.insert(available_devices.end(), num_available_devices, arch_name);
                } else{
                    log_info(tt::LogRuntime, "Silicon device arch name was detected as Invalid.");
                }
            } else{
                log_info(tt::LogRuntime, "No silicon devices detected.");
            }
        }

        // We don't need the cluster descriptor for:
        // 1. If available_devices = {}, then there are no MMIO devices -> offline compile
        // 2. GS, since all devices are MMIO mapped
        // 3. the user asks for only mmio
        // 4. already cached non-mmio devices
        if(available_devices.size() != 0 && available_devices.at(0) != tt::ARCH::GRAYSKULL && !only_detect_mmio && available_remote_devices.size() == 0) {
            log_trace(tt::LogRuntime, "Generating and querying cluster descriptor for remote devices in detect_available_devices()");
            char temp[] = "/tmp/temp_cluster_desc_dir_XXXXXX";
            char *dir_name = mkdtemp(temp);
            get_cluster_desc_path(dir_name);
            std::unique_ptr<tt_ClusterDescriptor> ndesc = tt_ClusterDescriptor::create_from_yaml(cluster_desc_path);
            ndesc -> get_all_chips();
            int num_mmio_chips = available_devices.size();

            for(int i = num_mmio_chips; i < ndesc -> get_all_chips().size(); i++) {
                available_remote_devices.push_back(available_devices.at(0));
            }
        }
    } else if(target_type == TargetDevice::Versim){
        if (available_devices.size() == 0){
            log_trace(tt::LogRuntime, "Going to query versim device for detect_available_devices()");
            int num_devices = tt_VersimDevice::detect_number_of_chips();
            available_devices.insert(available_devices.end(), num_devices, tt::ARCH::Invalid); // Name is not important.
        }
    } else if(target_type == TargetDevice::Emulation){
        if (available_devices.size() == 0){
            log_trace(tt::LogRuntime, "Going to query Emulation device for detect_available_devices()");
            int num_devices = tt_emulation_device::detect_number_of_chips();
            available_devices.insert(available_devices.end(), num_devices, tt::ARCH::Invalid); // Name is not important.
        }
    }

    // return mmio devices for:
    // 1. not silicon
    // 2. offline compile
    // 3. grayskull
    // 4. caller only asks for mmio
    if(target_type != TargetDevice::Silicon || available_devices.size() == 0 || available_devices.at(0) == tt::ARCH::GRAYSKULL || only_detect_mmio) {
        return available_devices;
    }
    else {
        std::vector<tt::ARCH> all_devices_in_cluster = available_devices;
        all_devices_in_cluster.insert(all_devices_in_cluster.end(), available_remote_devices.begin(), available_remote_devices.end());
        return all_devices_in_cluster;
    }
}

int extract_chip_id_from_sdesc_path(const fs::path& sdesc_path){
    string file = sdesc_path.filename().string();
    return atoi(file.substr(0, file.find(".")).c_str());
}

std::unordered_map<chip_id_t, buda_SocDescriptor> get_buda_desc_from_tt_desc(const std::unordered_map<chip_id_t, tt_SocDescriptor>& input) {
    std::unordered_map<chip_id_t, buda_SocDescriptor> rval = {};
    for(const auto it : input) {
        rval.emplace(it.first, buda_SocDescriptor(it.second));
    }
    return rval;
}

void tt_cluster::open_device(
    const tt::ARCH &arch,
    const TargetDevice &target_type,
    const std::set<chip_id_t> &target_devices,
    const std::string &sdesc_path,
    const std::string &ndesc_path,
    const bool& perform_harvesting,
    const bool &skip_driver_allocs,
    const bool &clean_system_resources,
    const std::string& output_dir,
    const std::unordered_map<chip_id_t, uint32_t>& harvesting_masks) {
    target_device_ids = target_devices;
    if (target_type == TargetDevice::Versim) {
        device = std::make_shared<tt_VersimDevice>(sdesc_path, ndesc_path);
    } else if (target_type == TargetDevice::Emulation) {
        device = std::make_shared<tt_emulation_device>(sdesc_path);
    } else if (target_type == TargetDevice::Silicon) {

        // This is the target/desired number of mem channels per arch/device. Silicon driver will attempt to open
        // this many hugepages as channels, and assert if workload uses more than available.
        uint32_t num_host_mem_ch_per_mmio_device = arch == tt::ARCH::GRAYSKULL ? 1 : 4;

        // For silicon driver, filter mmio devices to use only mmio chips required by netlist workload, to allow sharing
        // of resource (reservation/virtualization) like GS where cluster desc only contains netlist workload devices.
        device = std::make_shared<tt_SiliconDevice>(sdesc_path, ndesc_path, target_devices, num_host_mem_ch_per_mmio_device, tt::tlb_config::get_dynamic_tlb_config(arch), skip_driver_allocs, clean_system_resources, perform_harvesting, harvesting_masks);
        device -> set_driver_host_address_params(host_address_params);
        device -> set_driver_eth_interface_params(eth_interface_params);
    }
    device -> set_device_l1_address_params(l1_fw_params); // Need this for Silicon and Versim
    device -> set_device_dram_address_params(dram_fw_params);
    type = target_type;
    log_assert(type == TargetDevice::Versim or type == TargetDevice::Silicon or type == TargetDevice::Emulation, "Expected device type Silicon, Versim or Emulation.");

    generate_soc_descriptors_and_get_harvesting_info(arch, output_dir);
    
    if (device) {
        log_assert(get_num_chips() >= 1, "Must have at least one detected chip available on device!");
    }
    cluster_arch = arch;
}

void tt_cluster::generate_soc_descriptors_and_get_harvesting_info(const tt::ARCH& arch, const std::string& output_dir) {
    performed_harvesting = device -> using_harvested_soc_descriptors();
    harvested_rows_per_target = device -> get_harvesting_masks_for_soc_descriptors();
    noc_translation_en = device -> noc_translation_en();

    sdesc_per_chip = get_buda_desc_from_tt_desc(device -> get_virtual_soc_descriptors());
    
    if((noc_translation_en or performed_harvesting) and output_dir.size()) {
        // Emit SOC Descriptor files for runtime to pick up
        std::string runtime_soc_desc_dir = output_dir + "/device_descs/";
        std::string overlay_soc_desc_dir = "";
        if(noc_translation_en && (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0)) {
            runtime_soc_desc_dir = output_dir + "/device_desc_runtime/";
            overlay_soc_desc_dir = output_dir + "/device_descs/";
            if(!fs::exists(overlay_soc_desc_dir)) fs::create_directories(overlay_soc_desc_dir);
        }
        
        if(!fs::exists(runtime_soc_desc_dir)) fs::create_directories(runtime_soc_desc_dir);

        for(auto& chip : sdesc_per_chip) {
            stringstream runtime_desc_name;
            runtime_desc_name << runtime_soc_desc_dir << chip.first << ".yaml";
            generate_soc_description(&(chip.second), runtime_desc_name.str(),{static_cast<int>(chip.second.grid_size.y), static_cast<int>(chip.second.grid_size.x)});
            if(overlay_soc_desc_dir.size()) {
                translate_workers_and_eth(this, chip.first, runtime_desc_name.str(), output_dir);
                // translate_workers_and_eth(this, target_device_ids, runtime_desc_name.str(), output_dir);
            }
        }
    }
}

std::map<int, int> tt_cluster::get_all_device_aiclks(){
    log_assert(!is_idle(), "Attempting to read device aiclk when cluster device state is set to idle");
    return device->get_clocks();
}

int tt_cluster::get_device_aiclk(const chip_id_t &chip_id) {
    if (target_device_ids.find(chip_id) != target_device_ids.end()) {
        return device->get_clocks().at(chip_id);
    }
    return 0;
}

void tt_cluster::start_device(const tt_device_params &device_params) {
    log_assert(sdesc_per_chip.size(), "Descriptor must be loaded. Try open_device()");
    log_assert(device != nullptr ,  "Device not initialized, make sure compile is done before running!");
    auto device_params_ = device_params;

    if(type == TargetDevice::Silicon && device_params.init_device) {
        for(auto& device_id : device -> get_target_mmio_device_ids()) {
            tt::tlb_config::configure_static_tlbs(cluster_arch, device_id, get_soc_desc(device_id), device);
        }
    }
    device -> start_device(device_params);

    state = tt_cluster_state::Running;
    device_reset_time = high_resolution_clock::now();
    // TODO: Add a TLB verification function here to ensure that tlbs are correctly setup
}

void tt_cluster::close_device() {
    device -> close_device();
    device.reset();
    sdesc_per_chip.clear();
    // wait for all background threads to finish
    for (auto &thread : background_threads) {
        thread.join();
    }
    background_threads.clear();
    state = tt_cluster_state::Idle;
    log_debug(tt::LogRuntime, "Closed all devices successfully");
}

uint64_t tt_cluster::get_core_timestamp(tt_cxy_pair target_core) {
    vector<uint32_t> clock_event_lo;
    vector<uint32_t> clock_event_hi;
    read_dram_vec(clock_event_lo, target_core, l1_mem::address_map::WALL_CLOCK_L, 4, false, true);
    read_dram_vec(clock_event_hi, target_core, l1_mem::address_map::WALL_CLOCK_H, 4, false, true);
    uint64_t timestamp = (uint64_t(clock_event_hi[0]) << 32) + clock_event_lo[0];
    return timestamp;
}

void tt_cluster::memory_barrier(tt::MemBarType barrier_type, const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores) {
    if(barrier_type == tt::MemBarType::host_device_dram) {
        device -> dram_membar(chip, "SMALL_READ_WRITE_TLB", cores);
    }
    else if(barrier_type == tt::MemBarType::host_device_l1) {
        device -> l1_membar(chip, "SMALL_READ_WRITE_TLB", cores);
    }
}

void tt_cluster::memory_barrier(tt::MemBarType barrier_type, const chip_id_t chip, const std::unordered_set<uint32_t>& target_channels) {
    log_assert(barrier_type == tt::MemBarType::host_device_dram, "Can only specify target_channels in memory_barrier for Host to Device DRAM syncs");
    device -> dram_membar(chip, "SMALL_READ_WRITE_TLB", target_channels);
}
void tt_cluster::wait_for_completion(std::string output_dir) {
    vector<uint32_t> mem_vector;
    std::map<int, unordered_set<tt_xy_pair>> device_idle_cores;
    bool all_worker_cores_done;

    // initially assume no cores are idle
    for (const int device_id : target_device_ids) {
        device_idle_cores.insert({device_id, {}});
    }
    do {
        all_worker_cores_done = true;
        for (const int device_id : target_device_ids) {
            for (const tt_xy_pair &core : get_soc_desc(device_id).workers) {
                // check for core busy
                bool is_core_busy = device_idle_cores.at(device_id).find(core) == device_idle_cores.at(device_id).end();
                if (is_core_busy) {
                    // check for core done
                    read_dram_vec(mem_vector, tt_cxy_pair(device_id, core), l1_mem::address_map::NCRISC_FIRMWARE_BASE + 4, 4);
                    bool is_core_done = (mem_vector.at(0) == 1) or (mem_vector.at(0) == 0xabcd1234);
                    if (is_core_done) {
                        device_idle_cores.at(device_id).insert(core);
                        // check for stream assertions
                        read_dram_vec(mem_vector, tt_cxy_pair(device_id, core), l1_mem::address_map::FIRMWARE_BASE + 4, 4);
                        if (mem_vector.at(0) == 0xdeeeaaad) {
                            log_fatal("Device {} stream assertions detected from core {}-{}", device_id, core.x, core.y);
                        }
                    } else {
                        log_trace(tt::LogRuntime, "Device {} completion signal not received from core {}-{}", device_id, core.x, core.y);
                        all_worker_cores_done = false;
                    }
                }
            }
        }
    } while (!all_worker_cores_done);
    state = tt_cluster_state::PendingTerminate;
}

tt_xy_pair tt_cluster::get_first_active_worker(const chip_id_t &device_id) {
    return sdesc_per_chip.at(device_id).workers.at(0);
}

void tt_cluster::record_device_runtime_start(int device_id) {
    uint64_t host_current_timestamp = backend_profiler.get_current_timestamp();
    uint64_t device_current_timestamp = get_core_timestamp(tt_cxy_pair(device_id, get_first_active_worker(device_id)));
    // log_info(tt::LogAlways, "Device current timestamp: {}", device_current_timestamp);
    backend_profiler.record_device_start(host_current_timestamp, device_current_timestamp, device_id);
    perf_state.update_device_alignment_info_start(device_id, device_current_timestamp, host_current_timestamp);
}

void tt_cluster::deassert_risc_reset() {
    device_reset_time = high_resolution_clock::now();
    if (get_device_timeout().count() > 0) {
        log_info(tt::LogRuntime, "Starting device status monitor with TIMEOUT={}s", get_device_timeout().count());
        background_threads.emplace_back(std::thread(&tt_cluster::monitor_device_timeout, this));
    }
    for(const chip_id_t &device_id : target_device_ids) {
        record_device_runtime_start(device_id);
    }
    device -> deassert_risc_reset();
    deasserted_risc_reset = true;
}

void tt_cluster::monitor_device_timeout() {
    int timeout = get_device_timeout().count();

    // Timeout checking frequency scaled by timeout value (max 30s, min 1s)
    int check_internal = timeout / 10;
    check_internal = std::min(check_internal, 30);
    check_internal = std::max(check_internal, 1);

    while (true) {
        // check if we should terminate the loop
        if (state == tt_cluster_state::PendingTerminate) {
            break;
        }
        // check if device timeout has been reached
        int elapsed = get_device_duration().count();
        if (elapsed > timeout) {
            if (tt::io::info.output_dir != "") {
                dump_debug_mailbox(tt::io::info.output_dir);  // device diagnostics
            }
            std::string error_msg = "Device runtime exceeded timeout of " + std::to_string(timeout) + " seconds, a possible hang is detected.";
            std::string reset_call = tt::parse_env<std::string>("TT_BACKEND_RESET_CALL_ON_TIMEOUT", "");
            if (not reset_call.empty()) {
                log_info(tt::LogRuntime, "Resetting device with command: {}", reset_call);
                auto ret = std::system(reset_call.c_str());
                log_info(tt::LogRuntime, "Result: {}", ret);
            }
            log_error(
                "{} TT_BACKEND_TIMEOUT=<value> envvar can be used to increase the timeout.",
                error_msg,
                timeout);
            throw tt::error_types::timeout_error(error_msg);
        }
        // spin on check interval unless terminate signal is received
        double spin_seconds = 0;
        while (state != tt_cluster_state::PendingTerminate) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            spin_seconds += 0.1;
            if (spin_seconds >= (double)check_internal) {
                break;
            }
        }
        log_trace(tt::LogRuntime, "Device has been running for {}s, timeout is {}s", elapsed, timeout);
    }
}

void tt_cluster::dump_debug_mailbox(std::string output_dir) {
    log_assert(device != nullptr, "Device not initialized, make sure compile is done before running!");
    for (auto device_id: target_device_ids) {
        string perf_output_path = postprocess::get_perf_out_directory(output_dir, perf_state.get_perf_desc().override_perf_output_dir, false);
        string output_path = perf_output_path + "/debug_mailbox_device_";
        output_path += to_string(device_id) + ".yaml";
        log_info(tt::LogRuntime, "Reading debug mailbox for device {}, output yaml path {}", device_id, output_path);
        std::ofstream output_file(output_path);

        std::vector<std::string> debug_mailboxes = {"T0", "T1", "T2", "Ncrisc"};

        const int mailbox_base_addr = l1_mem::address_map::DEBUG_MAILBOX_BUF_BASE;
        const int mailbox_size = l1_mem::address_map::DEBUG_MAILBOX_BUF_SIZE;
        for (auto &worker_core : get_soc_desc(device_id).workers) {
            int core_x = worker_core.x;
            int core_y = worker_core.y;
            std::string core_id = std::to_string(core_x) + "-" + std::to_string(core_y);
            output_file << core_id << ":" << std::endl;
            vector<uint64_t> all_threads_runtime;
            int thread_idx = 0;
            for (auto thread : debug_mailboxes) {
                output_file << "    " << thread << ":" << std::endl;
                const int mailbox_thread_base_addr = mailbox_base_addr + thread_idx * mailbox_size;
                std::vector<uint32_t> mailbox_events;
                read_dram_vec(
                    mailbox_events, tt_cxy_pair(device_id, core_x, core_y), mailbox_thread_base_addr, mailbox_size);
                // Number of events returned must be the mailbox size divided by event size (4B)
                log_assert(mailbox_events.size() == mailbox_size / 4, "Unexpected number of events returned to mailbox");
                vector<uint32_t> kernel_runtime_events(mailbox_events.end() - 2, mailbox_events.end());
                uint64_t thread_runtime = (kernel_runtime_events.at(0) & 0xffffffff) |
                                            ((uint64_t(kernel_runtime_events.at(1)) << 32) & 0xffffffff);
                all_threads_runtime.push_back(thread_runtime);

                // cout << "last epoch kernel runtime for core " << worker_core.str() << " thread " << thread_idx << " = " << all_threads_runtime.at(thread_idx) << endl;
                thread_idx++;
                for (auto event : mailbox_events) {
                    // The debug mailbox registers are 16b each
                    output_file << "        - " << (event & 0xffff) << std::endl;
                    output_file << "        - " << ((event >> 16) & 0xffff) << std::endl;
                }
            }
            perf_state.update_last_epoch_kernel_runtime(worker_core, all_threads_runtime);
        }
    }
}

void tt_cluster::dump_perf_counters(std::string output_dir) {
    bool perf_check_passed = true;
    if (perf_state.get_perf_desc().dump_debug_mailbox) {
        dump_debug_mailbox(output_dir);
    }
    if (perf_state.get_perf_desc().enabled()) {
        const map<tt_cxy_pair, vector<uint32_t>> all_dram_events = get_perf_events();
        perf_check_passed = postprocess::run_perf_postprocess(all_dram_events, perf_state, output_dir, device, sdesc_per_chip);
    }
    perf_state.update_perf_check(perf_check_passed);
}


vector<tt::EpochPerfInfo> tt_cluster::get_perf_info_all_epochs() {
    if (perf_state.is_postprocessor_executed()) {
        return perf_state.get_all_epochs_perf_info();
    } else {
        log_error("Attempting to read perf info but Perf-Postprocessor has not been executed");
        return vector<tt::EpochPerfInfo>();
    }
}

void tt_cluster::wait_for_non_mmio_flush() {
    if (type == TargetDevice::Silicon) {
        perf::ScopedEventProfiler profile(__FUNCTION__);
        device -> wait_for_non_mmio_flush();
    }
}

inline tt_cxy_pair tt_cluster::get_dram_core(const tt_target_dram &dram) {
    int chip_id, d_chan, d_subchannel;
    std::tie(chip_id, d_chan, d_subchannel) = dram;
    buda_soc_description& desc_to_use = get_soc_desc(chip_id);
    log_assert(d_chan < (desc_to_use.dram_cores).size(), "Trying to address dram channel that doesnt exist in the device descriptor");
    log_assert(d_subchannel < (desc_to_use.dram_cores).at(d_chan).size(), "Trying to address dram sub channel that doesnt exist in the device descriptor");
    return tt_cxy_pair(chip_id, desc_to_use.get_core_for_dram_channel(d_chan, d_subchannel));
}

void tt_cluster::write_dram_vec(const void* ptr, const uint32_t size, tt_cxy_pair dram_core, uint64_t addr, bool small_access, bool send_epoch_cmd, bool last_send_epoch_cmd, bool register_txn, bool ordered_with_prev_remote_write) {
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    device -> write_to_device(ptr, size, dram_core, addr, tlb_to_use, send_epoch_cmd, last_send_epoch_cmd, ordered_with_prev_remote_write);
}

void tt_cluster::write_dram_vec(const void* ptr, const uint32_t size, const tt_target_dram &dram, uint64_t addr, bool small_access, bool send_epoch_cmd, bool last_send_epoch_cmd, bool register_txn, bool ordered_with_prev_remote_write)
{
    write_dram_vec(ptr, size, get_dram_core(dram), addr, small_access, send_epoch_cmd, last_send_epoch_cmd, register_txn, ordered_with_prev_remote_write);
}

void tt_cluster::write_dram_vec(vector<uint32_t> &vec, const tt_target_dram &dram, uint64_t addr, bool small_access, bool send_epoch_cmd, bool last_send_epoch_cmd, bool register_txn, bool ordered_with_prev_remote_write)
{
    write_dram_vec(vec, get_dram_core(dram), addr, small_access, send_epoch_cmd, last_send_epoch_cmd, register_txn, ordered_with_prev_remote_write);
}

void tt_cluster::broadcast_write_to_all_tensix_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access, bool register_txn) {
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    std::set<chip_id_t> chips_to_exclude = {};
    if(cluster_arch == tt::ARCH::GRAYSKULL) {
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude_for_grayskull_tensix_broadcast, 
                                            cols_to_exclude_for_grayskull_tensix_broadcast, tlb_to_use);
    }
    else if (cluster_arch == tt::ARCH::BLACKHOLE) {
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude_for_blackhole_tensix_broadcast, 
                                            cols_to_exclude_for_blackhole_tensix_broadcast, tlb_to_use);
    }
    else {
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude_for_wormhole_tensix_broadcast, 
                                            cols_to_exclude_for_wormhole_tensix_broadcast, tlb_to_use);
    }
    
}

void tt_cluster::broadcast_write_to_all_eth_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access, bool register_txn) {
    // FIXME MT: Revisit this check for Blackhole
    log_assert(cluster_arch == tt::ARCH::WORMHOLE or cluster_arch == tt::ARCH::WORMHOLE_B0, "Can only broadcast to ethernet cores on Wormhole devices.");
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    std::set<chip_id_t> chips_to_exclude = {};
    device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude_for_wormhole_eth_broadcast, 
                                        cols_to_exclude_for_wormhole_eth_broadcast, tlb_to_use);
}

void tt_cluster::broadcast_write_to_all_dram_in_cluster(const vector<uint32_t>& vec, uint64_t addr, bool small_access, bool register_txn) {
    std::set<chip_id_t> chips_to_exclude = {};
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    if(cluster_arch == tt::ARCH::GRAYSKULL) {
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude_for_grayskull_dram_broadcast, 
                                            cols_to_exclude_for_grayskull_dram_broadcast, tlb_to_use);
    }
    else if (cluster_arch == tt::ARCH::BLACKHOLE) {
        std::set<uint32_t> rows_to_exclude = {};
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude, 
                                            cols_to_exclude_for_blackhole_dram_broadcast, tlb_to_use);
    }
    else {
        // DRAM Group 1 on WH (column 5)
        std::set<uint32_t> rows_to_exclude = {};
        device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude, cols_to_exclude_for_wormhole_dram_broadcast, tlb_to_use);
    }
}

void tt_cluster::broadcast_write_to_cluster(const vector<uint32_t>& vec, uint64_t addr, std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, 
                                            bool small_access, bool register_txn) {
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    device -> broadcast_write_to_cluster(vec.data(), vec.size() * 4, addr, chips_to_exclude, rows_to_exclude, cols_to_exclude, tlb_to_use);
}

void tt_cluster::write_rolled_dram_vec(vector<uint32_t> &vec, uint32_t unroll_count, const tt_target_dram &dram, uint64_t addr, bool small_access, bool register_txn)
{
    write_rolled_dram_vec(vec, unroll_count, get_dram_core(dram), addr, small_access, register_txn);
}

void tt_cluster::read_dram_vec(vector<uint32_t> &vec, const tt_target_dram &dram, uint64_t addr, uint32_t size, bool small_access, bool register_txn)
{
    read_dram_vec(vec, get_dram_core(dram), addr, size, small_access, register_txn);
}

void tt_cluster::write_dram_vec(vector<uint32_t> &vec, tt_cxy_pair dram_core, uint64_t addr, bool small_access, bool send_epoch_cmd, bool last_send_epoch_cmd, bool register_txn, bool ordered_with_prev_remote_write)
{
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    device -> write_to_device(vec.data(), vec.size() * sizeof(uint32_t), dram_core, addr, tlb_to_use, send_epoch_cmd, last_send_epoch_cmd, ordered_with_prev_remote_write);
}

void tt_cluster::write_rolled_dram_vec(vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair dram_core, uint64_t addr, bool small_access, bool register_txn)
{
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_WRITE_TLB");
    device -> rolled_write_to_device(vec, unroll_count, dram_core, addr, tlb_to_use);
}

void tt_cluster::read_dram_vec(vector<uint32_t> &vec, tt_cxy_pair dram_core, uint64_t addr, uint32_t size, bool small_access, bool register_txn){
    std::string tlb_to_use = register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_READ_TLB");
    device -> read_from_device(vec, dram_core, addr, size, tlb_to_use);
}

void tt_cluster::write_core_vec(vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr) {
    write_dram_vec(vec, core, addr);
}

void tt_cluster::read_core_vec(vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size) {
    read_dram_vec(vec, core, addr, size);
}

void tt_cluster::write_sysmem_vec(const void* mem_ptr, uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    device -> write_to_sysmem(mem_ptr, size, addr, channel, src_device_id);
}

void tt_cluster::write_sysmem_vec(vector<uint32_t> &vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id)
{
    device -> write_to_sysmem(vec.data(), vec.size() * sizeof(uint32_t), addr, channel, src_device_id);
}

void tt_cluster::read_sysmem_vec(vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id)
{
    // TODO: ADD vec resizing here
    device->read_from_sysmem(vec, addr, channel, size, src_device_id);
}

void tt_cluster::send_hex_to_dram(tt_hex *hex, bool small_access, bool send_epoch_cmd, bool last_send_epoch_cmd) {
    tt_target_dram dram = {hex->d_chip_id, hex->d_chan, hex->d_subchannel};
    // Can we set small_acess=true here, or must set on caller of this for epoch cmds only.
    write_dram_vec(hex->hex_vec, dram, hex->d_addr, small_access, send_epoch_cmd, last_send_epoch_cmd);
}

void tt_cluster::read_hex_from_dram(tt_hex *hex) {
    tt_target_dram dram = {hex->d_chip_id, hex->d_chan, hex->d_subchannel};
    read_dram_vec(hex->rd_hex_vec, dram, hex->d_addr, hex->hex_vec.size()*4);
}

void tt_cluster::send_hex_to_core(tt_hex *hex, tt_cxy_pair core) {
    write_dram_vec(hex->hex_vec, core, hex->d_addr);
}

void tt_cluster::read_hex_from_core(tt_hex *hex, tt_cxy_pair core) {
    read_dram_vec(hex->rd_hex_vec, core, hex->d_addr, hex->hex_vec.size()*4);
}

void tt_cluster::check_hex_from_dram(tt_hex *hex, std::string name) {
    vector<uint32_t> rd_vec;
    tt_target_dram dram = {hex->d_chip_id, hex->d_chan, hex->d_subchannel};
    read_dram_vec(rd_vec, dram, hex->d_addr, hex->hex_vec.size()*4);

    vector<uint32_t> zerovec(rd_vec.size(), 0);
    // if (rd_vec == zerovec) { cout << "WARNING: Firmware " << name << " image reads back all zeros" << endl; }
    if (rd_vec != hex->hex_vec) {
        cout << "Firmware " << name << " image reads back WRONG" << endl;
        cout << "binary observed = " << rd_vec << endl;
        cout << "binary expected = " << hex->hex_vec << endl;
        log_fatal("Expected and Observed Epoch Binaries don't match.");
    }
}

void *tt_cluster::channel_address(std::uint32_t offset, const tt_target_dram &dram) {
    return device->channel_address(offset, get_dram_core(dram));
}

void *tt_cluster::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    return device->host_dma_address(offset, src_device_id, channel);
}

void tt_cluster::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
    device -> translate_to_noc_table_coords(device_id, r, c);
}

tt_version tt_cluster::get_ethernet_fw_version() {
    return device -> get_ethernet_fw_version();
}

std::ostream &operator<<(std::ostream &os, tt_target_dram const &dram) {
    os << "Target DRAM chip = " << std::get<0>(dram) << ", chan = " << std::get<1>(dram) << ", subchan = " << std::get<2>(dram);
    return os;
}

std::unique_ptr<buda_soc_description> load_soc_descriptor_from_file(const tt::ARCH &arch, std::string file_path) {
    log_assert(file_path != "", "soc-descriptor file path must be populated");
    return load_soc_descriptor_from_yaml(file_path);
}

tt_SiliconDevice* DebudaIFC::get_casted_device() {
    return dynamic_cast<tt_SiliconDevice *> (m_cluster->device.get());
}

std::set<chip_id_t> DebudaIFC::get_target_device_ids() {
    return m_cluster->target_device_ids;
}
