// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "device/soc_info.h"
#include "device/tt_xy_pair.h"
#include "pipegen2_exceptions.h"

namespace blobgen2
{

class NOCHelper;

using pipegen2::InvalidSoCInfoYamlException;
using pipegen2::SoCInfo;

// Helper class encapsulating SoC related calculations.
// Holds a couple of functions which encapsulate calculation in which soc info has to be used.
// Also holds a couple of functions which are just pass through to SoCInfo, used only for convenience to hide SoCInfo.
class SoCHelper
{
public:
    SoCHelper(const std::string& soc_descriptors_yaml_path, const std::vector<size_t>& chip_ids);

    uint32_t get_overlay_start_address(const tt_cxy_pair& core_id) const;

    uint32_t get_dummy_phase_address(const tt_cxy_pair& core_id) const;

    NOCHelper get_noc_helper(const size_t& chip_id) const;

    bool is_ethernet_core(const tt_cxy_pair& core_id) const { return m_soc_info->is_ethernet_core(core_id); }

    int get_overlay_version(const size_t& chip_id) const
    {
        return m_soc_info->get_soc_descriptor(chip_id)->overlay_version;
    };

    bool get_noc_translation_id_enabled(const size_t& chip_id) const
    {
        return m_soc_info->get_soc_descriptor(chip_id)->noc_translation_id_enabled;
    };

    tt_xy_pair get_physical_grid_size(const size_t& chip_id) const
    {
        return m_soc_info->get_soc_descriptor(chip_id)->physical_grid_size;
    };

    std::vector<size_t> get_chip_ids() const { return m_soc_info->get_chip_ids(); }

    std::vector<tt_cxy_pair> get_worker_cores(const size_t& chip_id) const
    {
        return m_soc_info->get_worker_cores_physical_locations(chip_id);
    }

    std::vector<tt_cxy_pair> get_ethernet_cores(const size_t& chip_id) const
    {
        return m_soc_info->get_ethernet_cores_physical_locations(chip_id);
    }

    tt::ARCH get_device_arch() const { return m_soc_info->get_device_arch(); }

    const SoCInfo* get_soc_info() const { return m_soc_info.get(); }

private:
    std::string m_soc_descriptors_yaml_path;
    std::unique_ptr<SoCInfo> m_soc_info;
};

}  // namespace blobgen2