// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
#include "base_types.hpp"

#include "model/typedefs.h"

class buda_SocDescriptor;

namespace pipegen2
{

// Contains SoC info for device chips.
class SoCInfo
{
public:
    // Parses SoC info from SOC descriptors yaml. In case when device has no harvested chips, SOC descriptors yaml
    // should contain the SOC descriptor definition that each of the device chips will use. Otherwise, when device has
    // harvested chips, SOC descriptors yaml should contain just paths to multiple SOC descriptor yamls, one for each of
    // device chips.
    static std::unique_ptr<SoCInfo> parse_from_yaml(const std::string& soc_descriptors_yaml_path,
                                                    const std::vector<ChipId>& chip_ids);

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    virtual ~SoCInfo();

    // Returns device architecture (it is expected that all chips share the same architecture).
    tt::ARCH get_device_arch() const;

    // Returns vector of all device chip IDs.
    std::vector<ChipId> get_chip_ids() const;

    // Returns vector of physical locations of all worker cores on a certain chip.
    std::vector<tt_cxy_pair> get_worker_cores_physical_locations(const ChipId chip_id) const;

    // Returns vector of physical locations of all ethernet cores on a certain chip.
    std::vector<tt_cxy_pair> get_ethernet_cores_physical_locations(const ChipId chip_id) const;

    // Returns a physical location of the ethernet core which corresponds to ethernet channel index on a given chip.
    tt_cxy_pair get_ethernet_channel_core_physical_location(const ChipId chip_id, const int ethernet_channel) const;

    // Returns matrix (subchannels in channels) of all DRAM cores on a certain chip.
    std::vector<std::vector<tt_cxy_pair>> get_dram_cores_physical_locations(const ChipId chip_id) const;

    // Returns vector of physical locations of first subchannel for each DRAM core on a certain chip.
    std::vector<tt_cxy_pair> get_dram_cores_physical_locations_of_first_subchannel(const ChipId chip_id) const;

    // Converts logical worker core coordinates to physical.
    tt_cxy_pair convert_logical_to_physical_worker_core_coords(const tt_cxy_pair& logical_coords) const;

    // Returns NOC address of a given dram buffer.
    std::uint64_t get_dram_buffer_noc_address(const std::uint64_t dram_buf_addr,
                                              const ChipId chip_id,
                                              const unsigned int channel,
                                              const unsigned int subchannel) const;

    // Returns NOC address through a PCIe core for a given buffer PCIe address.
    std::uint64_t get_buffer_noc_address_through_pcie(const std::uint64_t pcie_buf_addr, const ChipId chip_id) const;

    // Returns physical location of the first PCIe core on the chip.
    tt_cxy_pair get_first_pcie_core_physical_location(const ChipId chip_id) const;

    // Returns NOC address of the HOST PCIe buffer.
    std::uint64_t get_host_noc_address_through_pcie(const std::uint64_t host_pcie_buf_addr, const ChipId chip_id) const;

    // Returns the lower NOC_ADDR_LOCAL_BITS (35) bits of the dram buffer noc address => returns just the address part,
    // without the notion of the dram core location.
    std::uint64_t get_local_dram_buffer_noc_address(const std::uint64_t dram_buf_noc_addr) const;

    // Returns local PCIe address, without the notion of the PCIe core location.
    std::uint64_t get_local_pcie_buffer_address(const std::uint64_t l1_buffer_address,
                                                const tt_cxy_pair& worker_core_location,
                                                const bool is_pcie_downstream) const;

    // Converts core coordinates from harvested SoC descriptor to original coordinate system.
    // NOTE: This feature is only enabled on some WH devices which have translation tables enabled on device,
    // allowing us to use virtual harvesting.
    tt_cxy_pair convert_harvested_core_location_to_unharvested(const tt_cxy_pair& harvested_core_location) const;

    // Converts core coordinates from original coordinate system to system used in harvested devices.
    tt_cxy_pair convert_unharvested_core_location_to_harvested(const tt_cxy_pair& unharvested_core_location) const;

    // Gets the mapping of individual chips to its soc descriptors.
    const std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> get_soc_decriptors() const;

protected:
    // Protected constructor.
    SoCInfo() = default;

    // Checks if SOC descriptors yaml contains paths to multiple descriptors and returns them, otherwise returns only
    // path to the SOC descriptors yaml.
    static std::map<ChipId, std::string> get_soc_descriptors_paths(const std::string& soc_descriptors_yaml_path,
                                                                   const std::vector<ChipId>& chip_ids);

    // Returns a soc descriptor instance for a given chip id.
    const buda_SocDescriptor* get_soc_descriptor(const ChipId chip_id) const;

    // Checks if SoC descriptor used indicates harvested chip which supports NOC translation.
    bool is_harvested_chip_with_noc_translation(const ChipId& chip_id) const;

    // SoC descriptors per chip ID.
    std::map<ChipId, std::unique_ptr<buda_SocDescriptor>> m_soc_descriptors;
};

} // namespace pipegen2