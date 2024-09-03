// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include <sstream>

#include "runtime.hpp"
#include "common/param_lib.hpp"
#include "runtime_utils.hpp"
#include "epoch_loader.hpp"
#include "l1_address_map.h"
#include "dram_address_map.h"
#include "host_mem_address_map.h"
#include "eth_l1_address_map.h"
#include "src/net2pipe/inc/net2pipe_constants.h"
#include "device/tt_cluster_descriptor.h"

namespace fs = std::experimental::filesystem; // see comment above
using ClusterDescription = tt_ClusterDescriptor;

// Private namespace to avoid polluting the global namespace
namespace {
    vector<string> get_params_to_ignore_vec(YAML::Node reference_param_desc){
        bool found_params_to_ignore = false;
        for(auto it = reference_param_desc.begin(); it != reference_param_desc.end(); it++){
            if((it -> first).as<string>() == "params_to_ignore") found_params_to_ignore = true;
        }
        vector<string> params_to_ignore = {};
        if(found_params_to_ignore) params_to_ignore = reference_param_desc["params_to_ignore"].as<vector<string>>();
        return params_to_ignore;
    }

    void validate_reference_against_params(YAML::Node& reference_param_desc, string param_type, std::map<std::string, std::string> &params){
        vector<string> params_to_ignore = get_params_to_ignore_vec(reference_param_desc);

        for(auto it = reference_param_desc[param_type].begin(); it != reference_param_desc[param_type].end(); it++){
            if(params.find((it -> first).as<string>()) != params.end() and find(params_to_ignore.begin(), params_to_ignore.end(), (it -> first).as<string>()) == params_to_ignore.end()){
                if((it -> second).as<string>() != params[(it -> first).as<string>()]){
                    log_warning(tt::LogRuntime, "{} does not match between runtime_params reference and the database query. From reference file: {}     From database: {}", (it -> first).as<string>(), (it -> second).as<string>(), params[(it -> first).as<string>()]);
                    params[(it -> first).as<string>()] = (it -> second).as<string>();
                }
            }
        }
    }

    const std::map<tt::ARCH, std::set<chip_id_t>> &get_arch_to_silicon_device_ids_map() {
        static std::map<tt::ARCH, std::set<chip_id_t>> arch_to_silicon_device_ids_map;
        // If the map is empty, populate it
        if (arch_to_silicon_device_ids_map.size() == 0) {
            auto devices = tt_cluster::detect_available_devices(TargetDevice::Silicon, false);
            for (chip_id_t device_id = 0; device_id < devices.size(); device_id++)
            {
                if (devices.at(device_id) != tt::ARCH::Invalid) {
                    const tt::ARCH arch = static_cast<tt::ARCH>(devices.at(device_id));
                    arch_to_silicon_device_ids_map[arch].insert(device_id);
                }
            }
        }
        return arch_to_silicon_device_ids_map;
    }

    std::set<chip_id_t> get_present_device_ids(const tt::ARCH &arch){
        const auto &arch_to_devices = get_arch_to_silicon_device_ids_map();
        // If the arch is not found in the map, then no devices were detected
        if (arch_to_devices.find(arch) == arch_to_devices.end())
            return {};
        return arch_to_devices.at(arch);
    }

