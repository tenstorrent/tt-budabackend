// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "helpers/soc_helper.h"

#include "epoch.h"
#include "eth_l1_address_map.h"
#include "helpers/noc_helper.h"
#include "l1_address_map.h"

namespace blobgen2
{

SoCHelper::SoCHelper(const std::string& soc_descriptors_yaml_path, const std::vector<size_t>& chip_ids) :
    m_soc_descriptors_yaml_path(soc_descriptors_yaml_path)
{
    try
    {
        m_soc_info = SoCInfo::parse_from_yaml(soc_descriptors_yaml_path, chip_ids);
    }
    catch (const std::exception& e)
    {
        throw InvalidSoCInfoYamlException("Invalid SoC descriptors yaml: " + std::string(e.what()));
    }
}

uint32_t SoCHelper::get_overlay_start_address(const tt_cxy_pair& core_id) const
{
    return is_ethernet_core(core_id) ? eth_l1_mem::address_map::OVERLAY_BLOB_BASE
                                     : l1_mem::address_map::OVERLAY_BLOB_BASE;
}

uint32_t SoCHelper::get_dummy_phase_address(const tt_cxy_pair& core_id) const
{
    return get_overlay_start_address(core_id) + offsetof(epoch_t, dummy_phase_tile_header_and_data);
}

NOCHelper SoCHelper::get_noc_helper(const size_t& chip_id) const
{
    return NOCHelper(get_noc_translation_id_enabled(chip_id), get_physical_grid_size(chip_id));
}

}  // namespace blobgen2