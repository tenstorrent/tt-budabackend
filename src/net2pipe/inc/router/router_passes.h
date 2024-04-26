// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "router.hpp"
#include "router_types.h"
#include "common/tt_cluster_graph.hpp"

#include <vector>


namespace router {
void run_router_passes(Router &router, const ClusterGraph &cluster_graph, const netlist_parser &parsed_netlist);

void assign_dram_subchan(router::Router &router);
/*
 * This pass will optimize away ethernet datacopy ops that implement pass-through functionality.
 * They'll be replaced with a relay buffer
 */
void optimize_out_pass_through_ethernet_datacopy_ops(Router &router);
void insert_ethernet_buffers_for_chip_to_chip_pipes(Router &router, const ClusterGraph &cluster_graph);
void insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(Router &router);
void upsize_relay_buffers_pass(Router &router, const netlist_parser &parsed_netlist);
void op_queue_routing_pass(Router &router);
void resolve_buffer_stream_ids(Router &router);
};  // namespace router
