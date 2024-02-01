// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "router/router_passes.h"
#include "router/router_passes_common.h"
#include "router.hpp"
#include "validators.h"
#include "net2pipe_common.h"

// TODO(snijjar): Put the basic pass infra here (e.g. recording/dumping router details including graph, cluster, resources, etc.)
// Would like to create a lightweight pass wrapper like we had between Sage and spatial 1 where we can automatically run preprocess/postprocess
// for every pass and do things like automatically report the added and removed buffers and changed pipe connectivity for every pass.
//   - this should be relatively trivial to do since the router hides all routing graph updates behind its own API
namespace router {

void log_pass_start(const std::string &pass_name) {
    log_debug(tt::LogRouter, "----------------------------------------------------------------------------------------------------");
    auto ss = std::stringstream();
    ss << "RUNNING_PASS: " << pass_name;
    log_debug(tt::LogRouter, "{:100}", ss.str());
    log_debug(tt::LogRouter, "----------------------------------------------------------------------------------------------------");
}


void dump_l1_memory_usage_report(const Router &router) {
    log_debug(tt::LogRouter, "------------------------------------------------------------------------");
    log_debug(tt::LogRouter, "L1 Memory Report");
    log_debug(tt::LogRouter, "------------------------------------------------------------------------");
    const auto &all_chips = router.get_cluster_graph().cluster_description().get_all_chips();
    auto core_max_mem = std::unordered_map<CoreType, int>();
    for (const auto &[loc, core_desc] : router.get_soc_descriptor().cores) {
        core_max_mem[core_desc.type] = router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(*(all_chips.begin()), loc)).total_l1_size();
    }

    auto core_l1_mem_usage = compute_l1_use_from_buffers(router);
    auto core_buffers = get_cores_used_by_buffers(router);

    const auto &soc_desc = router.get_soc_descriptor();
    const int arch_rows = soc_desc.grid_size.y;
    const int arch_cols = soc_desc.grid_size.x;

    log_debug(tt::LogRouter, "Core Buffers Report");
    for (auto chip : all_chips) {
        for (int r = 0; r < arch_rows; r++) {
            for (int c = 0; c < arch_cols; c++) {
                log_debug(tt::LogRouter, "-- Chip: {}, row: {}, col: {} --------", chip, r, c);
                const auto &core_loc = tt_cxy_pair(chip,c,r);
                log_debug(tt::LogRouter, "\tcore_l1_mem_usage (sum of buffers, excludes extra buffering for gather): {}B", core_l1_mem_usage[core_loc]);
                for (auto id: core_buffers[core_loc]) {
                    const auto &buf = router.get_buffer(id);
                    log_debug(tt::LogRouter, "\t{} -> {} tiles, {} Bytes", id, buf.info().allocated_size_in_tiles(), buf.info().allocated_size_in_bytes());
                }
            }
        }
    }

    log_debug(tt::LogRouter, "Tensix      L1 total max router allocation size: {} Bytes", core_max_mem[CoreType::WORKER]);
    log_debug(tt::LogRouter, "Ethernet    L1 total max router allocation size: {} Bytes", core_max_mem[CoreType::ETH]);
    log_debug(tt::LogRouter, "Router Only L1 total max router allocation size: {} Bytes", core_max_mem[CoreType::ROUTER_ONLY]);

    constexpr int field_width = 10;
    for (auto chip : all_chips) {
        log_debug(tt::LogRouter, "Chip {} memory usage in Bytes", chip);
        {
            auto ss = std::stringstream();
            ss << "  |";
            for (int c = 0; c < arch_cols; c++) {
                ss << "    ";
                ss.width(2);
                ss << c;
                ss.width(field_width - (4 + 2) + 1);
                ss << "|";
            }
            log_debug(tt::LogRouter, "{}", ss.str());
        }

        {
            auto ss = std::stringstream();
            ss << "--|";
            for (int c = 0; c < arch_cols; c++) {
                ss << "----------|";
            }
            log_debug(tt::LogRouter, "{}", ss.str());
        }
        for (int r = 0; r < arch_rows; r++) {
            auto ss = std::stringstream();
            ss.width(2);
            ss << r << "|";
            for (int c = 0; c < arch_cols; c++) {
                ss.width(field_width);
                bool valid_core = soc_desc.cores.find(tt_xy_pair(c,r)) != soc_desc.cores.end();
                int num_allocated_bytes = valid_core ? router.get_cluster_resource_model().get_core_attributes(tt_cxy_pair(chip,c,r)).get_num_allocated_bytes() : -1;
                ss << num_allocated_bytes << "|";
            }
            log_debug(tt::LogRouter, "{}", ss.str());
        }
    }
}


