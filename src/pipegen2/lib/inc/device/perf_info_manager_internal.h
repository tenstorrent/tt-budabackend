// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/perf_info_enums.h"
#include "device/soc_info.h"
#include "model/typedefs.h"
// clang-format on

namespace pipegen2
{
namespace perf_info_manager_internal
{

// Abstraction of one RISC processor on Tensix core.
class WorkerThread
{
public:
    // Meaningful naming of RISC processors (so called "threads") on a Tensix worker core.
    enum class ThreadType
    {
        kUnpackerTriscThread = 0,
        kMathTriscThread,
        kPackerTriscThread,
        kNcriscThread,
        kBriscThread
    };

    WorkerThread(uint8_t thread_idx) :
        m_noc_addr(0), m_thread_mem_size(0), m_num_thread_slots(0), m_type(static_cast<ThreadType>(thread_idx))
    {
    }

    ThreadType get_type() const { return m_type; }

    uint64_t get_total_mem_size() const { return m_thread_mem_size * m_num_thread_slots; }

    uint64_t get_noc_addr() const { return m_noc_addr; }

    uint64_t get_num_slots() const { return m_num_thread_slots; }

    void set_noc_addr(uint64_t noc_addr) { m_noc_addr = noc_addr; }

    void set_mem_size(uint64_t mem_size) { m_thread_mem_size = mem_size; }

    void set_num_slots(uint64_t num_slots) { m_num_thread_slots = num_slots; }

private:
    // Meaninguful thread type.
    ThreadType m_type;

    // Address of this thread in perf buffer designated for core it is located on.
    uint64_t m_noc_addr;

    // Memory this thread takes up in perf buffer before expanding to max allowed size.
    uint64_t m_thread_mem_size;

    // Factor of how many times this thread can fit in memory designated for single worker in perf buffer.
    uint64_t m_num_thread_slots;
};

// Data structure holding performance attributes used in HOST spill mode.
class HostSpillModeAttributes
{
public:
    void set_attributes(
        uint64_t selected_worker_queue_header_addr, uint64_t host_trace_info, uint64_t host_dest_address);

    std::vector<uint64_t> get_attributes() const;

private:
    // Address of read and write pointer stored on the first core of host spill group.
    uint64_t m_selected_worker_queue_header_addr;

    // Information about HOST trace packed into 64bit word.
    uint64_t m_host_trace_info;

    // NOC address of the HOST to send perf data to over PCIe.
    uint64_t m_host_dest_address;
};

// Abstraction of one Tensix core.
class WorkerCore
{
public:
    WorkerCore(tt_cxy_pair location);

    const tt_cxy_pair& get_location() const { return m_location; }

    // Non-const getter to serve manager class when filling in info about worker threads.
    std::vector<WorkerThread>& get_threads() { return m_threads; }

    // Const getter to serve manager class when reading info about worker threads.
    const std::vector<WorkerThread>& get_threads() const { return m_threads; }

    // Returns vector of NOC addresses for each thread on this worker core.
    std::vector<uint64_t> get_threads_noc_addresses() const;

    // Returns vector of number of slots for each thread on this worker core.
    std::vector<uint64_t> get_threads_num_slots() const;

    // Sets WorkerCore attributes used in kHostSpillPerfTrace mode.
    void set_host_spill_mode_attributes(
        uint64_t selected_worker_queue_header_addr, uint64_t host_trace_info, uint64_t host_dest_address);

    // Returns vector of attributes used in kHostSpillPerfTrace mode.
    std::vector<uint64_t> get_host_spill_mode_attributes() const;

private:
    // Physical location of this core.
    tt_cxy_pair m_location;

    // List of threads (RISC processors) on this worker core. Their attributes get populated and used for perf
    // measurement in kL1PerfTrace and kDramSpillTrace modes.
    std::vector<WorkerThread> m_threads;

    // Structure holding all info populated and used for perf measurement in kHostSpillPerfTrace mode.
    HostSpillModeAttributes m_host_spill_mode_info;
};

// Abstraction of one DRAM core which keeps track of worker cores associated with itself.
class DramCore
{
public:
    DramCore(tt_cxy_pair location) : m_location(location) {}

