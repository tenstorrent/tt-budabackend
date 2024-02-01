// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>
#include <unordered_map>

#include "device/tt_xy_pair.h"

#include "device/perf_info_enums.h"
#include "device/soc_info.h"

namespace pipegen2
{
    // Forward declaration.
    namespace PerfInfoInternal
    {
        class Chip;
    }

    class PerfInfoManager
    {
    public:
        // Constructor. Performance dump level is stored in lower 8 bits of `perf_dump_info` argument passed to the
        // pipegen, while performance mode is stored in higher 8 bits.
        // TODO: fix this in future by passing two separate arguments to pipegen.
        PerfInfoManager(int perf_dump_info, const SoCInfo* soc_info);

        // Destructor. Necessary for forward declarations of classes in smart pointer members.
        ~PerfInfoManager();

        // Main entry method initiating calculation of everything needed for performance info dumping.
        void calculate_dram_perf_info();

        // Returns true if perf info calculation and dump to blob yaml is enabled, based on mode argument passed to the
        // pipegen.
        bool is_perf_dump_enabled() const;

        // Returns mapping from worker core location to info that is written to blob.yaml under `dram_perf_buf_noc_addr`
        // attribute.
        std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> get_dram_perf_buf_noc_addr_info() const;

        // Returns mapping from worker core location to info that is written to blob.yaml under `dram_perf_buf_max_req`
        // attribute.
        std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> get_dram_perf_buf_max_req_info() const;

    private:
        // Adds chip to track.
        void add_chip(std::unique_ptr<PerfInfoInternal::Chip> chip);

        // For each worker on each chip finds appropriate DRAM core to which perf info from the worker will be written.
        // Note that this same mapping is used in HOST spill mode for grouping workers, even though it has nothing to
        // do with DRAM.
        void map_workers_to_dram_banks();

        // Calculates performance information for each worker thread used in L1 spill and DRAM spill modes.
        void calculate_dram_spill_perf_info();

        // Calculates performance information for each worker used in HOST spill mode.
        void calculate_host_spill_perf_info();

        // Holds all chip information.
        const SoCInfo* m_soc_info;

        // User-chosen detail level of performance information that are recorded in runtime.
        PerfDumpLevel m_perf_dump_level;

        // User-chosen performance trace mode.
        PerfDumpMode m_device_perf_mode;

        // List of all chips this manager is calculating performance information for.
        std::vector<std::unique_ptr<PerfInfoInternal::Chip>> m_chips;
    };
}