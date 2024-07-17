// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/perf_info_manager_internal.h"

#include <cmath>
#include <cstdint>

#include "dram_address_map.h"
#include "host_mem_address_map.h"
#include "l1_address_map.h"
#include "noc/noc_parameters.h"
#include "perf_lib/scratch_api.h"
#include "utils/logger.hpp"

namespace pipegen2
{
namespace perf_info_manager_internal
{

void HostSpillModeAttributes::set_attributes(
    uint64_t selected_worker_queue_header_addr, uint64_t host_trace_info, uint64_t host_dest_address)
{
    m_selected_worker_queue_header_addr = selected_worker_queue_header_addr;
    m_host_trace_info = host_trace_info;
    m_host_dest_address = host_dest_address;
}

std::vector<uint64_t> HostSpillModeAttributes::get_attributes() const
{
    std::vector<uint64_t> host_spill_attributes{
        m_selected_worker_queue_header_addr,
        m_host_trace_info,
        m_host_dest_address,
        0, /* Unused by blobgen in host spill mode, returned because array of size 5 is expected. */
        0  /* Unused by blobgen in host spill mode, returned because array of size 5 is expected. */
    };
    return host_spill_attributes;
}

WorkerCore::WorkerCore(tt_cxy_pair location) : m_location(location)
{
    // Number of threads is fixed and same for all workers.
    constexpr uint8_t c_num_threads = l1_mem::address_map::PERF_NUM_THREADS;

    m_threads.reserve(c_num_threads);

    for (uint8_t thread_idx = 0; thread_idx < c_num_threads; thread_idx++)
    {
        m_threads.emplace_back(thread_idx);
    }
}

std::vector<uint64_t> WorkerCore::get_threads_noc_addresses() const
{
    std::vector<uint64_t> threads_noc_addresses;

    for (const WorkerThread& thread : m_threads)
    {
        threads_noc_addresses.push_back(thread.get_noc_addr());
    }

    return threads_noc_addresses;
}

std::vector<uint64_t> WorkerCore::get_threads_num_slots() const
{
    std::vector<uint64_t> threads_num_slots;

    for (const WorkerThread& thread : m_threads)
    {
        threads_num_slots.push_back(thread.get_num_slots());
    }

    return threads_num_slots;
}

void WorkerCore::set_host_spill_mode_attributes(
    uint64_t selected_worker_queue_header_addr, uint64_t host_trace_info, uint64_t host_dest_address)
{
    m_host_spill_mode_info.set_attributes(selected_worker_queue_header_addr, host_trace_info, host_dest_address);
}

std::vector<uint64_t> WorkerCore::get_host_spill_mode_attributes() const
{
    return m_host_spill_mode_info.get_attributes();
}

std::unordered_map<tt_cxy_pair, std::vector<tt_cxy_pair>> map_workers_to_dram_banks_on_chip(
    const ChipId chip_id, const SoCInfo* soc_info)
{
    std::unordered_map<tt_cxy_pair, std::vector<tt_cxy_pair>> worker_cores_per_dram_bank;

    std::vector<tt_cxy_pair> dram_cores_physical_locations_of_first_subchannel =
        soc_info->get_dram_cores_physical_locations_of_first_subchannel(chip_id);

    for (const tt_cxy_pair& worker_core_location : soc_info->get_worker_cores_physical_locations(chip_id))
    {
        tt_cxy_pair dram_core_location =
            get_nearest_dram_core(worker_core_location, dram_cores_physical_locations_of_first_subchannel, soc_info);

        auto it = worker_cores_per_dram_bank.find(dram_core_location);
        if (it == worker_cores_per_dram_bank.end())
        {
            worker_cores_per_dram_bank.insert(
                std::pair<tt_cxy_pair, std::vector<tt_cxy_pair>>(dram_core_location, {worker_core_location}));
        }
        else
        {
            it->second.push_back(worker_core_location);
        }
    }

    return worker_cores_per_dram_bank;
}

tt_cxy_pair get_nearest_dram_core(
    const tt_cxy_pair& worker_core_location, std::vector<tt_cxy_pair>& dram_cores_locations, const SoCInfo* soc_info)
{
    auto is_upper_left = [](const tt_cxy_pair& dram_core_location, const tt_cxy_pair& worker_core_location)
    { return (dram_core_location.x <= worker_core_location.x && dram_core_location.y <= worker_core_location.y); };

    auto distance = [](const tt_cxy_pair& dram_core_location, const tt_cxy_pair& worker_core_location)
    {
        return (
            abs(dram_core_location.x - worker_core_location.x) + abs(dram_core_location.y - worker_core_location.y));
    };

    // Temporarily shift worker location to unharvested coordinate system in order to find nearest upper-left DRAM
    // core (needed due to DRAM cores always being represented in unharvested coordinates in SoC descriptor).
    tt_cxy_pair worker_location = soc_info->convert_harvested_core_location_to_unharvested(worker_core_location);

    tt_cxy_pair nearest_dram_core = dram_cores_locations.front();

    for (const tt_cxy_pair& dram_core_location : dram_cores_locations)
    {
        // If dram core is upper-left to the worker, compare Manhattan distances of this and currently nearest core
        // and choose the one that is closer to the worker.
        if (is_upper_left(dram_core_location, worker_location) &&
            distance(dram_core_location, worker_location) < distance(nearest_dram_core, worker_location))
        {
            nearest_dram_core = dram_core_location;
        }
    }

    return nearest_dram_core;
}

uint32_t calculate_worker_mem_size(std::size_t num_workers)
{
    // to ensure NOC_ADDRESS_ALIGNMENT bytes aligned address,
    // allocate memory in multiples of NOC_ADDRESS_ALIGNMENT bytes.
    return num_workers > 0
               ? (dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_SIZE / num_workers) & ~(NOC_ADDRESS_ALIGNMENT - 1)
               : 0;
}

void calculate_worker_threads_mem_size(
    std::vector<WorkerCore>& workers, uint32_t perf_buf_worker_mem_size, PerfDumpLevel perf_dump_level)
{
    for (WorkerCore& worker : workers)
    {
        // Mem space all threads combined take up.
        uint32_t total_perf_buf_threads_size = 0;

        for (WorkerThread& thread : worker.get_threads())
        {
            uint64_t thread_mem_size = calculate_thread_mem_size(thread.get_type(), perf_dump_level);
            thread.set_mem_size(thread_mem_size);
            total_perf_buf_threads_size += thread_mem_size;
        }

        // Aux variable to calculate how many times thread spaces combined can fit in perf buffer designated for
        // single worker core.
        uint64_t worker_num_thread_slots = perf_buf_worker_mem_size / total_perf_buf_threads_size;
        // Always have space for two consecutive dumps for one thread. Ensure divisibility by 2.
        worker_num_thread_slots -= (worker_num_thread_slots % 2);

        // Trisc increments a request counter that NCRISC polls on. We also have an ACK counter. And we only
        // increment the req counter if it is no less than ACK-2. This is because we double buffer, and to make
        // sure we wait for NCRISC to copy the data to DRAM before overwriting it. Every time we increment the req
        // counter, we switch buffers. NCRISC will stop copying when the ACK counter hits worker_num_thread_slots.
        // To make sure we don’t get any issues where the req counter gets incremented and wraps around, before ACK
        // counter gets updated, this assert is added as safety.
        log_assert(worker_num_thread_slots < (0xFFFF - 4), "Request counter wrapped around!");

        // Set this expansion factor for all threads.
        for (WorkerThread& thread : worker.get_threads())
        {
            thread.set_num_slots(worker_num_thread_slots);
        }
    }
}

uint32_t calculate_thread_mem_size(WorkerThread::ThreadType thread_type, PerfDumpLevel perf_dump_level)
{
    uint64_t thread_mem_size = 0;
    // For each thread, double buffering in L1 is performed, thus we only need to fit half of L1 buffer in DRAM.
    switch (thread_type)
    {
        case WorkerThread::ThreadType::kUnpackerTriscThread:
        case WorkerThread::ThreadType::kPackerTriscThread:
            thread_mem_size = perf_dump_level == PerfDumpLevel::kLevel0
                                  ? l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0 / 2
                                  : l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 / 2;
            break;

        case WorkerThread::ThreadType::kMathTriscThread:
            thread_mem_size = l1_mem::address_map::MATH_PERF_BUF_SIZE / 2;
            break;

        case WorkerThread::ThreadType::kNcriscThread:
            thread_mem_size = perf_dump_level == PerfDumpLevel::kLevel0
                                  ? l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0 / 2
                                  : l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1 / 2;
            break;

        case WorkerThread::ThreadType::kBriscThread:
            thread_mem_size = l1_mem::address_map::BRISC_PERF_BUF_SIZE / 2;
            break;
    }

    return thread_mem_size;
}

void calculate_worker_threads_noc_addr(
    std::vector<WorkerCore>& workers, const DramCore& dram_bank, uint64_t perf_buf_worker_mem_size)
{
    // Set worker perf buf address to the base of this DRAM bank.
    uint32_t perf_buf_worker_addr = dram_mem::address_map::DRAM_EACH_BANK_PERF_BUFFER_BASE;

    for (WorkerCore& worker : workers)
    {
        // Calculate NOC address for this worker inside perf buffer.
        uint64_t worker_thread_buf_addr =
            NOC_XY_ADDR(dram_bank.get_location().x, dram_bank.get_location().y, perf_buf_worker_addr);

        for (WorkerThread& thread : worker.get_threads())
        {
            thread.set_noc_addr(worker_thread_buf_addr);
            // Shift address to the next thread.
            worker_thread_buf_addr += thread.get_total_mem_size();
        }

        // Shift perf buf address to the next worker.
        perf_buf_worker_addr += perf_buf_worker_mem_size;
    }
}

void calculate_workers_host_spill_mode_info(
    std::vector<WorkerCore>& workers,
    uint64_t num_host_queue_slots,
    uint64_t host_dest_address,
    const SoCInfo* soc_info)
{
    uint64_t selected_worker_queue_header_addr = NOC_XY_ADDR(
        workers.front().get_location().x, workers.front().get_location().y, l1_mem::address_map::PERF_QUEUE_PTRS);

    for (std::size_t worker_idx = 0; worker_idx < workers.size(); worker_idx++)
    {
        WorkerCore& worker = workers[worker_idx];

        tt_cxy_pair unharvested_worker_location =
            soc_info->convert_harvested_core_location_to_unharvested(worker.get_location());

        uint64_t host_trace_info =
            calculate_host_trace_info(unharvested_worker_location, num_host_queue_slots, worker_idx == 0);

        worker.set_host_spill_mode_attributes(selected_worker_queue_header_addr, host_trace_info, host_dest_address);
    }
}

uint32_t calculate_num_host_queue_slots(PerfDumpLevel perf_dump_level)
{
    const uint32_t thread_dump_size = perf_dump_level == PerfDumpLevel::kLevel0
                                          ? l1_mem::address_map::BRISC_PERF_BUF_SIZE
                                          : l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1 / 2;

    const uint32_t device_dump_size = host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP * thread_dump_size;
    const uint32_t num_host_queue_slots = host_mem::address_map::HOST_PERF_QUEUE_SLOT_SIZE / device_dump_size;

    return static_cast<uint32_t>(std::floor(std::log2(num_host_queue_slots)));
}

uint64_t calculate_host_trace_info(
    const tt_cxy_pair& unharvested_worker_core_location,
    const uint32_t num_host_queue_slots,
    const bool is_first_core_in_bank)
{
    // TODO: this should be done a bit better, not squashing all this info into one 64bit word.
    perf::PerfDumpHeader header{
        .x = uint8_t(unharvested_worker_core_location.x & 0xf),
        .y = uint8_t(unharvested_worker_core_location.y & 0xf),
        .chip_id = uint8_t(unharvested_worker_core_location.chip & 0xff),
        .thread_id = 0,
        .epoch_id = 0};

    // This breaks compalation with G++ 11.4.0
    // uint32_t header_word = *(reinterpret_cast<uint32_t*>(&header));
    perf::PerfDumpHeader* h1 = &header;
    uint32_t* h2 = reinterpret_cast<uint32_t*>(h1);
    uint32_t header_word = *h2;

    uint64_t dram_buf_info =
        (is_first_core_in_bank & 0xff) | ((num_host_queue_slots & 0xff) << 8) | (uint64_t(header_word) << 32);

    return dram_buf_info;
}

uint64_t calculate_host_dest_address(const ChipId chip_id, const uint8_t worker_group_index, const SoCInfo* soc_info)
{
    // Each MMIO device has its own hugepage system memory and a separate set of queues. Since we are assuming all
    // chips are MMIO mapped (ie `get_closest_mmio_capable_chip(chip_id)` returns `chip_id` in pipegen 1), offset
    // here is always set to index of the worker group on this very chip. In practice, it should point to queue on
    // closest MMIO capable chip.
    // TODO: Revise this once MMIO logic is fixed in pipegen 1.
    const uint64_t host_pcie_buf_addr = host_mem::address_map::HOST_PERF_SCRATCH_BUF_START +
                                        worker_group_index * host_mem::address_map::HOST_PERF_QUEUE_SLOT_SIZE;

    return soc_info->get_host_noc_address_through_pcie(host_pcie_buf_addr, chip_id);
}

}  // namespace perf_info_manager_internal
}  // namespace pipegen2