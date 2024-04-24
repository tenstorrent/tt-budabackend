// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/soc_info.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>

#include "device/soc_info_constants.h"
#include "device/tt_arch_types.h"
#include "l1_address_map.h"
#include "noc/noc_parameters.h"

#include "pipegen2_exceptions.h"

namespace pipegen2
{

std::unique_ptr<SoCInfo> SoCInfo::parse_from_yaml(const std::string& soc_descriptors_yaml_path,
                                                  const std::vector<ChipId>& chip_ids)
{
    std::unique_ptr<SoCInfo> soc_info(new SoCInfo());

    std::map<ChipId, std::string> soc_descriptors_paths = get_soc_descriptors_paths(
        soc_descriptors_yaml_path, chip_ids);

    for (const auto& [chip_id, soc_descriptor_path] : soc_descriptors_paths)
    {
        soc_info->m_soc_descriptors.emplace(chip_id, load_soc_descriptor_from_yaml(soc_descriptor_path));
    }

    return soc_info;
}

std::map<ChipId, std::string> SoCInfo::get_soc_descriptors_paths(const std::string& soc_descriptors_yaml_path,
                                                                 const std::vector<ChipId>& chip_ids)
{
    std::map<ChipId, std::string> soc_descriptors_paths;

    std::ifstream soc_descriptors_yaml(soc_descriptors_yaml_path);
    std::string current_line;
    std::getline(soc_descriptors_yaml, current_line);

    if (current_line.find(soc_info_constants::multi_soc_descriptors_prefix) == std::string::npos)
    {
        // Set the same SoC descriptor for each chip found in pipegen.yaml.
        for (ChipId chip_id : chip_ids)
        {
            soc_descriptors_paths.emplace(chip_id, soc_descriptors_yaml_path);
        }
    }
    else
    {
        while (std::getline(soc_descriptors_yaml, current_line))
        {
            // Each line should contain chip ID and corresponding SOC descriptor path, separated by whitespace.
            std::istringstream line_stream(current_line);
            std::vector<std::string> line_parts(
                (std::istream_iterator<std::string>(line_stream)), std::istream_iterator<std::string>());

            if (line_parts.size() == 2)
            {
                soc_descriptors_paths.emplace(static_cast<ChipId>(std::stoi(line_parts[0])), line_parts[1]);
            }
        }

        // Chip IDs found in SoC descriptors should match those found in pipegen.yaml.
        for (ChipId chip_id : chip_ids)
        {
            if (soc_descriptors_paths.count(chip_id) == 0)
            {
                throw InvalidSoCInfoYamlException(
                    "Expecting multi SoC descriptor chips to match chip IDs found in pipegen.yaml!");
            }
        }
    }

    soc_descriptors_yaml.close();

    if (soc_descriptors_paths.empty())
    {
        throw InvalidSoCInfoYamlException("No SoC descriptor paths found");
    }

    return soc_descriptors_paths;
}

SoCInfo::~SoCInfo() = default;

tt::ARCH SoCInfo::get_device_arch() const
{
    log_assert(!m_soc_descriptors.empty(), "Expecting to find at least one soc descriptor");

    // Expecting that all descriptors have the same architecture.
    return m_soc_descriptors.begin()->second->arch;
}

std::vector<ChipId> SoCInfo::get_chip_ids() const
{
    std::vector<ChipId> chip_ids;
    for (const auto& it : m_soc_descriptors)
    {
        chip_ids.push_back(it.first);
    }

    return chip_ids;
}

std::vector<tt_cxy_pair> SoCInfo::get_worker_cores_physical_locations(const ChipId chip_id) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

    std::vector<tt_cxy_pair> worker_cores_physical_locations;
    for (const tt_xy_pair& core_physical_coords : chip_soc_desc->workers)
    {
        worker_cores_physical_locations.push_back(tt_cxy_pair(chip_id, core_physical_coords));
    }

    return worker_cores_physical_locations;
}

std::vector<tt_cxy_pair> SoCInfo::get_ethernet_cores_physical_locations(const ChipId chip_id) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

    std::vector<tt_cxy_pair> eth_cores_physical_locations;
    for (const tt_xy_pair& core_physical_coords : chip_soc_desc->ethernet_cores)
    {
        eth_cores_physical_locations.push_back(tt_cxy_pair(chip_id, core_physical_coords));
    }

    return eth_cores_physical_locations;
}

tt_cxy_pair SoCInfo::get_ethernet_channel_core_physical_location(const ChipId chip_id, const int ethernet_channel) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

    const std::vector<tt_xy_pair>& chip_eth_cores = chip_soc_desc->ethernet_cores;
    log_assert(ethernet_channel >= 0 && ethernet_channel < chip_eth_cores.size(),
               "Ethernet channel {} out of bounds", ethernet_channel);

    return tt_cxy_pair(chip_id, chip_eth_cores[ethernet_channel]);
}

