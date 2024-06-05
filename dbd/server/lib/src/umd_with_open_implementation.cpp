// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "dbdserver/umd_with_open_implementation.h"

#include <limits.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "device/blackhole_implementation.h"
#include "device/grayskull_implementation.h"
#include "device/tt_cluster_descriptor.h"
#include "device/tt_device.h"
#include "device/wormhole_implementation.h"

// Include automatically generated files that we embed in source to avoid managing their deployment
static const uint8_t blackhole_configuration_bytes[] = {
#include "../configuration/blackhole.embed"
};
static const uint8_t grayskull_configuration_bytes[] = {
#include "../configuration/grayskull.embed"
};
static const uint8_t wormhole_configuration_bytes[] = {
#include "../configuration/wormhole.embed"
};
static const uint8_t wormhole_b0_configuration_bytes[] = {
#include "../configuration/wormhole_b0.embed"
};

static std::filesystem::path get_temp_working_directory() {
    std::filesystem::path temp_path = std::filesystem::temp_directory_path();
    std::string temp_name = temp_path / "debuda_server_XXXXXX";

    return mkdtemp(temp_name.data());
}

static std::filesystem::path temp_working_directory = get_temp_working_directory();

static std::string write_temp_file(const std::string &file_name, const char *bytes, size_t length) {
    std::string temp_file_name = temp_working_directory / file_name;
    std::ofstream conf_file(temp_file_name, std::ios::out | std::ios::binary);

    if (!conf_file.is_open()) {
        throw std::runtime_error("Couldn't write configuration to temp file " + temp_file_name + ".");
    }
    conf_file.write(bytes, length);
    conf_file.close();
    return temp_file_name;
}

static std::string create_temp_configuration_file(tt::ARCH arch) {
    const uint8_t *configuration_bytes = nullptr;
    size_t configuration_length = 0;

    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            configuration_bytes = blackhole_configuration_bytes;
            configuration_length = sizeof(blackhole_configuration_bytes) / sizeof(blackhole_configuration_bytes[0]);
            break;
        case tt::ARCH::GRAYSKULL:
            configuration_bytes = grayskull_configuration_bytes;
            configuration_length = sizeof(grayskull_configuration_bytes) / sizeof(grayskull_configuration_bytes[0]);
            break;
        case tt::ARCH::WORMHOLE:
            configuration_bytes = wormhole_configuration_bytes;
            configuration_length = sizeof(wormhole_configuration_bytes) / sizeof(wormhole_configuration_bytes[0]);
            break;
        case tt::ARCH::WORMHOLE_B0:
            configuration_bytes = wormhole_b0_configuration_bytes;
            configuration_length = sizeof(wormhole_b0_configuration_bytes) / sizeof(wormhole_b0_configuration_bytes[0]);
            break;
        default:
            throw std::runtime_error("Unsupported architecture " + get_arch_str(arch) + ".");
    }

    return write_temp_file("soc_descriptor.yaml", reinterpret_cast<const char *>(configuration_bytes),
                           configuration_length);
}

// Identifies and returns the directory path of the currently running executable in a Linux environment.
static std::filesystem::path find_binary_directory() {
    char buffer[PATH_MAX + 1];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

    if (len != -1) {
        buffer[len] = '\0';
        std::string path(buffer);
        std::string::size_type pos = path.find_last_of("/");
        return path.substr(0, pos);
    }
    return {};
}

static std::string create_temp_network_descriptor_file(tt::ARCH arch, std::filesystem::path binary_directory) {
    if (arch == tt::ARCH::GRAYSKULL || arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        // Check if create-ethernet-map exists
        if (binary_directory.empty()) {
            binary_directory = find_binary_directory();
        }
        std::string create_ethernet_map = binary_directory / "debuda-create-ethernet-map-wormhole";

        if (std::filesystem::exists(create_ethernet_map)) {
            std::string cluster_descriptor_path = temp_working_directory / "cluster_desc.yaml";

            // Try calling create-ethernet-map
            if (!std::system(
                    (create_ethernet_map + " " + cluster_descriptor_path + " >/dev/null 2>/dev/null").c_str())) {
                return cluster_descriptor_path;
            }

            // create-ethernet-map failed, fallback to yaml generation
        } else
            throw std::runtime_error("Couldn't find create-ethernet-map at " + create_ethernet_map + ".");

        // TODO: If it doesn't, create file without network connections
        throw std::runtime_error("Call to create-ethernet-map failed. Fallback not implemented...");
    }
    throw std::runtime_error("Unsupported architecture " + get_arch_str(arch) + ".");
    return {};
}

