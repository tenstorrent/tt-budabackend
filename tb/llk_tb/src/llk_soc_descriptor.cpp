// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_soc_descriptor.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include "glog/logging.h"
#include "llk_addresses.h"

namespace fs = boost::filesystem;
using namespace llk;

std::string llk::get_arch_string(ARCH arch_name) {
    if (arch_name == ARCH::JAWBRIDGE) {
        return "jawbridge";
    } else if (arch_name == ARCH::GRAYSKULL) {
        return "grayskull";
    } else if (arch_name == ARCH::WORMHOLE) {
        return "wormhole";
    } else if (arch_name == ARCH::WORMHOLE_B0) {
        return "wormhole_b0";
    } else {
        throw std::runtime_error("Invalid ARCH passed in");
    }
}

const llk::SocDescriptor llk::load_soc_descriptor_from_yaml(std::string device_descriptor_file_path) {
    llk::SocDescriptor soc_descriptor;
    if (!fs::exists(device_descriptor_file_path)) {
        LOG(FATAL) << "file_path: " << device_descriptor_file_path;
        LOG(FATAL) << (fs::exists(device_descriptor_file_path), "Path to device-descriptor does not exist.");
    }

    YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_file_path);
    std::vector<std::size_t> trisc_sizes = {
        l1_mem::address_map::TRISC0_SIZE,
        l1_mem::address_map::TRISC1_SIZE,
        l1_mem::address_map::TRISC2_SIZE};  // TODO: Read trisc size from yaml

    auto grid_size_x = device_descriptor_yaml["grid"]["x_size"].as<int>();
    auto grid_size_y = device_descriptor_yaml["grid"]["y_size"].as<int>();

    load_core_descriptors_from_device_descriptor(device_descriptor_yaml, soc_descriptor);
    soc_descriptor.grid_size = llk::xy_pair(grid_size_x, grid_size_y);
    soc_descriptor.device_descriptor_file_path = device_descriptor_file_path;
    soc_descriptor.trisc_sizes = trisc_sizes;

    auto arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    boost::trim(arch_name_value);
    boost::to_upper(arch_name_value);

    if (arch_name_value == "JAWBRIDGE") {
        soc_descriptor.arch = llk::ARCH::JAWBRIDGE;
    } else if ((arch_name_value == "GRAYSKULL")) {
        soc_descriptor.arch = llk::ARCH::GRAYSKULL;
    } else if (arch_name_value == "WORMHOLE") {
        soc_descriptor.arch = llk::ARCH::WORMHOLE;
    } else if (arch_name_value == "WORMHOLE_B0") {
        soc_descriptor.arch = llk::ARCH::WORMHOLE_B0;
    } else {
        throw std::runtime_error(
            "At llk::LoadSocDescriptorFromYaml: \"" + arch_name_value + "\" is not recognized as ARCH.");
    }
    return soc_descriptor;
}
void llk::load_core_descriptors_from_device_descriptor(
    YAML::Node device_descriptor_yaml, llk::SocDescriptor &soc_descriptor) {
    auto worker_l1_size = device_descriptor_yaml["worker_l1_size"].as<int>();
    auto eth_l1_size = device_descriptor_yaml["eth_l1_size"].as<int>();

    auto arc_cores = device_descriptor_yaml["arc"].as<std::vector<std::string>>();
    for (const auto &core_string : arc_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::ARC;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto pcie_cores = device_descriptor_yaml["pcie"].as<std::vector<std::string>>();
    for (const auto &core_string : pcie_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::PCIE;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto dram_cores = device_descriptor_yaml["dram"].as<std::vector<std::string>>();
    for (const auto &core_string : dram_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::DRAM;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto eth_cores = device_descriptor_yaml["eth"].as<std::vector<std::string>>();
    for (const auto &core_string : eth_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::ETH;
        core_descriptor.l1_size = eth_l1_size;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto worker_cores = device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>();
    for (const auto &core_string : worker_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::WORKER;
        core_descriptor.l1_size = worker_l1_size;
        core_descriptor.dram_size_per_core = DEFAULT_DRAM_SIZE_PER_CORE;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
        soc_descriptor.workers.push_back(core_descriptor.coord);
    }
    auto harvested_cores = device_descriptor_yaml["harvested_workers"].as<std::vector<std::string>>();
    for (const auto &core_string : harvested_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::HARVESTED;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto router_only_cores = device_descriptor_yaml["router_only"].as<std::vector<std::string>>();
    for (const auto &core_string : router_only_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = llk::format_node(core_string);
        core_descriptor.type = CoreType::ROUTER_ONLY;
        soc_descriptor.cores.insert({core_descriptor.coord, core_descriptor});
    }
}

void llk::translate_soc_descriptor_to_ca_soc(CA::Soc &soc, const llk::SocDescriptor soc_descriptor) {
    for (auto &core : soc_descriptor.cores) {
        CA::SocNocNode node;
        node.noc_coord = core.first;
        node.memory_size = core.second.l1_size;
        switch (core.second.type) {
            case CoreType::ARC: node.arc = true; break;
            case CoreType::DRAM: node.dram = true; break;
            case CoreType::ETH: node.eth = true; break;
            case CoreType::PCIE: node.pcie = true; break;
            case CoreType::WORKER: node.worker = true; break;
            case CoreType::HARVESTED: node.harvested = true; break;
            case CoreType::ROUTER_ONLY: node.router_only = true; break;
            default: LOG(FATAL) << "Unsupported CoreType type: " << static_cast<int>(core.second.type); break;
        }
        soc.SetNodeProperties(node.noc_coord, node);
    }
}

std::ostream &operator<<(std::ostream &os, const llk::ARCH &arch) {
    switch (arch) {
        case llk::ARCH::JAWBRIDGE: os << "jawbridge"; break;
        case llk::ARCH::GRAYSKULL: os << "grayskull"; break;
        case llk::ARCH::WORMHOLE: os << "wormhole"; break;
        case llk::ARCH::WORMHOLE_B0: os << "wormhole_b0"; break;
        default: LOG(FATAL) << "Unsupported ARCH";
    };

    return os;
}