std::vector<std::vector<tt_cxy_pair>> SoCInfo::get_dram_cores_physical_locations(const ChipId chip_id) const
{
    const buda_SocDescriptor* chip_soc_descriptor = get_soc_descriptor(chip_id);

    std::vector<std::vector<tt_cxy_pair>> dram_cores_physical_locations;

    for (const std::vector<tt_xy_pair>& dram_channels : chip_soc_descriptor->dram_cores)
    {
        std::vector<tt_cxy_pair> dram_subchannels;
        for (const tt_xy_pair& dram_core : dram_channels)
        {
            dram_subchannels.push_back(tt_cxy_pair(chip_id, dram_core));
        }
        dram_cores_physical_locations.push_back(dram_subchannels);
    }

    return dram_cores_physical_locations;
}

std::vector<tt_cxy_pair> SoCInfo::get_dram_cores_physical_locations_of_first_subchannel(const ChipId chip_id) const
{
    std::vector<std::vector<tt_cxy_pair>> dram_cores_physical_locations = get_dram_cores_physical_locations(chip_id);
    std::vector<tt_cxy_pair> dram_cores_first_subchannel_locations;

    for (const std::vector<tt_cxy_pair>& dram_core_subchannels_locations : dram_cores_physical_locations)
    {
        log_assert(!dram_core_subchannels_locations.empty(), "Expecting subchannels to exist!");
        dram_cores_first_subchannel_locations.push_back(dram_core_subchannels_locations.front());
    }

    return dram_cores_first_subchannel_locations;
}

tt_cxy_pair SoCInfo::convert_logical_to_physical_worker_core_coords(const tt_cxy_pair& logical_coords) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(logical_coords.chip);

    // Routing cores are superset of worker cores.
    try
    {
        return tt_cxy_pair(logical_coords.chip, chip_soc_desc->get_routing_core(logical_coords));
    }
    catch (std::exception& e)
    {
        throw NoPhysicalCoreException("There is no physical worker core on logical location " +
                                      logical_coords.str(),
                                      logical_coords);
    }
}

std::uint64_t SoCInfo::get_dram_buffer_noc_address(const std::uint64_t dram_buf_addr,
                                                   const ChipId chip_id,
                                                   const unsigned int channel,
                                                   const unsigned int subchannel) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

    tt_xy_pair noc_dram_core = chip_soc_desc->dram_cores.at(channel).at(subchannel);

    return NOC_XY_ADDR(noc_dram_core.x, noc_dram_core.y, dram_buf_addr);
}

std::uint64_t SoCInfo::get_buffer_noc_address_through_pcie(const std::uint64_t pcie_buf_addr,
                                                           const ChipId chip_id) const
{
    tt_cxy_pair noc_pcie_core = get_first_pcie_core_physical_location(chip_id);

    return NOC_XY_ADDR(noc_pcie_core.x, noc_pcie_core.y, pcie_buf_addr);
}

tt_cxy_pair SoCInfo::get_first_pcie_core_physical_location(const ChipId chip_id) const
{
    const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

    // TODO: Some test SoC descriptors used for testing do not define PCIe cores list and rely on pipegen to populate it
    // with default PCIe core. We should edit those SoC descriptors to always define the PCIe cores and not rely on
    // pipegen to handle empty lists with default values.
    // TODO: Today pipegen always uses the first PCIe core. In future if we support multiple PCIe cores, we can add
    // logic to choose between those.
    tt_xy_pair noc_pcie_core = chip_soc_desc->pcie_cores.empty() ?
        tt_xy_pair(soc_info_constants::default_pcie_core_x, soc_info_constants::default_pcie_core_y) :
        chip_soc_desc->pcie_cores.front();

    return tt_cxy_pair(chip_id, noc_pcie_core);
}

std::uint64_t SoCInfo::get_host_noc_address_through_pcie(const std::uint64_t host_pcie_buf_addr,
                                                         const ChipId chip_id) const
{
    std::uint64_t host_noc_address = get_buffer_noc_address_through_pcie(host_pcie_buf_addr, chip_id);

    // All reads over PCIe go through an Address Translation Unit (ATU), that translates the requested address to one in
    // host system (hugepage) memory. Due to some hardware constraint in WH an additional offset must be applied when
    // WH is reading from host over PCIe.
    if (get_device_arch() == tt::ARCH::WORMHOLE || get_device_arch() == tt::ARCH::WORMHOLE_B0)
    {
        host_noc_address += soc_info_constants::wh_pcie_host_noc_address_offset;
    }

    return host_noc_address;
}

std::uint64_t SoCInfo::get_local_dram_buffer_noc_address(const std::uint64_t dram_buf_noc_addr) const
{
    return dram_buf_noc_addr & ((1ULL << NOC_ADDR_LOCAL_BITS) - 1);
}