static std::unique_ptr<tt_SiliconDevice> create_grayskull_device(const std::string &device_configuration_path,
                                                                 const std::string &cluster_descriptor_path,
                                                                 const std::set<chip_id_t> &target_devices) {
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config;
    dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = tt::umd::grayskull::MEM_SMALL_READ_WRITE_TLB;
    dynamic_tlb_config["REG_TLB"] = tt::umd::grayskull::REG_TLB;

    return std::make_unique<tt_SiliconDevice>(device_configuration_path, cluster_descriptor_path, target_devices,
                                              num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
}

static std::unique_ptr<tt_SiliconDevice> create_wormhole_device(const std::string &device_configuration_path,
                                                                const std::string &cluster_descriptor_path,
                                                                const std::set<chip_id_t> &target_devices) {
    uint32_t num_host_mem_ch_per_mmio_device = 4;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config;
    dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = tt::umd::wormhole::MEM_SMALL_READ_WRITE_TLB;
    dynamic_tlb_config["REG_TLB"] = tt::umd::wormhole::REG_TLB;

    return std::make_unique<tt_SiliconDevice>(device_configuration_path, cluster_descriptor_path, target_devices,
                                              num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
}

static std::unique_ptr<tt_SiliconDevice> create_blackhole_device(const std::string &device_configuration_path,
                                                                 const std::string &cluster_descriptor_path,
                                                                 const std::set<chip_id_t> &target_devices) {
    uint32_t num_host_mem_ch_per_mmio_device = 4;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config;
    dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = tt::umd::blackhole::TLB_BASE_INDEX_2M + 1;
    dynamic_tlb_config["REG_TLB"] = tt::umd::blackhole::REG_TLB;

    return std::make_unique<tt_SiliconDevice>(device_configuration_path, cluster_descriptor_path, target_devices,
                                              num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
}

// Creates SOC descriptor files by serializing tt_SocDescroptor structure to yaml.
// TODO: Current copied from runtime/runtime_utils.cpp: print_device_description. It should be moved to UMD and reused
// on both places.
static std::map<uint8_t, std::string> create_device_soc_descriptors(tt_SiliconDevice *device,
                                                                    const std::vector<uint8_t> &device_ids) {
    tt_device *d = static_cast<tt_device *>(device);
    std::map<uint8_t, std::string> device_soc_descriptors;

    for (auto device_id : device_ids) {
        auto soc_descriptor = d->get_soc_descriptor(device_id);
        std::string file_name = temp_working_directory / ("device_desc_runtime_" + std::to_string(device_id) + ".yaml");
        std::ofstream outfile(file_name);

        outfile << "grid:" << std::endl;
        outfile << "  x_size: " << soc_descriptor->grid_size.x << std::endl;
        outfile << "  y_size: " << soc_descriptor->grid_size.y << std::endl << std::endl;

        if (soc_descriptor->physical_grid_size.x != soc_descriptor->grid_size.x ||
            soc_descriptor->physical_grid_size.y != soc_descriptor->grid_size.y) {
            outfile << "physical:" << std::endl;
            outfile << "  x_size: " << std::min(soc_descriptor->physical_grid_size.x, soc_descriptor->grid_size.x)
                    << std::endl;
            outfile << "  y_size: " << std::min(soc_descriptor->physical_grid_size.y, soc_descriptor->grid_size.y)
                    << std::endl
                    << std::endl;
        }

        outfile << "arc:" << std::endl;
        outfile << "  [" << std::endl;

        for (const auto &arc : soc_descriptor->arc_cores) {
            if (arc.x < soc_descriptor->grid_size.x && arc.y < soc_descriptor->grid_size.y) {
                outfile << arc.x << "-" << arc.y << ", ";
            }
        }
        outfile << std::endl;
        outfile << "  ]" << std::endl << std::endl;

        outfile << "pcie:" << std::endl;
        outfile << "  [" << std::endl;

        for (const auto &pcie : soc_descriptor->pcie_cores) {
            if (pcie.x < soc_descriptor->grid_size.x && pcie.y < soc_descriptor->grid_size.y) {
                outfile << pcie.x << "-" << pcie.y << ", ";
            }
        }
        outfile << std::endl;
        outfile << "  ]" << std::endl << std::endl;

        // List of available dram cores in full grid
        outfile << "dram:" << std::endl;
        outfile << "  [" << std::endl;

        for (const auto &dram_cores : soc_descriptor->dram_cores) {
            // Insert the dram core if it's within the given grid
            std::vector<std::string> inserted = {};
            for (const auto &dram_core : dram_cores) {
                if ((dram_core.x < soc_descriptor->grid_size.x) and (dram_core.y < soc_descriptor->grid_size.y)) {
                    inserted.push_back(std::to_string(dram_core.x) + "-" + std::to_string(dram_core.y));
                }
            }
            if (inserted.size()) {
                outfile << "[";

                for (int i = 0; i < inserted.size(); i++) {
                    outfile << inserted[i] << ", ";
                }

                outfile << "]," << std::endl;
            }
        }
        outfile << std::endl << "]" << std::endl << std::endl;

        outfile << "eth:" << std::endl << "  [" << std::endl;
        bool inserted_eth = false;
        for (const auto &ethernet_core : soc_descriptor->ethernet_cores) {
            // Insert the eth core if it's within the given grid
            if (ethernet_core.x < soc_descriptor->grid_size.x && ethernet_core.y < soc_descriptor->grid_size.y) {
                if (inserted_eth) {
                    outfile << ", ";
                }
                outfile << ethernet_core.x << "-" << ethernet_core.y;
                inserted_eth = true;
            }
        }
        outfile << std::endl << "]" << std::endl << std::endl;
        // Insert worker cores that are within the given grid
        outfile << "harvested_workers:" << std::endl;
        outfile << "  [" << std::endl;

        for (const auto &worker : soc_descriptor->harvested_workers) {
            if (worker.x < soc_descriptor->grid_size.x && worker.y < soc_descriptor->grid_size.y) {
                outfile << worker.x << "-" << worker.y << ", ";
            }
        }
        outfile << std::endl;
        outfile << "  ]" << std::endl << std::endl;

        outfile << "functional_workers:" << std::endl;
        outfile << "  [" << std::endl;
        for (const auto &worker : soc_descriptor->workers) {
            if (worker.x < soc_descriptor->grid_size.x && worker.y < soc_descriptor->grid_size.y) {
                outfile << worker.x << "-" << worker.y << ", ";
            }
        }
        outfile << std::endl;
        outfile << "  ]" << std::endl << std::endl;

        outfile << "router_only:" << std::endl << "  []" << std::endl << std::endl;

        // Fill in the rest that are static to our device
        outfile << "worker_l1_size:" << std::endl;
        outfile << "  " << soc_descriptor->worker_l1_size << std::endl << std::endl;
        outfile << "dram_bank_size:" << std::endl;
        outfile << "  " << soc_descriptor->dram_bank_size << std::endl << std::endl;
        outfile << "eth_l1_size:" << std::endl;
        outfile << "  " << soc_descriptor->eth_l1_size << std::endl << std::endl;
        outfile << "arch_name: " << get_arch_str(soc_descriptor->arch) << std::endl << std::endl;
        outfile << "features:" << std::endl;
        outfile << "  noc:" << std::endl;
        outfile << "    translation_id_enabled: True" << std::endl;
        outfile << "  unpacker:" << std::endl;
        outfile << "    version: " << soc_descriptor->unpacker_version << std::endl;
        outfile << "    inline_srca_trans_without_srca_trans_instr: False" << std::endl;
        outfile << "  math:" << std::endl;
        outfile << "    dst_size_alignment: " << soc_descriptor->dst_size_alignment << std::endl;
        outfile << "  packer:" << std::endl;
        outfile << "    version: " << soc_descriptor->packer_version << std::endl;
        outfile << "  overlay:" << std::endl;
        outfile << "    version: " << soc_descriptor->overlay_version << std::endl;
        outfile << std::endl;

        device_soc_descriptors[device_id] = file_name;
    }
    return device_soc_descriptors;
}

namespace tt::dbd {

umd_with_open_implementation::umd_with_open_implementation(std::unique_ptr<tt_SiliconDevice> device)
    : umd_implementation(device.get()), device(std::move(device)) {}

std::unique_ptr<umd_with_open_implementation> umd_with_open_implementation::open(
    const std::filesystem::path &binary_directory, const std::string &runtime_yaml_path,
    const std::vector<uint8_t> &wanted_devices) {
    auto devices = tt_SiliconDevice::detect_available_device_ids();

    if (devices.size() == 0) {
        throw std::runtime_error("No Tenstorrent devices were detected on this system.");
    }

    // Check that all chips are of the same type
    tt::ARCH arch = detect_arch(devices[0]);

    for (size_t i = 1; i < devices.size(); i++) {
        auto newArch = detect_arch(devices[i]);

        if (arch != newArch) {
            throw std::runtime_error("Not all devices have the same architecture.");
        }
    }

    // Create device
    auto device_configuration_path = create_temp_configuration_file(arch);
    if (device_configuration_path.empty()) {
        return {};
    }
    auto cluster_descriptor_path = create_temp_network_descriptor_file(arch, binary_directory);
    std::vector<uint8_t> device_ids;
    std::unique_ptr<tt_SiliconDevice> device;

    // Try to read cluster descriptor
    std::set<chip_id_t> target_devices;

    if (!cluster_descriptor_path.empty() && arch != tt::ARCH::GRAYSKULL) {
        auto cluster_descriptor = tt_ClusterDescriptor::create_from_yaml(cluster_descriptor_path);

        for (chip_id_t i : cluster_descriptor->get_all_chips()) {
            device_ids.push_back(i);
        }
    } else {
        // Fallback to use only local devices
        for (chip_id_t i = 0; i < devices.size(); i++) {
            device_ids.push_back(i);
        }
    }

    // If we specified which devices we want, check that they are available and then extract their ids
    for (auto wanted_device : wanted_devices)
        if (std::find(device_ids.begin(), device_ids.end(), wanted_device) == device_ids.end())
            throw std::runtime_error("Device " + std::to_string(wanted_device) + " is not available.");
    if (!wanted_devices.empty()) device_ids = wanted_devices;

    for (auto device_id : device_ids) target_devices.insert(device_id);

    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            device = create_grayskull_device(device_configuration_path, std::string(), target_devices);
            break;
        case tt::ARCH::WORMHOLE:
        case tt::ARCH::WORMHOLE_B0:
            device = create_wormhole_device(device_configuration_path, cluster_descriptor_path, target_devices);
            break;
        case tt::ARCH::BLACKHOLE:
            device = create_blackhole_device(device_configuration_path, cluster_descriptor_path, target_devices);
            break;
        default:
            throw std::runtime_error("Unsupported architecture " + get_arch_str(arch) + ".");
    }

    auto device_soc_descriptors = create_device_soc_descriptors(device.get(), device_ids);

    auto implementation = std::make_unique<umd_with_open_implementation>(std::move(device));

    implementation->runtime_yaml_path = runtime_yaml_path;
    implementation->device_configuration_path = device_configuration_path;
    implementation->cluster_descriptor_path = cluster_descriptor_path;
    implementation->device_ids = device_ids;
    implementation->device_soc_descriptors = device_soc_descriptors;
    return std::move(implementation);
}

std::optional<std::string> umd_with_open_implementation::get_runtime_data() {
    if (runtime_yaml_path.empty()) return {};

    std::ifstream file(runtime_yaml_path);
    if (!file.is_open()) return {};

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

std::optional<std::string> umd_with_open_implementation::get_cluster_description() { return cluster_descriptor_path; }

std::optional<std::vector<uint8_t>> umd_with_open_implementation::get_device_ids() { return device_ids; }

std::optional<std::string> umd_with_open_implementation::get_device_soc_description(uint8_t chip_id) {
    try {
        return device_soc_descriptors[chip_id];
    } catch (...) {
        return {};
    }
}

}  // namespace tt::dbd