    const tt_cxy_pair& get_location() const { return m_location; }

    // Adds worker core to track.
    void add_worker(WorkerCore&& worker_core) { m_worker_cores.push_back(std::move(worker_core)); }

    // Non-const getter to serve manager class when filling in info about worker threads.
    std::vector<WorkerCore>& get_worker_cores() { return m_worker_cores; }

    // Const getter to serve manager class when reading info about worker threads.
    const std::vector<WorkerCore>& get_worker_cores() const { return m_worker_cores; }

private:
    // Physical location of this core.
    tt_cxy_pair m_location;

    // List of workers mapped to this DRAM bank.
    std::vector<WorkerCore> m_worker_cores;
};

// Abstraction of entire chip which keeps track of DRAM cores on it.
class Chip
{
public:
    Chip(ChipId id) : m_id(id) {}

    const ChipId& get_id() const { return m_id; }

    // Adds dram core to track.
    void add_dram_core(DramCore&& dram_core) { m_dram_cores.push_back(std::move(dram_core)); }

    // Non-const getter to serve manager class when filling in info about worker threads.
    std::vector<DramCore>& get_dram_cores() { return m_dram_cores; }

    // Const getter to serve manager class when reading info about worker threads.
    const std::vector<DramCore>& get_dram_cores() const { return m_dram_cores; }

private:
    // ID of this chip.
    ChipId m_id;

    // List of all DRAM cores on this chip to which perf info is written.
    std::vector<DramCore> m_dram_cores;
};

// Returns mapping from DRAM core locations to vector of worker cores that write perf info to that DRAM core.
std::unordered_map<tt_cxy_pair, std::vector<tt_cxy_pair>> map_workers_to_dram_banks_on_chip(
    const ChipId chip_id, const SoCInfo* soc_info);

// Finds nearest DRAM core from list of cores upper-left to the worker core location.
tt_cxy_pair get_nearest_dram_core(
    const tt_cxy_pair& worker_core_location, std::vector<tt_cxy_pair>& dram_cores_locations, const SoCInfo* soc_info);

// Calculates how much space does a single worker take up in DRAM bank's perf buffer.
uint32_t calculate_worker_mem_size(std::size_t num_workers);

// For each thread/RISC in a worker calculates how much space it takes up in perf buffer.
void calculate_worker_threads_mem_size(
    std::vector<WorkerCore>& workers, uint32_t perf_buf_worker_mem_size, PerfDumpLevel perf_dump_level);

// For each thread in each worker mapped to this dram bank calculates its NOC address.
void calculate_worker_threads_noc_addr(
    std::vector<WorkerCore>& workers, const DramCore& dram_bank, uint64_t perf_buf_worker_mem_size);

// Calculates info that is stored in workers' attributes.
void calculate_workers_host_spill_mode_info(
    std::vector<WorkerCore>& workers,
    uint64_t num_host_queue_slots,
    uint64_t host_dest_address,
    const SoCInfo* soc_info);

// Calculates how much space a single worker thread takes up in DRAM bank's perf buffer.
uint32_t calculate_thread_mem_size(WorkerThread::ThreadType thread_type, PerfDumpLevel perf_dump_level);

// Calculates number of queue slots on HOST in which perf info will be stored.
uint32_t calculate_num_host_queue_slots(PerfDumpLevel perf_dump_level);

// Returns info about HOST perf trace packed into 64bit word.
uint64_t calculate_host_trace_info(
    const tt_cxy_pair& unharvested_worker_core_location,
    const uint32_t num_host_queue_slots,
    const bool is_first_core_in_bank);

// Returns NOC address of the queue on HOST to which this worker group writes perf data over PCIe.
uint64_t calculate_host_dest_address(const ChipId chip_id, const uint8_t worker_group_index, const SoCInfo* soc_info);

}  // namespace perf_info_manager_internal
}  // namespace pipegen2