    std::vector<tt::ARCH> get_arch_vec(const std::string &user_desc) {
        std::vector<tt::ARCH> arch_vec;
        if (user_desc != "") {
            arch_vec = {load_soc_descriptor_from_yaml(user_desc)->arch};
        } else {
            arch_vec = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE, tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE};
        }
        return arch_vec;
    }

    uint64_t get_dram_bw(const tt::ARCH &arch) {
        static const std::unordered_map<tt::ARCH, uint64_t> dram_bw_map = {
            {tt::ARCH::GRAYSKULL,   (uint64_t)(12.8f * (1024*1024*1024))},
            {tt::ARCH::WORMHOLE,    (uint64_t)(24.0f * (1024*1024*1024))},
            {tt::ARCH::WORMHOLE_B0, (uint64_t)(24.0f * (1024*1024*1024))},
            {tt::ARCH::BLACKHOLE,   (uint64_t)(24.0f * (1024*1024*1024))},      // FIXME MT: Check this number
        };
        return dram_bw_map.at(arch);
    }

    uint64_t get_dram_bw_measured(const tt::ARCH &arch) {
        static const std::unordered_map<tt::ARCH, uint64_t> dram_bw_map = {
            {tt::ARCH::GRAYSKULL,   (uint64_t)(10.5f * (1024*1024*1024))},
            {tt::ARCH::WORMHOLE,    (uint64_t)(19.0f * (1024*1024*1024))},
            {tt::ARCH::WORMHOLE_B0, (uint64_t)(19.0f * (1024*1024*1024))},
            {tt::ARCH::BLACKHOLE,   (uint64_t)(19.0f * (1024*1024*1024))},      // FIXME MT: Check this number
        };
        return dram_bw_map.at(arch);
    }

    uint64_t get_eth_bw(const tt::ARCH &arch) {
        static const std::unordered_map<tt::ARCH, uint64_t> eth_bw_map = {
            {tt::ARCH::GRAYSKULL,   (uint64_t)(0)},
            {tt::ARCH::WORMHOLE,    (uint64_t)(12.5f * (1024*1024*1024))},
            {tt::ARCH::WORMHOLE_B0, (uint64_t)(12.5f * (1024*1024*1024))},
            {tt::ARCH::BLACKHOLE,   (uint64_t)(12.5f * (1024*1024*1024))},        // MT: Double check this BW number
        };
        return eth_bw_map.at(arch);
    }

    float get_aiclk_freq(const tt::ARCH &arch) {
        // Frequency in GigaHertz
        static const std::unordered_map<tt::ARCH, float> aiclk_freq_map = {
            {tt::ARCH::GRAYSKULL,   1.2},
            {tt::ARCH::WORMHOLE,    1.0},
            {tt::ARCH::WORMHOLE_B0, 1.0},
            {tt::ARCH::BLACKHOLE,   1.0},
        };
        return aiclk_freq_map.at(arch);
    }
    float get_noc_bw(const tt::ARCH &arch) {
        // NOC BW in bytes/clock cycle. Could vary with future archs.
        static const std::unordered_map<tt::ARCH, float> noc_bw_map = {
            {tt::ARCH::GRAYSKULL,   32.0},
            {tt::ARCH::WORMHOLE,    32.0},
            {tt::ARCH::WORMHOLE_B0, 32.0},
            {tt::ARCH::BLACKHOLE,   32.0},
        };
        return noc_bw_map.at(arch) * get_aiclk_freq(arch); // get final value in GBPS
    }
    uint32_t get_noc_channels(const tt::ARCH &arch) {
        // Could vary with future archs.
        static const std::unordered_map<tt::ARCH, uint32_t> noc_vc_map = {
            {tt::ARCH::GRAYSKULL,   (uint32_t)(16)},
            {tt::ARCH::WORMHOLE,    (uint32_t)(16)},
            {tt::ARCH::WORMHOLE_B0, (uint32_t)(16)},
            {tt::ARCH::BLACKHOLE,   (uint32_t)(16)},
        };
        return noc_vc_map.at(arch); // get final value in GBPS
    }
}
std::string tt::param::get_device_yaml_based_on_config(tt::ARCH arch, std::uint32_t harvesting_mask, std::pair<int, int> grid_dim, const std::string& out_dir) {
    std::string file_hash =  get_string_lowercase(arch) + "_" +  std::to_string(harvesting_mask) + "_"
                                + std::to_string(grid_dim.first) + "x" + std::to_string(grid_dim.second);
    stringstream desc_file_name;
    desc_file_name << out_dir << "/device_descs/" << file_hash << ".yaml";

    if(!fs::exists(desc_file_name.str())) {
        create_soc_descriptor(desc_file_name.str(), arch, harvesting_mask, grid_dim);
    }
    return desc_file_name.str();
}

std::string tt::param::get_device_cluster_yaml(const std::string& out_dir) {
    auto silicon_devices_per_arch = get_arch_to_silicon_device_ids_map();
    if(silicon_devices_per_arch.find(tt::ARCH::WORMHOLE_B0) != silicon_devices_per_arch.end() || silicon_devices_per_arch.find(tt::ARCH::WORMHOLE) != silicon_devices_per_arch.end()) {
        return tt_cluster::get_cluster_desc_path(out_dir);
    }
    else {
        return "";
    }
}

std::vector<tt::param::DeviceDesc> tt::param::get_device_descs(const std::string& out_dir) {
    std::vector<tt::ARCH> available_archs = tt_cluster::detect_available_devices(TargetDevice::Silicon, false);
    std::vector<tt::ARCH> available_archs_mmio = tt_cluster::detect_available_devices(TargetDevice::Silicon);

    std::vector<DeviceDesc> device_desc_vec = {};

    for(int dev_id = 0; dev_id < available_archs.size(); dev_id++) {
        DeviceDesc curr_entry;
        curr_entry.arch = available_archs.at(dev_id);
        std::tie(curr_entry.soc_desc_yaml, curr_entry.harvesting_mask) = get_device_yaml_at_index(dev_id, out_dir);

        if(available_archs.at(dev_id) == tt::ARCH::GRAYSKULL) {
            curr_entry.mmio = true;
        }
        else {
            if(dev_id < available_archs_mmio.size()) {
                curr_entry.mmio = true;
            }
            else {
                curr_entry.mmio = false;
            }
        }
        device_desc_vec.push_back(curr_entry);
    }
    return device_desc_vec;
}

