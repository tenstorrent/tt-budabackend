// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/perf_info_manager.h"

#include <iostream>

#include "dram_address_map.h"
#include "host_mem_address_map.h"
#include "noc/noc_parameters.h"

#include "device/perf_info_manager_internal.h"

namespace pipegen2
{
    using namespace PerfInfoInternal;

    PerfInfoManager::PerfInfoManager(int perf_dump_info, const SoCInfo* soc_info) :
        m_perf_dump_level(static_cast<PerfDumpLevel>(perf_dump_info & 0xFF)),
        m_device_perf_mode(static_cast<PerfDumpMode>((perf_dump_info >> 8) & 0xFF)),
        m_soc_info(soc_info)
    {
    }

    PerfInfoManager::~PerfInfoManager() {}

    std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> PerfInfoManager::get_dram_perf_buf_noc_addr_info() const
    {
        std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> workers_to_info;

        for (const std::unique_ptr<Chip>& chip : m_chips)
        {
            for (const DramCore& dram_core : chip->get_dram_cores())
            {
                for (const WorkerCore& worker : dram_core.get_worker_cores())
                {
                    if (m_device_perf_mode == PerfDumpMode::kHostSpillPerfTrace)
                    {
                        workers_to_info[worker.get_location()] = worker.get_host_spill_mode_attributes();
                    }
                    else
                    {
                        workers_to_info[worker.get_location()] = worker.get_threads_noc_addresses();
                    }
                }
            }
        }

        return workers_to_info;
    }

    std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> PerfInfoManager::get_dram_perf_buf_max_req_info() const
    {
        std::unordered_map<tt_cxy_pair, std::vector<uint64_t>> workers_to_info;

        for (const std::unique_ptr<Chip>& chip : m_chips)
        {
            for (const DramCore& dram_core : chip->get_dram_cores())
            {
                for (const WorkerCore& worker : dram_core.get_worker_cores())
                {
                    if (m_device_perf_mode == PerfDumpMode::kHostSpillPerfTrace)
                    {
                        // Set to all zeros, `dram_perf_buf_max_req_info` not used in this mode.
                        workers_to_info[worker.get_location()] = {0, 0, 0, 0, 0};
                    }
                    else
                    {
                        workers_to_info[worker.get_location()] = worker.get_threads_num_slots();
                    }
                }
            }
        }

        return workers_to_info;
    }

    bool PerfInfoManager::is_perf_dump_enabled() const
    {
        return m_device_perf_mode != PerfDumpMode::kDisabled;
    }

    void PerfInfoManager::calculate_dram_perf_info()
    {
        // If perf dump is disabled, no need to perform any calculations.
        if (!is_perf_dump_enabled())
        {
            return;
        }

        map_workers_to_dram_banks();

        // In both L1PerfTrace and DramSpillPerfTrace perf trace info is dumped to DRAM. For L1PerfTrace it happens once
        // for every epoch, while for DramSpillPerfTrace we can dump multiple times during an epoch.
        if (m_device_perf_mode == PerfDumpMode::kL1PerfTrace || m_device_perf_mode == PerfDumpMode::kDramSpillPerfTrace)
        {
            calculate_dram_spill_perf_info();
        }
        else if (m_device_perf_mode == PerfDumpMode::kHostSpillPerfTrace)
        {
            calculate_host_spill_perf_info();
        }
    }

    void PerfInfoManager::add_chip(std::unique_ptr<Chip> chip)
    {
        m_chips.push_back(std::move(chip));
    }

    void PerfInfoManager::map_workers_to_dram_banks()
    {
        for (const ChipId& chip_id : m_soc_info->get_chip_ids())
        {
            // We need to populate dram cores in chip in the same order as pipegen1, to be able to compare generated
            // dram perf info blobs. The reason for this is because in HostSpill mode, host dest address depends on the
            // order of dram cores we iterate through. To be able to achieve the same order, we need to use the same
            // structure (unordered_map) when mapping worker cores to dram banks.
            // TODO: Once we detach from comparison with pipegen1 we can simplify this, since this order is not
            // affecting perf infra functionality and this is done purely to be consistent with pipegen1 output.
            std::unordered_map<tt_cxy_pair, std::vector<tt_cxy_pair>> worker_cores_per_dram_bank =
                map_workers_to_dram_banks_on_chip(chip_id, m_soc_info);

            // Create chip which will keep track of DRAM cores and worker cores mapped to them.
            std::unique_ptr<Chip> chip = std::make_unique<Chip>(chip_id);

            for (auto& dram_core_to_worker_cores : worker_cores_per_dram_bank)
            {
                DramCore dram_bank(dram_core_to_worker_cores.first);

                for (const tt_cxy_pair& worker_location : dram_core_to_worker_cores.second)
                {
                    // Keep track of this worker on DRAM core.
                    dram_bank.add_worker(WorkerCore(worker_location));
                }

                // Keep track of this DRAM core on chip.
                chip->add_dram_core(std::move(dram_bank));
            }

            // Keep track of this chip in manager object.
            add_chip(std::move(chip));
        }
    }

    void PerfInfoManager::calculate_dram_spill_perf_info()
    {
        for (std::unique_ptr<Chip>& chip : m_chips)
        {
            for (DramCore& dram_core : chip->get_dram_cores())
            {
                std::vector<WorkerCore>& workers = dram_core.get_worker_cores();
                uint64_t perf_buf_worker_mem_size = calculate_worker_mem_size(workers.size());
                calculate_worker_threads_mem_size(workers, perf_buf_worker_mem_size, m_perf_dump_level);
                calculate_worker_threads_noc_addr(workers, dram_core, perf_buf_worker_mem_size);
            }
        }
    }

    void PerfInfoManager::calculate_host_spill_perf_info()
    {
        uint32_t num_host_queue_slots = calculate_num_host_queue_slots(m_perf_dump_level);

        for (std::unique_ptr<Chip>& chip : m_chips)
        {
            std::vector<DramCore>& dram_cores = chip->get_dram_cores();

            for (uint8_t dram_bank_idx = 0; dram_bank_idx < dram_cores.size(); dram_bank_idx++)
            {
                uint64_t host_dest_address = calculate_host_dest_address(chip->get_id(), dram_bank_idx, m_soc_info);

                calculate_workers_host_spill_mode_info(
                    dram_cores[dram_bank_idx].get_worker_cores(), num_host_queue_slots, host_dest_address, m_soc_info);
            }
        }
    }
}