// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "pipegen2_location_utils.h"

#include "common/buda_soc_descriptor.h"
#include "model/pipe_graph/pipe_graph.h"
#include "utils/logger.hpp"

namespace pipegen2 {
tt_cxy_pair convert_physical_core_to_logical(
    const tt_cxy_pair& physical_core,
    const std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> sdesc_per_chip) {
    if (PipeGraph::is_unmapped_location(physical_core)) {
        return physical_core;
    }

    const tt::chip_id_t chip_id = physical_core.chip;
    log_assert(sdesc_per_chip.find(chip_id) != sdesc_per_chip.end(), "Expecting to find SoC descriptor for chip {}",
               chip_id);
    const buda_SocDescriptor* soc_desc_for_chip = sdesc_per_chip.at(chip_id);

    if (soc_desc_for_chip->is_ethernet_core(physical_core)) {
        return physical_core;
    }

    return tt_cxy_pair(chip_id, soc_desc_for_chip->get_worker_core(physical_core));
}

tt_cxy_pair convert_physical_core_to_logical(
    const tt_cxy_pair& physical_core, const std::unordered_map<tt::chip_id_t, buda_SocDescriptor>& sdesc_per_chip) {
    std::unordered_map<tt::chip_id_t, const buda_SocDescriptor*> sdesc_per_chip_pointer_map;
    for (const auto& [chip_id, soc_descriptor] : sdesc_per_chip) {
        sdesc_per_chip_pointer_map.emplace(chip_id, &(soc_descriptor));
    }

    return convert_physical_core_to_logical(physical_core, sdesc_per_chip_pointer_map);
}

}  // namespace pipegen2
