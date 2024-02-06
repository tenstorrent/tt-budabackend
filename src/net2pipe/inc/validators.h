// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "netlist/netlist.hpp"
#include "common/buda_soc_descriptor.h"
#include "device/tt_cluster_descriptor.h"
#include "router.hpp"

using router::Router;
namespace validate {

    bool netlist_fits_on_arch(const netlist_parser &netlist, const buda_SocDescriptor &soc_descriptor);
    void validate_netlist_fits_on_arch(const netlist_parser &netlist, const buda_SocDescriptor &soc_descriptor);
    void validate_netlist_fits_on_cluster(const netlist_parser &netlist, const tt_ClusterDescriptor &cluster_descriptor);
    void l1_usage_in_sync(const Router &router);
    void unique_id_range_covers_all_buffers(
        std::map<std::uint64_t, router::router_buffer_info_t> const &buffer_map,
        std::unordered_map<std::uint64_t, tt_queue_info> const &queue_unique_id_info_map);

}; // namespace validator