std::uint64_t SoCInfo::get_local_pcie_buffer_address(const std::uint64_t l1_buffer_address,
                                                     const tt_cxy_pair& worker_core_location,
                                                     const bool is_pcie_downstream) const
{
    std::uint64_t local_pcie_buffer_address =
        l1_buffer_address +
        (is_pcie_downstream ? soc_info_constants::pcie_downstream_offset : soc_info_constants::pcie_upstream_offset);

    std::size_t tlb_index =
        worker_core_location.y * soc_info_constants::tlb_index_core_y_multiplier + worker_core_location.x;

    int l1_offset = std::ceil(std::log2(l1_mem::address_map::MAX_SIZE));
    l1_offset = 1 << l1_offset;

    local_pcie_buffer_address += tlb_index * l1_offset;

    return local_pcie_buffer_address;
}

bool SoCInfo::is_harvested_chip_with_noc_translation(const ChipId& chip_id) const
{
    const buda_SocDescriptor* soc_descriptor = get_soc_descriptor(chip_id);

    // Grid size of 32x32 is an indication that harvested SoC descriptor is used.
    const bool is_harvested_soc_descriptor = soc_descriptor->grid_size.y == 32 && soc_descriptor->grid_size.x == 32;

    // NOC translation is enabled on Wormhole and Blackhole.
    tt::ARCH device_arch = get_device_arch();
    const bool does_arch_support_noc_translation = (device_arch == tt::ARCH::WORMHOLE ||
                                                    device_arch == tt::ARCH::WORMHOLE_B0 ||
                                                    device_arch == tt::ARCH::BLACKHOLE);

    return does_arch_support_noc_translation && is_harvested_soc_descriptor;
}

tt_cxy_pair SoCInfo::convert_harvested_core_location_to_unharvested(const tt_cxy_pair& harvested_core_location) const
{
    if (!is_harvested_chip_with_noc_translation(harvested_core_location.chip))
    {
        return harvested_core_location;
    }

    tt_cxy_pair unharvested_core_location = harvested_core_location;

    // Shift back x coordinates, leaving space for dedicated dram cores.
    unharvested_core_location.x -= soc_info_constants::wh_harvested_translation_offset;
    if (unharvested_core_location.x <= soc_info_constants::wh_dram_cores_column)
    {
        --unharvested_core_location.x;
    }

    // Shift back translated ethernet rows to their default positions.
    if (harvested_core_location.y == soc_info_constants::wh_harvested_translation_offset)
    {
        unharvested_core_location.y = soc_info_constants::wh_eth_cores_first_row;
    }
    else if (harvested_core_location.y == soc_info_constants::wh_harvested_translation_offset + 1)
    {
        unharvested_core_location.y = soc_info_constants::wh_eth_cores_second_row;
    }
    else
    {
        // Shift back everything else, leaving space for dedicated ethernet cores.
        unharvested_core_location.y -= soc_info_constants::wh_harvested_translation_offset;
        if (unharvested_core_location.y <= soc_info_constants::wh_eth_cores_second_row)
        {
            --unharvested_core_location.y;
        }
    }

    return unharvested_core_location;
}

tt_cxy_pair SoCInfo::convert_unharvested_core_location_to_harvested(const tt_cxy_pair& unharvested_core_location) const
{
    if (!is_harvested_chip_with_noc_translation(unharvested_core_location.chip))
    {
        return unharvested_core_location;
    }

    tt_cxy_pair harvested_core_location = unharvested_core_location;

    // Shift all unharvested x coordinates to the right, eliminating dram cores from harvested coordinate system.
    harvested_core_location.x += soc_info_constants::wh_harvested_translation_offset;
    if (unharvested_core_location.x < soc_info_constants::wh_dram_cores_column)
    {
        ++harvested_core_location.x;
    }

    // Shift ethernet rows to the beginning of translated coordinate system.
    if (unharvested_core_location.y == soc_info_constants::wh_eth_cores_first_row)
    {
        harvested_core_location.y = soc_info_constants::wh_harvested_translation_offset;
    }
    else if (unharvested_core_location.y == soc_info_constants::wh_eth_cores_second_row)
    {
        harvested_core_location.y = soc_info_constants::wh_harvested_translation_offset + 1;
    }
    else
    {
        // Shift other cores to come after ethernet cores in harvested coordinate system.
        harvested_core_location.y += soc_info_constants::wh_harvested_translation_offset;
        if (unharvested_core_location.y < soc_info_constants::wh_eth_cores_second_row)
        {
            ++harvested_core_location.y;
        }
    }

    return harvested_core_location;
}

const buda_SocDescriptor* SoCInfo::get_soc_descriptor(const ChipId chip_id) const
{
    const auto soc_descriptor_it = m_soc_descriptors.find(chip_id);
    log_assert(
        soc_descriptor_it != m_soc_descriptors.end(),
        "Expecting to find SoC descriptor for chip id {}", chip_id);

    return soc_descriptor_it->second.get();
}

const std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> SoCInfo::get_soc_decriptors() const
{
    std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> soc_descriptor;
    for (auto& soc_descriptor_it : m_soc_descriptors)
    {
        soc_descriptor.emplace(static_cast<tt::chip_id_t>(soc_descriptor_it.first), soc_descriptor_it.second.get());
    }

    return soc_descriptor;
}

} // namespace pipegen2