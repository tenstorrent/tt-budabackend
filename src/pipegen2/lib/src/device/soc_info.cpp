// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/soc_info.h"

#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "common/buda_soc_descriptor.h"
#include "l1_address_map.h"
#include "noc/noc_parameters.h"

#include "pipegen2_utils.h"

namespace pipegen2
{
    const std::string SoCInfo::s_multi_soc_descriptors_prefix = "chip_descriptors:";

    std::unique_ptr<SoCInfo> SoCInfo::parse_from_yaml(const std::string& soc_descriptors_yaml_path,
                                                      const std::vector<ChipId>& chip_ids)
    {
        std::unique_ptr<SoCInfo> soc_info(new SoCInfo());
        std::map<ChipId, std::string>
        soc_descriptors_paths = get_soc_descriptors_paths(soc_descriptors_yaml_path, chip_ids);

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

        if (current_line.find(s_multi_soc_descriptors_prefix) == std::string::npos)
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
                std::vector<std::string> line_parts((std::istream_iterator<std::string>(line_stream)),
                    std::istream_iterator<std::string>());
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
                    throw std::runtime_error(
                        "SoCInfo: Expecting multi SoC descriptor chips to match chip IDs found in pipegen.yaml!");
                }
            }
        }

        soc_descriptors_yaml.close();

        if (soc_descriptors_paths.empty())
        {
            throw std::runtime_error("SoCInfo: No SoC descriptor paths found");
        }

        return soc_descriptors_paths;
    }

    SoCInfo::~SoCInfo() = default;

    tt::ARCH SoCInfo::get_device_arch() const
    {
        ASSERT(!m_soc_descriptors.empty(), "Expecting to find at least one soc descriptor");

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

    std::vector<tt_cxy_pair> SoCInfo::get_eth_cores_physical_locations(const ChipId chip_id) const
    {
        const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

        std::vector<tt_cxy_pair> eth_cores_physical_locations;
        for (const tt_xy_pair& core_physical_coords : chip_soc_desc->ethernet_cores)
        {
            eth_cores_physical_locations.push_back(tt_cxy_pair(chip_id, core_physical_coords));
        }

        return eth_cores_physical_locations;
    }

    tt_cxy_pair SoCInfo::get_ethernet_channel_core_physical_location(const ChipId chip_id,
                                                                     const int ethernet_channel) const
    {
        const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

        const std::vector<tt_xy_pair>& chip_eth_cores = chip_soc_desc->ethernet_cores;
        ASSERT(ethernet_channel >= 0 && ethernet_channel < chip_eth_cores.size(), "Ethernet channel out of bounds");

        return tt_cxy_pair(chip_id, chip_eth_cores[ethernet_channel]);
    }

    std::vector<std::vector<tt_cxy_pair>> SoCInfo::get_dram_cores_physical_locations(const ChipId chip_id) const
    {
        const tt_SocDescriptor* chip_soc_descriptor = get_soc_descriptor(chip_id);

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
        std::vector<std::vector<tt_cxy_pair>> dram_cores_physical_locations =
            get_dram_cores_physical_locations(chip_id);
        std::vector<tt_cxy_pair> dram_cores_first_subchannel_locations;

        for (const std::vector<tt_cxy_pair>& dram_core_subchannels_locations : dram_cores_physical_locations)
        {
            ASSERT(!dram_core_subchannels_locations.empty(), "Expecting subchannels to exist!");
            dram_cores_first_subchannel_locations.push_back(dram_core_subchannels_locations.front());
        }

        return dram_cores_first_subchannel_locations;
    }

    tt_cxy_pair SoCInfo::convert_logical_to_physical_worker_core_coords(const tt_cxy_pair& logical_coords) const
    {
        const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(logical_coords.chip);

        // Routing cores are superset of worker cores.
        // TODO: rename function to convert_logical_to_physical_routing_core_coords.
        return tt_cxy_pair(logical_coords.chip, chip_soc_desc->get_routing_core(logical_coords));
    }

    unsigned long SoCInfo::get_dram_buffer_noc_address(const unsigned long dram_buf_addr, const ChipId chip_id,
                                                       const unsigned int channel, const unsigned int subchannel) const
    {
        const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);
        tt_xy_pair noc_dram_core = chip_soc_desc->dram_cores.at(channel).at(subchannel);

        return NOC_XY_ADDR(noc_dram_core.x, noc_dram_core.y, dram_buf_addr);
    }

    unsigned long SoCInfo::get_buffer_noc_address_through_pcie(const unsigned long pcie_buf_addr,
                                                               const ChipId chip_id) const
    {
        tt_cxy_pair noc_pcie_core = get_first_pcie_core_physical_location(chip_id);

        return NOC_XY_ADDR(noc_pcie_core.x, noc_pcie_core.y, pcie_buf_addr);
    }

    tt_cxy_pair SoCInfo::get_first_pcie_core_physical_location(const ChipId chip_id) const
    {
        const buda_SocDescriptor* chip_soc_desc = get_soc_descriptor(chip_id);

        // TODO: Pipegen1 uses hardcoded PCIe core location if there is none specified in soc descriptor, with a comment
        //       that it is just a temporary fix. We should investigate if we still need to do that.
        //       Also today pipegen always uses first pcie core. In future if we support multiple pcie cores, we can add
        //       logic to choose between those.
        tt_xy_pair noc_pcie_core = chip_soc_desc->pcie_cores.empty() ?
            tt_xy_pair(c_default_pcie_core_x, c_default_pcie_core_y) : chip_soc_desc->pcie_cores.front();

        return tt_cxy_pair(chip_id, noc_pcie_core);
    }

    unsigned long SoCInfo::get_local_dram_buffer_noc_address(const unsigned long dram_buf_noc_addr) const
    {
        return dram_buf_noc_addr & ((1ULL << NOC_ADDR_LOCAL_BITS) - 1);
    }

    unsigned long SoCInfo::get_local_pcie_buffer_address(const unsigned long l1_buffer_address,
                                                         const tt_cxy_pair& worker_core_location,
                                                         const bool is_pcie_downstream) const
    {
        unsigned long local_pcie_buffer_address =
            l1_buffer_address + (is_pcie_downstream ? c_pcie_downstream_offset : c_pcie_upstream_offset);

        // TODO: Magic numbers from pipegen1, find out what this represents and document it here.
        std::size_t tlb_index = worker_core_location.y * 13 + worker_core_location.x;
        int l1_offset = std::ceil(std::log2(l1_mem::address_map::MAX_SIZE));
        l1_offset = 1 << l1_offset;
        local_pcie_buffer_address += tlb_index * l1_offset;

        return local_pcie_buffer_address;
    }

    unsigned long SoCInfo::get_host_noc_address_through_pcie(const unsigned long host_pcie_buf_addr,
                                                             const ChipId chip_id) const
    {
        unsigned long host_noc_address = get_buffer_noc_address_through_pcie(host_pcie_buf_addr, chip_id);

        // All reads over PCIe go through an Address Translation Unit (ATU), that translates the requested address to one in
        // host system (hugepage) memory. Due to some hardware constraint in WH an additional offset of 0x800000000 must be
        // added when WH is reading from host over PCIe.
        if (get_device_arch() == tt::ARCH::WORMHOLE || get_device_arch() == tt::ARCH::WORMHOLE_B0)
        {
            host_noc_address += 0x800000000;
        }

        return host_noc_address;
    }

    bool SoCInfo::is_chip_harvested_wh(const ChipId& chip_id) const
    {
        const buda_SocDescriptor* soc_descriptor = get_soc_descriptor(chip_id);

        // Grid size of 32x32 is an indication that harvested SoC descriptor is used.
        return (get_device_arch() == tt::ARCH::WORMHOLE || get_device_arch() == tt::ARCH::WORMHOLE_B0) &&
            soc_descriptor->grid_size.y == 32 && soc_descriptor->grid_size.x == 32;
    }

    tt_cxy_pair SoCInfo::convert_harvested_core_location_to_unharvested(
        const tt_cxy_pair& harvested_core_location) const
    {
        if (!is_chip_harvested_wh(harvested_core_location.chip))
        {
            return harvested_core_location;
        }

        tt_cxy_pair unharvested_core_location = harvested_core_location;

        // Shift back x coordinates, leaving space for dedicated dram cores.
        unharvested_core_location.x -= c_wh_harvested_translation_offset;
        if (unharvested_core_location.x <= c_wh_dram_cores_column)
        {
            --unharvested_core_location.x;
        }

        // Shift back translated ethernet rows to their default positions.
        if (harvested_core_location.y == c_wh_harvested_translation_offset)
        {
            unharvested_core_location.y = c_wh_eth_cores_first_row;
        }
        else if (harvested_core_location.y == c_wh_harvested_translation_offset + 1)
        {
            unharvested_core_location.y = c_wh_eth_cores_second_row;
        }
        else
        {
            // Shift back everything else, leaving space for dedicated ethernet cores.
            unharvested_core_location.y -= c_wh_harvested_translation_offset;
            if (unharvested_core_location.y <= c_wh_eth_cores_second_row)
            {
                --unharvested_core_location.y;
            }
        }

        return unharvested_core_location;
    }

    tt_cxy_pair SoCInfo::convert_unharvested_core_location_to_harvested(
        const tt_cxy_pair& unharvested_core_location) const
    {
        if (!is_chip_harvested_wh(unharvested_core_location.chip))
        {
            return unharvested_core_location;
        }

        tt_cxy_pair harvested_core_location = unharvested_core_location;

        // Shift all unharvested x coordinates to the right, eliminating dram cores from harvested coordinate system.
        harvested_core_location.x += c_wh_harvested_translation_offset;
        if (unharvested_core_location.x < c_wh_dram_cores_column)
        {
            ++harvested_core_location.x;
        }

        // Shift ethernet rows to the beginning of translated coordinate system.
        if (unharvested_core_location.y == c_wh_eth_cores_first_row)
        {
            harvested_core_location.y = c_wh_harvested_translation_offset;
        }
        else if (unharvested_core_location.y == c_wh_eth_cores_second_row)
        {
            harvested_core_location.y = c_wh_harvested_translation_offset + 1;
        }
        else
        {
            // Shift other cores to come after ethernet cores in harvested coordinate system.
            harvested_core_location.y += c_wh_harvested_translation_offset;
            if (unharvested_core_location.y < c_wh_eth_cores_second_row)
            {
                ++harvested_core_location.y;
            }
        }

        return harvested_core_location;
    }

    const buda_SocDescriptor* SoCInfo::get_soc_descriptor(const ChipId chip_id) const
    {
        const auto soc_descriptor_it = m_soc_descriptors.find(chip_id);
        ASSERT(soc_descriptor_it != m_soc_descriptors.end(), "Expecting to find SoC descriptor for the given chip id");

        return soc_descriptor_it->second.get();
    }
}