tt::param::DeviceDesc tt::param::get_custom_device_desc(tt::ARCH arch, bool mmio, std::uint32_t harvesting_mask, std::pair<int, int> grid_dim, const std::string& out_dir) {
    DeviceDesc descriptor;
    descriptor.soc_desc_yaml = get_device_yaml_based_on_config(arch, harvesting_mask, grid_dim, out_dir);
    descriptor.mmio = mmio;
    descriptor.arch = arch;
    descriptor.harvesting_mask = harvesting_mask;
    return descriptor;
}

std::tuple<std::string, std::uint32_t> tt::param::get_device_yaml_at_index(std::size_t device_index, const std::string& out_dir) {
    // Get all available archs in the device cluster
    static std::map<chip_id_t, uint32_t> rows_to_harvest = {};
    std::vector<tt::ARCH> available_archs = tt_cluster::detect_available_devices(TargetDevice::Silicon, false);
    const bool skip_driver_allocs = true;
    const bool perform_harvesting = true;
    const bool clean_system_resources = false;
    if(device_index >= available_archs.size()) {
        log_assert(device_index < available_archs.size(), "Querying SOC desc for device id {}, when there are only {} devices in the cluster.", device_index, available_archs.size());
    }
    if(!rows_to_harvest.size()) {
        auto silicon_devices_per_arch = get_arch_to_silicon_device_ids_map();
        for(auto& arch : available_archs) {
            std::string soc_desc_path = get_soc_description_file(arch, tt::TargetDevice::Silicon);
            if(arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE) {
                char temp[] = "/tmp/temp_cluster_desc_dir_XXXXXX";
                char *dir_name = mkdtemp(temp);
                tt_cluster::get_cluster_desc_path(dir_name); // populates cluster_desc_path used downstream
            }

            std::unique_ptr<tt_cluster> cluster = std::make_unique<tt_cluster>();
            if(std::getenv("TT_BACKEND_HARVESTED_ROWS")) {
                auto harvesting_masks = populate_harvesting_masks_from_env_var(silicon_devices_per_arch.at(arch), true);
                cluster->open_device(arch, TargetDevice::Silicon, silicon_devices_per_arch.at(arch), soc_desc_path, cluster_desc_path, perform_harvesting, skip_driver_allocs, clean_system_resources, "", harvesting_masks);
            }
            else {
                cluster->open_device(arch, TargetDevice::Silicon, silicon_devices_per_arch.at(arch), soc_desc_path, cluster_desc_path, perform_harvesting, skip_driver_allocs, clean_system_resources);
            }
            rows_to_harvest = std::map<chip_id_t, uint32_t>(cluster -> harvested_rows_per_target.begin(), cluster -> harvested_rows_per_target.end());
        }
    }
    return std::make_tuple(
        get_device_yaml_based_on_config(available_archs.at(device_index), rows_to_harvest.at(device_index), {0, 0}, out_dir), rows_to_harvest.at(device_index));
}

void tt::param::create_soc_descriptor(const std::string& file_name, tt::ARCH arch, std::uint32_t harvesting_mask, std::pair<int, int> grid_dim) {
    std::string default_desc_path = get_soc_description_file(arch, tt::TargetDevice::Silicon, "", false, tt_xy_pair(grid_dim.first, grid_dim.second));
    fs::path file_path = file_name;
    if(!fs::exists(file_path.parent_path())) {
        fs::create_directories(file_path.parent_path());
    }
    if(!harvesting_mask) {
        fs::copy(default_desc_path, file_name);
    }
    else {
        auto default_sdesc = load_soc_descriptor_from_yaml(default_desc_path);
        uint32_t max_row = (*std::max_element((default_sdesc -> workers).begin(), (default_sdesc -> workers).end(), [] (const auto& a, const auto& b) { return a.y < b.y; })).y;
        remove_worker_row_from_descriptor(default_sdesc.get(), extract_rows_to_remove(arch, max_row, harvesting_mask));
        generate_soc_description(default_sdesc.get(), file_name, {static_cast<int>((default_sdesc->grid_size).y), static_cast<int>((default_sdesc->grid_size).x)});
    }
}

