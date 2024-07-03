// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace pipegen2
{
namespace soc_info_constants
{

// NOC address offset when WH chip is reading from host over PCIe.
// Related to get_pcie_base_addr_from_device in UMD.
constexpr std::uint64_t wh_pcie_host_noc_address_offset = 0x800000000;

// NOC bit to set when BH chip is writing to host over PCIe.
// Related to get_pcie_base_addr_from_device in UMD.
// This will give signal to ATU to use window index 4 for translation (these are set by bits 58-60).
constexpr std::uint64_t bh_pcie_host_noc_address_offset = 1ULL << 60;

// PCIe NOC address offset when sending data downstream the NOC.
constexpr std::uint64_t pcie_downstream_offset = 0x80000000;

// PCIe NOC address offset when sending data upstream the NOC.
constexpr std::uint64_t pcie_upstream_offset = 0x40000000;

// Default PCIe core x location, if no PCIe cores are specified in SOC descriptor.
constexpr std::size_t default_pcie_core_x = 0;

// Default PCIe core y location, if no PCIe cores are specified in SOC descriptor.
constexpr std::size_t default_pcie_core_y = 4;

// Magic number from pipegen1.
// TODO find out what it represents.
constexpr unsigned int tlb_index_core_y_multiplier = 13;

// Dedicated column used for DRAM cores in WH SoC descriptor.
constexpr std::size_t wh_dram_cores_column = 5;

// First dedicated row used for ethernet cores in WH SoC descriptor.
constexpr std::size_t wh_eth_cores_first_row = 0;

// Second dedicated row used for ethernet cores in WH SoC descriptor.
constexpr std::size_t wh_eth_cores_second_row = 6;

// Default offset with which unharvested coordinate system is shifted to harvested and vice versa.
constexpr std::size_t wh_harvested_translation_offset = 16;

// Default grid size in harvested WH soc descriptors.
constexpr std::size_t wh_harvested_grid_size = 32;

// Prefix to be used in SOC descriptors yaml when it contains only paths to multiple SOC descriptor yamls.
const std::string multi_soc_descriptors_prefix = "chip_descriptors:";

}  // namespace soc_info_constants
}  // namespace pipegen2