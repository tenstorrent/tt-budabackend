// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "l1_address_map.h"
#include "model/typedefs.h"
#include "noc_overlay_parameters.h"
#include "noc_parameters.h"

namespace blobgen2
{

using pipegen2::NOC_ROUTE;

constexpr static uint64_t c_noc_id_mask = (((uint64_t)1) << NOC_ID_WIDTH) - ((uint64_t)1);
constexpr static uint64_t c_noc_addr_local_mask = (((uint64_t)1) << NOC_ADDR_LOCAL_BITS) - ((uint64_t)1);

// Given that some pointer points currently to "address", calculate the number of bytes needed for it to advance so that
// it is aligned to NOC_ADDRESS_ALIGNMENT.
constexpr inline size_t get_noc_alignment_padding_bytes(uint32_t address)
{
    return (address + NOC_ADDRESS_ALIGNMENT - 1) / NOC_ADDRESS_ALIGNMENT * NOC_ADDRESS_ALIGNMENT;
}

// Helper which encapsulates NOC address related logic used at several places in the code.
class NOCHelper
{
public:
    NOCHelper(const bool noc_translation_id_enabled, const tt_xy_pair& physical_grid_size) :
        m_noc_translation_id_enabled(noc_translation_id_enabled), m_physical_grid_size(physical_grid_size)
    {
    }

    // Returns the NOC coordinates in case of Ethernet transfer.
    tt_xy_pair get_ethernet_noc_coords() const { return {c_noc_id_mask & -1, c_noc_id_mask & -1}; }

    // NOC1 and NOC0 coords are such that when you add them, you get the physical grid size.
    // So this calculates NOC1 coords for the corresponding NOC0 coords.
    // If using harvested chip, this logic is calculated by device, so pass just the original coord.
    tt_xy_pair get_noc1_coords(const tt_xy_pair& noc0_coords) const
    {
        return tt_xy_pair(noc1_x_id(noc0_coords.x), noc1_y_id(noc0_coords.y));
    }

    size_t noc1_x_id(const size_t x) const;
    size_t noc1_y_id(const size_t y) const;

    // dram addrs consists of (rest | nocX | nocY | addr)
    // This function modifies nocX | nocY coordinates in case of NOC1.
    uint64_t modify_dram_noc_addr(const uint64_t dram_addr, const NOC_ROUTE noc_id) const;

private:
    const bool m_noc_translation_id_enabled;
    const tt_xy_pair m_physical_grid_size;
};

}  // namespace blobgen2
