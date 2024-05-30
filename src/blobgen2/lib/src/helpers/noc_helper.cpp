// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "helpers/noc_helper.h"

#include "device/soc_info_constants.h"

namespace blobgen2
{

size_t NOCHelper::noc1_x_id(const size_t x) const
{
    if (m_noc_translation_id_enabled && x >= pipegen2::soc_info_constants::wh_harvested_translation_offset)
    {
        return x;
    }
    else
    {
        return m_physical_grid_size.x - 1 - x;
    }
}

size_t NOCHelper::noc1_y_id(const size_t y) const
{
    if (m_noc_translation_id_enabled == 1 && y >= pipegen2::soc_info_constants::wh_harvested_translation_offset)
    {
        return y;
    }
    else
    {
        return m_physical_grid_size.y - 1 - y;
    }
}

// dram addrs consists of (rest | nocX | nocY | addr)
// This function modifies nocX | nocY coordinates in case of NOC1.
uint64_t NOCHelper::modify_dram_noc_addr(const uint64_t dram_addr, const NOC_ROUTE noc_id) const
{
    uint64_t addr = dram_addr & c_noc_addr_local_mask;
    uint64_t noc_x = (dram_addr >> NOC_ADDR_LOCAL_BITS) & c_noc_id_mask;
    uint64_t noc_y = (dram_addr >> (NOC_ADDR_LOCAL_BITS + NOC_ID_WIDTH)) & c_noc_id_mask;
    uint64_t rest = dram_addr >> (NOC_ADDR_LOCAL_BITS + NOC_ID_WIDTH + NOC_ID_WIDTH);

    if (noc_id == NOC_ROUTE::NOC1)
    {
        noc_x = noc1_x_id(noc_x);
        noc_y = noc1_y_id(noc_y);
    }

    return (rest << (NOC_ADDR_LOCAL_BITS + NOC_ID_WIDTH + NOC_ID_WIDTH)) |
           (noc_y << (NOC_ADDR_LOCAL_BITS + NOC_ID_WIDTH)) | (noc_x << (NOC_ADDR_LOCAL_BITS)) | (addr);
}
}  // namespace blobgen2