// Runtime implementation of param_lib's extern populate_runtime_params
//
void tt::param::populate_runtime_params(
    std::map<std::string, std::string> &params,
    const std::string &soc_desc_file_path_in,
    const std::string &eth_cluster_file_path_in,
    std::string runtime_params_ref_path,
    bool store)
{
    log_assert(!fs::is_directory(runtime_params_ref_path) , "The provided reference YAML path is not a valid file.");
    std::string key;

    // ------------------------------------------------------------------------
    // Populate arch device specific parameters
    // ------------------------------------------------------------------------
    // Derate factors are needed to map theoretical to realistic numbers
    // due to dynamic memory refresh freq and packetization overheads
    // constexpr float dram_bw_derate_factor = 0.75f;  // using get_dram_bw_measured instead
    constexpr float eth_bw_derate_factor = 0.90f;
    constexpr uint32_t num_noc_rings = 2;
    constexpr int kMaxBytesPerPhase = 38;
    constexpr int kBackendReservedInQueues = 1;
    constexpr int kMaxDRAMInQueues = 40 - kBackendReservedInQueues;
    constexpr int kMaxDRAMOutQueues = 8;
    constexpr int kMaxFanOutStreams = 16;

    const bool skip_driver_allocs = true;
    const std::vector<string> scope_vec = {"SYSMEM", "DRAM", "ETH", "T6", "NOC", "PARAM"};

    // ------------------------------------------------------------------------
    // Populate arch device specific parameters
    // ------------------------------------------------------------------------
    std::vector<tt::ARCH> arch_vec = get_arch_vec(soc_desc_file_path_in);

    for (const tt::ARCH &arch : arch_vec)
    {
        const string arch_name = get_arch_str(arch);
        log_debug(tt::LogBackend, "Populating runtime params for arch: {}", arch_name);

        if(soc_desc_file_path_in.size() == 0) {
            log_warning(tt::LogBackend, "The SOC Descriptor field should be populated when calling populate_runtime_params. Using this without a SOC Descriptor is deperecated.");
        }
        string soc_desc_file_path = soc_desc_file_path_in == "" ? get_soc_description_file(arch, tt::TargetDevice::Silicon) : soc_desc_file_path_in;
        std::unique_ptr<buda_soc_description> sdesc = load_soc_descriptor_from_file(arch, soc_desc_file_path);
        tt_xy_pair grid_size = {(sdesc->worker_grid_size).x, (sdesc->worker_grid_size).y};
        uint16_t num_worker_cores = (sdesc->workers).size();
        std::vector<int> dram_chan = sdesc->get_dram_chan_map();
        int num_chans = dram_chan.size();
        int num_blocks = sdesc->get_num_dram_blocks_per_channel();
        std::vector<uint16_t> t6_cores_per_dram_channel = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->workers, sdesc->dram_core_channel_map, num_chans);
        std::vector<uint16_t> eth_cores_per_dram_channel = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->ethernet_cores, sdesc->dram_core_channel_map, num_chans);

        int num_host_mem_channels = arch == tt::ARCH::GRAYSKULL ? 1 : 4;

        for (const string &scope : scope_vec)
        {
            if (scope == "SYSMEM")
            {
                for(int i = 0; i < num_host_mem_channels; i++) {
                    std::string tmp = "_chan" + std::to_string(i);
                    key = tt::param::get_lookup_key({arch_name, scope, "host_region_range_size" + tmp});
                    params[key] = std::to_string(host_mem::address_map::ALLOCATABLE_QUEUE_REGION_SIZE(i));
                    key = tt::param::get_lookup_key({arch_name, scope, "backend_reserved_range_start" + tmp});
                    params[key] = params[key] = std::to_string(host_mem::address_map::DEVICE_TO_HOST_SCRATCH_START_PER_CHANNEL[i]);
                    key = tt::param::get_lookup_key({arch_name, scope, "backend_reserved_range_size" + tmp});
                    params[key] = std::to_string(host_mem::address_map::DEVICE_TO_HOST_SCRATCH_SIZE_BYTES_PER_CHANNEL[i]);
                }
                key = tt::param::get_lookup_key({arch_name, scope, "host_region_range_start"});
                params[key] = std::to_string(host_mem::address_map::DEVICE_TO_HOST_REGION_START);

                key = tt::param::get_lookup_key({arch_name, scope, "host_region_num_channels"});
                params[key] = std::to_string(num_host_mem_channels);
            }
            else if (scope == "DRAM")
            {
                key = tt::param::get_lookup_key({arch_name, scope, "num_channels"});
                params[key] = std::to_string(num_chans);

                key = tt::param::get_lookup_key({arch_name, scope, "num_blocks_per_channel"});
                params[key] = std::to_string(num_blocks);

                key = tt::param::get_lookup_key({arch_name, scope, "channel_capacity"});
                params[key] = std::to_string(sdesc->dram_bank_size);

                key = tt::param::get_lookup_key({arch_name, scope, "block_capacity"});
                params[key] = std::to_string(sdesc->dram_bank_size / num_blocks);

                key = tt::param::get_lookup_key({arch_name, scope, "bandwidth_per_block_theoretical"});
                params[key] = std::to_string(get_dram_bw(arch));

                key = tt::param::get_lookup_key({arch_name, scope, "bandwidth_per_block_measured"});
                params[key] = std::to_string(get_dram_bw_measured(arch));

                key = tt::param::get_lookup_key({arch_name, scope, "overlay_full_blob_size"});
                params[key] = std::to_string(dram_mem::address_map::OVERLAY_FULL_BLOB_SIZE());

                key = tt::param::get_lookup_key({arch_name, scope, "overlay_base_blob_size"});
                params[key] = std::to_string(l1_mem::address_map::OVERLAY_BLOB_SIZE);

                key = tt::param::get_lookup_key({arch_name, scope, "overlay_extra_blob_size"});
                params[key] = std::to_string(dram_mem::address_map::OVERLAY_MAX_EXTRA_BLOB_SIZE());

                key = tt::param::get_lookup_key({arch_name, scope, "fw_block_size"});
                params[key] = std::to_string(dram_mem::address_map::FW_DRAM_BLOCK_SIZE());

                // dram chan core locations
                for (int i=0; i<num_chans; ++i) {
                    const auto &dram_chan = sdesc->dram_cores.at(i);
                    for (int j=0; j<dram_chan.size(); j++) {
                        const auto &dram_core = dram_chan.at(j);
                        std::string tmp = "core_xy_chan" + std::to_string(i);
                        if (dram_chan.size() > 1) { tmp = tmp + "_subchan" + std::to_string(j); }
                        key = tt::param::get_lookup_key({arch_name, scope, tmp});
                        params[key] = std::to_string(dram_core.x) + "-" + std::to_string(dram_core.y);
                    }
                }

                // dram reserved region for backend use
                uint64_t max_reserved = 0;
                for (int i=0; i<num_chans; ++i) {
                    // Needs to  be done per chip
                    uint64_t top_of_q_update_blob = tt_epoch_dram_manager::get_top_of_q_update_blobs(t6_cores_per_dram_channel[i], eth_cores_per_dram_channel[i]);
                    key = tt::param::get_lookup_key({arch_name, scope, "backend_reserved_chan" + std::to_string(i)});
                    params[key] = std::to_string(top_of_q_update_blob);
                    max_reserved = std::max(top_of_q_update_blob, max_reserved);
                }
                key = tt::param::get_lookup_key({arch_name, scope, "backend_reserved_max"});
                params[key] = std::to_string(max_reserved);

                // host mmio target ranges for fast tilize
                key = tt::param::get_lookup_key({arch_name, scope, "host_mmio_range_channel"});
                params[key] = std::to_string(tt::DRAM_HOST_MMIO_CHANNEL);
                key = tt::param::get_lookup_key({arch_name, scope, "host_mmio_range_start"});
                params[key] = std::to_string(tt::DRAM_HOST_MMIO_RANGE_START);
                key = tt::param::get_lookup_key({arch_name, scope, "host_mmio_range_size"});
                params[key] = std::to_string(tt::DRAM_HOST_MMIO_SIZE_BYTES);

                // device pcie peer2peer target ranges
                key = tt::param::get_lookup_key({arch_name, scope, "p2p_range_channel"});
                params[key] = std::to_string(tt::DRAM_PEER2PEER_CHANNEL);
                key = tt::param::get_lookup_key({arch_name, scope, "p2p_range_start"});
                params[key] = std::to_string(tt::DRAM_PEER2PEER_REGION_START);
                key = tt::param::get_lookup_key({arch_name, scope, "p2p_range_size"});
                params[key] = std::to_string(tt::DRAM_PEER2PEER_SIZE_BYTES);

            }
            else if (scope == "ETH")
            {
                int num_cores = sdesc->ethernet_cores.size();

                key = tt::param::get_lookup_key({arch_name, scope, "num_cores"});
                params[key] = std::to_string(num_cores);

                key = tt::param::get_lookup_key({arch_name, scope, "bandwidth_per_link_theoretical"});
                params[key] = std::to_string(get_eth_bw(arch));

                key = tt::param::get_lookup_key({arch_name, scope, "bandwidth_per_link_measured"});
                params[key] = std::to_string((uint64_t)(get_eth_bw(arch) * eth_bw_derate_factor));

                // ethernet core locations
                for (int i=0; i<num_cores; ++i) {
                    const auto &eth_core = sdesc->ethernet_cores.at(i);
                    key = tt::param::get_lookup_key({arch_name, scope, "core_xy_eth" + std::to_string(i)});
                    params[key] = std::to_string(eth_core.x) + "-" + std::to_string(eth_core.y);
                }
                key = tt::param::get_lookup_key({arch_name, scope, "l1_size"});
                params[key] = std::to_string(sdesc->eth_l1_size);

                int runtime_blob_size = eth_l1_mem::address_map::OVERLAY_BLOB_BASE + eth_l1_mem::address_map::OVERLAY_BLOB_SIZE + eth_l1_mem::address_map::OVERLAY_MAX_EXTRA_BLOB_SIZE + eth_l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_runtime_blob_size"});
                params[key] = std::to_string(runtime_blob_size);

                int pipegen_blob_size = PIPEGEN_STREAM_MEM_PIPE_PER_CORE_ETHERNET;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_pipegen_blob_size"});
                params[key] = std::to_string(pipegen_blob_size);

                int reserved_size = runtime_blob_size + pipegen_blob_size;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_backend_reserved_size"});
                params[key] = std::to_string(reserved_size);
            }
            else if (scope == "T6")
            {
                int num_cores = num_worker_cores;
                key = tt::param::get_lookup_key({arch_name, scope, "num_cores"});
                params[key] = std::to_string(num_cores);

                key = tt::param::get_lookup_key({arch_name, scope, "grid_size"});
                params[key] = std::to_string(grid_size.x) + "-" + std::to_string(grid_size.y);

                key = tt::param::get_lookup_key({arch_name, scope, "l1_size"});
                params[key] = std::to_string(sdesc->worker_l1_size);

                key = tt::param::get_lookup_key({arch_name, scope, "dst_size"});
                params[key] = std::to_string(sdesc->dst_size_alignment);

                int runtime_blob_size = l1_mem::address_map::OVERLAY_BLOB_BASE + l1_mem::address_map::OVERLAY_BLOB_SIZE + dram_mem::address_map::OVERLAY_MAX_EXTRA_BLOB_SIZE() + l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_runtime_blob_size"});
                params[key] = std::to_string(runtime_blob_size);

                int pipegen_blob_size = NET2PIPE_STREAM_MEM_PIPE_PER_CORE + PIPEGEN_STREAM_MEM_PIPE_PER_CORE;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_pipegen_blob_size"});
                params[key] = std::to_string(pipegen_blob_size);

                int reserved_size = runtime_blob_size + pipegen_blob_size;
                key = tt::param::get_lookup_key({arch_name, scope, "l1_backend_reserved_size"});
                params[key] = std::to_string(reserved_size);
            }
            else if (scope == "NOC")
            {
                std::stringstream oss;
                std::uint32_t print_precision = 2; // Printing Floating Point values here at specified precision

                key = tt::param::get_lookup_key({arch_name, scope, "bw_gbps"});
                oss << std::fixed << std::setprecision(2) << get_noc_bw(arch);
                params[key] = oss.str();
                oss.str("");
                key = tt::param::get_lookup_key({arch_name, scope, "num_rings"});
                params[key] = std::to_string(num_noc_rings);
                key = tt::param::get_lookup_key({arch_name, scope, "num_vcs"});
                params[key] = std::to_string(get_noc_channels(arch));
                key = tt::param::get_lookup_key({arch_name, "aiclk", "freq_ghz"});
                oss << std::fixed << std::setprecision(2) << get_aiclk_freq(arch);
                params[key] = oss.str();
                oss.str("");
            }
            else if (scope == "PARAM")
            {
                key = tt::param::get_lookup_key({arch_name, scope, "max_bytes_per_phase"});
                params[key] = std::to_string(kMaxBytesPerPhase);

                key = tt::param::get_lookup_key({arch_name, scope, "backend_reserved_in_queues"});
                params[key] = std::to_string(kBackendReservedInQueues);

                key = tt::param::get_lookup_key({arch_name, scope, "max_dram_in_queues"});
                params[key] = std::to_string(kMaxDRAMInQueues);

                key = tt::param::get_lookup_key({arch_name, scope, "max_dram_out_queues"});
                params[key] = std::to_string(kMaxDRAMOutQueues);

                key = tt::param::get_lookup_key({arch_name, scope, "max_fan_out_streams"});
                params[key] = std::to_string(kMaxFanOutStreams);
            }
        }
    }
    if (runtime_params_ref_path.size()) {
        if (store) {
            fs::path directory = fs::path(runtime_params_ref_path).remove_filename();
            if(!directory.empty()) fs::create_directory(directory);
            std::ofstream fout(runtime_params_ref_path);
            YAML::Node runtime_param_desc;
            for(auto it = params.begin(); it != params.end(); it++){
                runtime_param_desc["arch_level_params"][it -> first] = it -> second;
                if((it -> first).find("env") != string::npos) runtime_param_desc["params_to_ignore"].push_back(it -> first);
            }
            fout << runtime_param_desc;
            fout.close();
        } else {
            log_assert(fs::exists(runtime_params_ref_path), "The provided reference YAML path does not exist.");
            YAML::Node reference_param_desc = YAML::LoadFile(runtime_params_ref_path);
            validate_reference_against_params(reference_param_desc, "arch_level_params", params);
        }
    }
}