void run_router_passes(Router &router, const ClusterGraph &cluster_graph, const netlist_parser &parsed_netlist) {
    dump_l1_memory_usage_report(router);
    // validate::l1_usage_in_sync(router);

    bool enable_relay_buffer_insertion = router.get_soc_descriptor().arch == tt::ARCH::WORMHOLE &&
        (env_var("WHA0_ENABLE_RELAY_BUFS", false) != 0);
    if (!enable_relay_buffer_insertion) {
        // Dram subchannel assignment done as part of DRAM read relay buffer insertion for WH A0
        log_pass_start("ASSIGN DRAM SUBCHANNELS");
        assign_dram_subchan(router);
        // validate::l1_usage_in_sync(router);
    }

    if (enable_relay_buffer_insertion) {
        log_pass_start("INSERT RELAY BUFFERS TO FROCE DRAM READS ON SAME ROW FOR WH A0");
        insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(router);
        validate_all_buffers_and_pipes_assigned_to_cores(router);
        // validate::l1_usage_in_sync(router);
    }

    if (cluster_graph.cluster_description().chips_have_ethernet_connectivity()) {
        log_pass_start("OPTIMIZE OUT PASS-THROUGH ETHERNET DATACOPY OPS");
        if (env_var("ENABLE_REDUNDANT_ETH_DATACOPY_OPT", 0)) {
            optimize_out_pass_through_ethernet_datacopy_ops(router);
        }

        log_pass_start("INSERT ETHERNET BUFFERS FOR CHIP TO CHIP PIPES");
        insert_ethernet_buffers_for_chip_to_chip_pipes(router, cluster_graph);

        if (enable_relay_buffer_insertion) {
            // Rerurn relay buffer insertion after ethernet buffer insertion. The reason we
            // call it before multichip pass as well is because for WHA0, subchannels must
            // be assigned during relay buffer insertion. Otherwise the pass will try to
            // grab queue core locations to make routing decisions but they won't exist.
            // Rather than complicating the pass by making it aware of subchannel assignment
            // and relay buffers, we'll just do this unless we find a good reason not to,
            // especially since it only applies to WHA0    
            log_pass_start("INSERT RELAY BUFFERS TO FROCE DRAM READS ON SAME ROW FOR WH A0");
            insert_relay_buffers_to_force_dram_reads_on_same_row_for_wh_a0(router);
        }
        validate_all_buffers_and_pipes_assigned_to_cores(router);
        // validate::l1_usage_in_sync(router);
    }

    
    log_pass_start("OP/QUEUE ROUTING PASS");
    op_queue_routing_pass(router);

    bool relay_buffer_upsizing_enabled = !env_var("TT_ENABLE_FW_ETH_STREAMS", 0) && !env_var("TT_DISABLE_RELAY_BUF_UPSIZING", 0);
    // Currently the FE ethernet stream feature doesn't support buffers that are sized > 1 tile.
    // At this point in compile, we won't know which buffers are going to be mapped to FE ethernet streams
    if (relay_buffer_upsizing_enabled) {
        log_pass_start("UPSIZE RELAY BUFFERS");
        upsize_relay_buffers_pass(router, parsed_netlist);
    }
    
    dump_l1_memory_usage_report(router);

    log_pass_start("VALIDATE BUFFERS AND PIPES ASSIGNED TO CORES");
    validate_all_buffers_and_pipes_assigned_to_cores(router);

    resolve_buffer_stream_ids(router);

    log_pass_start("VALIDATE BUFFERS ASSIGNED TO STREAMS");
    validate_all_buffers_assigned_to_streams(router);

    dump_l1_memory_usage_report(router);
}

};  // namespace router
