// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <ostream>

#include "llk_xy_pair.h"
#include "yaml-cpp/yaml.h"

// TODO: Remove dependency on command_assembler + soc
#include "command_assembler/soc.h"

namespace CA = CommandAssembler;
namespace llk {

//! Arch Enumeration - Chip Generations
/*! Denotes Chip Generations */
enum class ARCH {
    JAWBRIDGE = 0,
    GRAYSKULL = 1,
    WORMHOLE = 2,
    WORMHOLE_B0 = 3,
    Invalid = 0xFF,
};
std::string get_arch_string(ARCH arch_name);

static constexpr ARCH ArchNameDefault = ARCH::GRAYSKULL;
static constexpr std::size_t DEFAULT_L1_SIZE = 1 * 1024 * 1024;
static constexpr std::size_t DEFAULT_CQ_SIZE = 64 * 1024;
static constexpr std::size_t DEFAULT_DRAM_SIZE_PER_CORE = 8 * 1024 * 1024;
static constexpr std::size_t DEFAULT_TRISC_SIZE = 16 * 1024;
static constexpr int NUM_TRISC = 3;

//! SocCore type enumerations
/*! Superset for all chip generations */
enum class CoreType {
    ARC,
    DRAM,
    ETH,
    PCIE,
    WORKER,
    HARVESTED,
    ROUTER_ONLY,

};

//! SocNodeDescriptor contains information regarding the Node/Core
/*!
    Should only contain relevant configuration for SOC
*/
struct CoreDescriptor {
    llk::xy_pair coord = llk::xy_pair(0, 0);
    CoreType type;

    std::size_t l1_size = 0;
    std::size_t dram_size_per_core = 0;
};

//! SocDescriptor contains information regarding the SOC configuration targetted.
/*!
    Should only contain relevant configuration for SOC
*/
struct SocDescriptor {
    ARCH arch;
    llk::xy_pair grid_size;
    std::unordered_map<llk::xy_pair, CoreDescriptor> cores;
    std::vector<llk::xy_pair> workers;
    std::vector<std::size_t> trisc_sizes;  // Most of software stack assumes same trisc size for whole chip..
    std::string device_descriptor_file_path = std::string("");
    const bool has(llk::xy_pair input) { return cores.find(input) != cores.end(); }
};

//! load_soc_descriptor_from_yaml takes a path to yaml and loads the llk::SocDescriptor with relevant information
/*!
  \param device_descriptor_file_path yaml path
  \return llk::SocDescriptor
*/
const llk::SocDescriptor load_soc_descriptor_from_yaml(std::string device_descriptor_file_path);
//! load_core_descriptors_from_device_descriptor takes yaml node and loads the llk::SocDescriptor with relevant
//! information per core
void load_core_descriptors_from_device_descriptor(
    YAML::Node device_descriptor_yaml, llk::SocDescriptor &soc_descriptor);
//! translate_soc_descriptor_to_ca_soc takes a path to yaml and loads the llk::SocDescriptor with relevant information
void translate_soc_descriptor_to_ca_soc(CA::Soc &soc, const llk::SocDescriptor soc_descriptor);
}  // namespace llk

std::ostream &operator<<(std::ostream &os, const llk::ARCH &arch);