// Silicon device system params are grouped together so they can be populated only on request.
std::tuple<std::string, std::string> tt::param::populate_runtime_system_params(
    std::map<std::string, std::string> &params,
    const std::string &soc_desc_file_path_in,
    const std::string &eth_cluster_file_path_in,
    std::string runtime_params_ref_path,
    bool store)
{
    YAML::Node reference_param_desc;
    bool found_sys_level_params = false;
    log_assert(!fs::is_directory(runtime_params_ref_path) , "The provided reference YAML path is not a valid file.");
    if(runtime_params_ref_path.size() and not store){
        log_assert(fs::exists(runtime_params_ref_path), "The provided reference YAML path does not exist.");
        reference_param_desc = YAML::LoadFile(runtime_params_ref_path);
        for(auto it = reference_param_desc.begin(); it != reference_param_desc.end(); it++){
            if((it -> first).as<string>() == "system_level_params") found_sys_level_params = true;
        }
    }
    std::string eth_cluster_file_path = eth_cluster_file_path_in;

    if (!parse_env("TT_BACKEND_SKIP_SYSTEM_RUNTIME_PARAMS", false)){

        std::string key;
        int num_harvested_rows = 0;
        const bool skip_driver_allocs = true;
        const bool perform_harvesting = true;
        const bool clean_system_resources = false;
        auto devices = tt_cluster::detect_available_devices(TargetDevice::Silicon, false);
        auto devices_with_mmio = tt_cluster::detect_available_devices(TargetDevice::Silicon, true);
        log_assert(devices.size() >= devices_with_mmio.size(), "Num devices must be greater than or equal to num devices with mmio");

        // If devices is empty, just populate db from file. else compare db with sys level params
        if (!devices.size()) goto load_from_file;
        const std::map<tt::ARCH, std::set<chip_id_t>> &arch_to_silicon_device_ids_map = get_arch_to_silicon_device_ids_map();

        // ------------------------------------------------------------------------
        // Populate system device specific parameters
        // queries to the current system under use are made and stored
        // ------------------------------------------------------------------------

        const string prefix = "system";

        key = tt::param::get_lookup_key({prefix, "device", "num_mmio_devices"});
        params[key] = std::to_string(devices_with_mmio.size());

        auto available_devices_for_wormhole = std::count(devices.begin(), devices.end(), tt::ARCH::WORMHOLE);
        auto available_devices_for_wormhole_b0 = std::count(devices.begin(), devices.end(), tt::ARCH::WORMHOLE_B0);
        auto available_devices_for_blackhole = std::count(devices.begin(), devices.end(), tt::ARCH::BLACKHOLE);
        auto available_devices_for_arch = available_devices_for_wormhole + available_devices_for_wormhole_b0 + available_devices_for_blackhole;

        if (!fs::exists(eth_cluster_file_path) and available_devices_for_arch) {
            // Check if there are at least 1 wormhole silicon devices present - if so generate cluster description file and set path
            char temp[] = "/tmp/temp_cluster_desc_dir_XXXXXX";
            char *dir_name = mkdtemp(temp);
            eth_cluster_file_path = tt_cluster::get_cluster_desc_path(dir_name);
        }
        tt_cluster cluster;

        // Open all devices in system for each arch at once, and query for info.
        for (auto &d : arch_to_silicon_device_ids_map)
        {
            const tt::ARCH arch = d.first;
            const std::set<chip_id_t> silicon_device_ids = d.second;

            // per chip in case of sw harvesting
            string soc_desc_file_path = soc_desc_file_path_in == "" ? get_soc_description_file(arch, tt::TargetDevice::Silicon) : soc_desc_file_path_in;
            log_debug(tt::LogRuntime, "For runtime param db (system), going to query all {} silicon devices for arch: {}", silicon_device_ids.size(), arch);
            if(std::getenv("TT_BACKEND_HARVESTED_ROWS")) {
                auto harvesting_masks = populate_harvesting_masks_from_env_var(silicon_device_ids, true);
                cluster.open_device(arch, TargetDevice::Silicon, silicon_device_ids, soc_desc_file_path, cluster_desc_path, perform_harvesting, skip_driver_allocs, clean_system_resources, "", harvesting_masks);
            }
            else {
                cluster.open_device(arch, TargetDevice::Silicon, silicon_device_ids, soc_desc_file_path, eth_cluster_file_path, perform_harvesting, skip_driver_allocs, clean_system_resources);
            }
            std::shared_ptr<tt_device> device = cluster.get_device();
            for (auto device_id : silicon_device_ids)
            {
                const string scope = "device" + std::to_string(device_id);
                key = tt::param::get_lookup_key({prefix, scope, "type"});
                params[key] = get_string(devices.at(device_id));

                uint32_t harvesting_mask = cluster.harvested_rows_per_target[device_id];
                num_harvested_rows = std::bitset<32>(harvesting_mask).count();
                key = tt::param::get_lookup_key({prefix, scope, "harvesting_mask"});
                params[key] = std::to_string(harvesting_mask);
                key = tt::param::get_lookup_key({prefix, scope, "num_harvested_rows"});
                params[key] = std::to_string(num_harvested_rows);
                key = tt::param::get_lookup_key({prefix, scope, "pcie_speed_gbps"});
                params[key] = std::to_string(device->get_pcie_speed(device_id));
            }
        }

        if(!available_devices_for_arch){
            // This machine has GS devices, which don't have a cluster file. Get the number of chips from the cluster list.
            key = tt::param::get_lookup_key({prefix, "device", "number_of_chips"});
            params[key] = std::to_string(std::count(devices.begin(), devices.end(), tt::ARCH::GRAYSKULL));
        }

        // Get cluster descriptor file parameters
        // Get path to cluster descriptor file
        // Check if cluster descriptor file exists
        if (fs::exists(eth_cluster_file_path)) {
            log_info(tt::LogRuntime, "Found cluster descriptor file at path={}", eth_cluster_file_path);

            // Add cluster descriptor file into params object
            key = tt::param::get_lookup_key({prefix, "device", "cluster_descriptor"});
            params[key] = eth_cluster_file_path;

            // Generate tt cluster object
            std::unique_ptr<ClusterDescription> cluster_description = ClusterDescription::create_from_yaml(eth_cluster_file_path);
            log_assert(cluster_description != nullptr, "Expected cluster descriptor to be populated");

            // Extract all information from cluster file
            // Store chip ids that have mmio
            std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio = cluster_description->get_chips_with_mmio();
            std::string chips_with_mmio_serialized = "";

            // Serialize chip ids with mmio separated by a dash (eg. '1-2-3')
            for (const auto &pair : chips_with_mmio) {
                auto &chip_id = pair.first;
                chips_with_mmio_serialized += std::to_string(chip_id);
                chips_with_mmio_serialized += "-";
            }
            key = tt::param::get_lookup_key({prefix, "device", "chips_with_mmio"});
            params[key] = chips_with_mmio_serialized;
            log_assert(chips_with_mmio.size() == devices_with_mmio.size(), "Number of chips with MMIO reported from cluster and device must match!");

            // Store chip locations separated by a dash (eg. '0,0,0-1,1,0-')
            std::unordered_map<chip_id_t, eth_coord_t> chip_locations = cluster_description->get_chip_locations();
            std::string chip_locations_serialized = "";

            for(auto chip_locs = chip_locations.begin(); chip_locs != chip_locations.end(); chip_locs++) {
                // chip_id,x,y
                chip_locations_serialized += std::to_string(chip_locs->first) + "," + std::to_string(std::get<0>(chip_locs->second)) + "," + std::to_string(std::get<1>(chip_locs->second)) + ",";
                // rack,shelf
                chip_locations_serialized += std::to_string(std::get<2>(chip_locs->second)) + "," +
                                             std::to_string(std::get<3>(chip_locs->second)) + ",";
                chip_locations_serialized += "-";
            }
            key = tt::param::get_lookup_key({prefix, "device", "chip_locations"});
            params[key] = chip_locations_serialized;

            // Store number of chips
            std::size_t number_of_chips = cluster_description->get_number_of_chips();
            key = tt::param::get_lookup_key({prefix, "device", "number_of_chips"});
            params[key] = std::to_string(number_of_chips);

            // Store ethernet connection information separate by a dash (eg. '0,0,1,0-0,1,1,1-'
            std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>> ethernet_connections = cluster_description->get_ethernet_connections();
            std::string ethernet_connections_serialized = "";

            // Serialize information
            for(auto chip_connections = ethernet_connections.begin(); chip_connections != ethernet_connections.end(); chip_connections++) {
                for(auto chip_eth_connections = chip_connections->second.begin(); chip_eth_connections != chip_connections->second.end(); chip_eth_connections++) {
                    ethernet_connections_serialized += std::to_string(chip_connections->first) + "," + std::to_string(chip_eth_connections->first) + "," + std::to_string(std::get<0>(chip_eth_connections->second)) + "," + std::to_string(std::get<1>(chip_eth_connections->second)) + ",";
                    ethernet_connections_serialized += "-";
                }
            }
            key = tt::param::get_lookup_key({prefix, "device", "ethernet_connections"});
            params[key] = ethernet_connections_serialized;
        } else {
            log_info(tt::LogRuntime, "Cluster descriptor file does not exist at path={}", eth_cluster_file_path);
        }
        if(found_sys_level_params){
            validate_reference_against_params(reference_param_desc, "system_level_params", params);
        }
    }
    else{
        load_from_file:
            log_assert(runtime_params_ref_path.size() and !store, "When querying system level parameters with TT_BACKEND_SKIP_SYSTEM_RUNTIME_PARAMS=true or without any silicon devices, a reference file must be provided and bool store = false.");
            log_assert(found_sys_level_params, "When querying system level parameters from a reference file, the file must have a 'system_level_params' field.");

            for(auto it = reference_param_desc["system_level_params"].begin(); it != reference_param_desc["system_level_params"].end(); it++){
                params[(it -> first).as<string>()] = (it -> second).as<string>();
            }
    }
    if(runtime_params_ref_path.size() and store){
        log_assert(fs::exists(runtime_params_ref_path), "Could not find a {} file. This file must exist for system level parameters to be saved on disk.", runtime_params_ref_path); //Should never hit this assert based on the current flow (just here for verification).
        YAML::Node runtime_param_desc = YAML::LoadFile(runtime_params_ref_path);

        for(auto it = params.begin(); it != params.end(); it++){
            if((it -> first).find("system") != string::npos){
                runtime_param_desc["system_level_params"][it -> first] = it -> second;
                if((it -> first).find("aiclk_freq") != string::npos) runtime_param_desc["params_to_ignore"].push_back(it -> first);
            }
        }
        std::ofstream fout(runtime_params_ref_path);
        fout << runtime_param_desc;
        fout.close();
    }
    return {soc_desc_file_path_in, eth_cluster_file_path};
}
