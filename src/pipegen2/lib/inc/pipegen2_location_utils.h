// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <unordered_map>

#include "common/base_types.hpp"
#include "device/tt_xy_pair.h"

// Forward declarations.
class buda_SocDescriptor;

namespace pipegen2
{

// Converts given physical core coordinates to logical coordinates.
tt_cxy_pair convert_physical_core_to_logical(
    const tt_cxy_pair& physical_core, const std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> sdesc_per_chip);

// Converts given physical core coordinates to logical coordinates.
tt_cxy_pair convert_physical_core_to_logical(
    const tt_cxy_pair& physical_core, const std::unordered_map<tt::chip_id_t, buda_SocDescriptor>& sdesc_per_chip);

} // namespace pipegen2
