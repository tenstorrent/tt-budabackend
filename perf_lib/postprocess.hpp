// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <iostream>
#include <cstdint>
#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <limits.h>
#include "model/model.hpp"
#include "scratch_api.h"

#include "l1_address_map.h"
#include "dram_address_map.h"
#include "host_mem_address_map.h"
#include "common/base.hpp"
#include "perf_descriptor.hpp"
#include "common/tt_queue_ptr.hpp"
#include "common/buda_soc_descriptor.h"
#include "perf_base.hpp"
#include "perf_state.hpp"

#include "third_party/json/json.hpp"

namespace fs = std::experimental::filesystem;

using digraph = boost::adjacency_list<boost::listS, boost::vecS, boost::bidirectionalS, tt_digraph_node_struct, tt_digraph_edge_struct>;
using vertex_t = boost::graph_traits<digraph>::vertex_descriptor;
using edge_t = boost::graph_traits<digraph>::edge_descriptor;
using json = nlohmann::json;

namespace postprocess {

using namespace perf;

////////////////////////////////////////////////////////
//////////// Concurrent performance trace
////////////////////////////////////////////////////////
extern std::recursive_mutex postprocessor_queue_mutex;
extern std::recursive_mutex report_generator_queue_mutex;

// Following queue is used for synchronizing queues between threads so it must be thread-safe
// Only one thread will increment rd_ptr
// Only one thread will increment wr_ptr
// pointers will not wrap
// This queue is only used to store pointers to the actual data and everytime an element is to be popped
// we delete the memory allocated to that element and will keep the pointer itself
template<typename T>
struct PerfQueue {
    
    uint32_t rd_ptr = 0;
    uint32_t wr_ptr = 0;
    std::queue<T> q;
    std::recursive_mutex &queue_mutex;

    bool empty() {
        const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
        return wr_ptr <= rd_ptr;
    }

    void check_not_empty_and_pop() {
        if (!empty()) {
            const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
            T ptr = q.front();
            q.pop();
            rd_ptr++;
        }
    }

    inline uint32_t get_rd_ptr() {
        const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
        return rd_ptr;
    }
    inline uint32_t get_wr_ptr() {
        const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
        return wr_ptr;
    }

    void push(T val) {
        const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
        log_assert(wr_ptr < UINT32_MAX, "queue is full. wr_ptr = {}", UINT32_MAX);
        q.push(val);
        wr_ptr++;
    }


    inline T front() {
        const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
        log_assert(wr_ptr > rd_ptr, "Attempting to read from the queue but queue is empty");
        return q.front();
    }

    PerfQueue(std::recursive_mutex &queue_mutex): queue_mutex(queue_mutex) {}
    ~PerfQueue() {
        if (!empty()) {
            const std::lock_guard<std::recursive_mutex> lock(queue_mutex);
            while (!q.empty()) {
                q.pop();
            }
            log_error("Unused entries inside perf queue");
        }
    }
};

// Following data-structure, is responsible for generating the performance trace reports.
// The input to this is an epoch_events data-structure which is populated by the PerfHostPostprocessQueue::process_new_device_dump() api.
// PerfQueue<epoch_events> q represents all the epoch_events that has not yet been processed.
// IMPORTANT: The json library that we use IS NOT thread-safe.
// This data structure SHOULD NOT be multi-threaded.
struct PerfHostGenerateReport {
private:
    bool active = false;
    string test_output_dir;
    PerfDesc perf_desc;
    vector<EpochPerfInfo> all_epoch_perf_info;
public:
    const PerfState *perf_state;
    PerfQueue<std::shared_ptr<epoch_events>> q;
    vector<InstructionInfo> all_instruction_info;
    void push_new_epoch_events(std::shared_ptr<epoch_events> epoch_event);
    void generate_all_reports();
    bool run_concurrent_performance_check();
    void create_postprocess_report_concurrent(const std::shared_ptr<epoch_events> epoch_events);
    inline void push_all_epoch_perf_info(vector<EpochPerfInfo> input_all_epoch_perf_info) {
        all_epoch_perf_info = input_all_epoch_perf_info;
    }
    inline const vector<EpochPerfInfo> &get_all_epoch_perf_info() const {
        return all_epoch_perf_info;
    }
    PerfHostGenerateReport(const PerfState *perf_state):
        perf_state(perf_state), q(PerfQueue<std::shared_ptr<epoch_events>>(report_generator_queue_mutex)) {
            test_output_dir = perf_state->get_test_output_dir();
        }
};

// Postprocessor queue
// Host scratch buffer (PerfHostScratchBuffer) feeds the postprocessor queue
// A separate runtime thread will run perf postprocessor on every entry that is pushed into this queue
// Host scratch buffer pushes tensix thread performance trace dumps in this queue

// Postprocessor queue will collect:
//      Partial thread dumps (inside partial_thread_dumps) until we have all the events for a thread within one epoch
//      Everytime a complete thread dump is collected, it is added to a partial core event map (partial_core_events)
//      If all thread dumps within one core in one epoch are collected, it will be added to a partial epoch event map global_epoch_id_to_epoch_events
//      If all active cores within one epoch are complete, the reports for that epoch are generated
struct PerfHostPostprocessQueue {
private:
    bool active = false;
    std::thread postprocess_queue_thread;
    uint32_t single_thread_dump_size;
    uint32_t host_dump_slot_size;

    PerfHostGenerateReport *report_generator;
    
    unordered_map<tt_cxy_pair, uint32_t> core_coord_to_num_dumps;
    uint32_t total_num_threads_received = 0;
    uint32_t total_num_valid_threads_received = 0;
    uint32_t total_num_full_threads_received = 0;
    uint32_t total_num_full_core_received = 0;
    uint32_t total_num_full_epoch_received = 0;

public:
    PerfState *perf_state;

    PerfQueue<std::shared_ptr<uint32_t>> q;
    map<perf::PerfDumpHeaderWrap, vector<uint32_t> > partial_thread_dumps;
    map<perf::PerfDumpCoreHeaderWrapper, core_events> partial_core_events;
    unordered_map<uint32_t, std::shared_ptr<epoch_events>> global_epoch_id_to_epoch_events;

    PerfHostPostprocessQueue(PerfState *perf_state, uint32_t single_thread_dump_size, PerfHostGenerateReport *report_generator):
        perf_state(perf_state), single_thread_dump_size(single_thread_dump_size),
        q(PerfQueue<std::shared_ptr<uint32_t>>(postprocessor_queue_mutex)),
        report_generator(report_generator) {
        
        host_dump_slot_size = host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP * single_thread_dump_size;
    }
    
    ~PerfHostPostprocessQueue();
    
    inline void push_new_device_perf_dump(const uint32_t* incomming_data_addr) {
        log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Starting to copy a new device performance dump from host mmio mapped region (addr: {}) to the postprocessor queue at rd_ptr: {}, wr_ptr: {}.", (void*)incomming_data_addr, q.get_rd_ptr(), q.get_wr_ptr());
        uint32_t *new_ptr = new uint32_t [host_dump_slot_size / sizeof(uint32_t)];
        std::shared_ptr<uint32_t> shared(new_ptr, [](uint32_t* ptr) { delete[] ptr; });
        std::memcpy(new_ptr, incomming_data_addr, host_dump_slot_size);
        q.push(shared);
        log_debug(tt::LogPerfInfra, "PerfHostPostprocessQueue: Finished copying the new device performance dump from host mmio mapped region (addr: {}) to the postprocessor queue at rd_ptr: {}, wr_ptr: {}.", (void*)incomming_data_addr, q.get_rd_ptr(), q.get_wr_ptr());
    }

    void start_runtime_postprocessor();
    void stop_runtime_postprocessor();
    void process_new_device_dump();
    const void print_final_summary() const;

};

// Host scratch buffer queue

// Size of each entry in this queue = A single tensix thread perf dump size. This changes based on perf_level
// The first word of each entry will be initialized to value perf::perf_scratch_empty_id
// We poll on the first word waiting for perf::valid_thread_dump_start_id
struct PerfHostScratchQueue {
    uint32_t* base_ptr;
    const uint num_slots;
    const uint slot_size_bytes;
    const uint device_id;
    const uint mmio_device_id;
    const uint32_t local_queue_id;
    const uint32_t global_queue_id;
    tt_queue_ptr ptr;
    vector<uint32_t*> qslot_start_addr;
    uint32_t last_word_idx;
    
    const tt_cxy_pair core_with_host_ptrs;
    uint total_num_traces_received = 0;

    PerfHostScratchQueue(uint32_t* base_ptr, const uint num_slots, const uint slot_size_bytes, const uint device_id, const uint mmio_device_id,
                        const uint32_t local_queue_id, const uint global_queue_id, const tt_cxy_pair core_with_host_ptrs):
                        base_ptr(base_ptr), num_slots(num_slots), slot_size_bytes(slot_size_bytes), device_id(device_id), mmio_device_id(mmio_device_id),
                        local_queue_id(local_queue_id), global_queue_id(global_queue_id), ptr(tt_queue_ptr(num_slots)), core_with_host_ptrs(core_with_host_ptrs) {
        log_info(tt::LogPerfInfra, "Initializing Perf Host Scratch Queue for device id {} mapped to mmio device id {}, local queue id {}, global queue id {}, with num_slots {}, base_ptr {}, slot_size_bytes {}", 
                    device_id, mmio_device_id, local_queue_id, global_queue_id, num_slots, (void*)(base_ptr), slot_size_bytes);
        log_assert(num_slots > 0, "Number of slots for perf host scratch queue must be at least 1");
        last_word_idx = slot_size_bytes / sizeof(uint32_t) - 1;
        for (uint i = 0; i < num_slots; i++) {
            qslot_start_addr.push_back(base_ptr + i * (slot_size_bytes / sizeof(uint32_t)));
            qslot_start_addr.back()[0] = perf::perf_scratch_empty_id;
            qslot_start_addr.back()[last_word_idx] = perf::perf_scratch_end_empty_id;
        }
    }

    inline uint32_t* get_current_entry_addr() {
        uint rd_ptr = ptr.get_rd_ptr();
        uint32_t* addr = qslot_start_addr.at(rd_ptr);
        return addr;
    }

    inline uint32_t get_first_word_of_entry() {
        uint32_t* addr = get_current_entry_addr();
        return addr[0];
    }

    inline uint32_t get_last_word_of_entry() {
        uint32_t* addr = get_current_entry_addr();
        return addr[last_word_idx];
    }

    // Read the first word of the current entry and check to see if the value is set to perf::valid_thread_dump_start_id
    // This value indicates that device sent a new thread performance trace
    inline bool check_new_entry() {

        uint32_t entry_first_word = get_first_word_of_entry();
        uint32_t entry_last_word = get_last_word_of_entry();
        log_assert(entry_first_word == perf::perf_scratch_empty_id || entry_first_word == perf::valid_thread_dump_start_id,
                    "PerfHostScratchQueue: First word in device perf dump queue must either be {} (initialized by host) or {} (written by device). Current_value = {} addr = {}, device_id {}, mmio_device_id {}, local_queue_id {}, global_queue_id {}",
                    perf::perf_scratch_empty_id, perf::valid_thread_dump_start_id, entry_first_word, (void*)(get_current_entry_addr()), device_id, mmio_device_id, local_queue_id, global_queue_id);
        return ((entry_first_word == perf::valid_thread_dump_start_id) && (entry_last_word != perf::perf_scratch_end_empty_id));
    }

    inline void update_wr_ptr() {
        ptr.incr_wr();
    }

    inline void update_rd_ptr() {
        ptr.incr_rd();
    }

    // pop last entry by resetting the first word to perf::perf_scratch_empty_id
    inline void free_last_queue_entry() {
        uint32_t* slot_addr = qslot_start_addr.at(ptr.get_rd_ptr());
        slot_addr[0] = perf::perf_scratch_empty_id;
        slot_addr[last_word_idx] = perf::perf_scratch_end_empty_id;
        ptr.incr_rd();
    }

    const void print() const;
};

// Host scratch buffer
// Contains a total number of queues = num_chips * num_dram_banks_each_chip
// Using the same mapping for worker cores to dram banks
// The same group of workers that are assigned to a dram bank will be assigned to a unique host scratch queue

// A separate runtime thread will iterate over all the queues (PerfHostScratchQueue) and polls on the first word of the current entry
// If we see perf::valid_thread_dump_start_id, that entry is copied into the postprocessor queue
struct PerfHostScratchBuffer {

private:
    bool active = false;
    // Following thread will spin waiting for new performance traces
    std::thread host_scratch_buffer_thread;
    // This function is intended for updating the copy of the read pointer on device
    // Cores are grouped together on the device. E.g. Every 15 cores in gs are grouped
    // We use the same mapping of workers to dram cores in --dump-perf-intermediate mode to group cores here
    // The first worker in each group is selected for carrying the copy of host-queue rd/wr pointer
    // The following function will update the read pointer on the first core of each group
    std::function<void(tt_cxy_pair, vector<uint32_t>)> update_host_queue_ptr;
    
    // Total amount of space reserved for performance scratch buffer
    // Allocated on channel-0
    const uint total_size = host_mem::address_map::HOST_PERF_SCRATCH_BUF_SIZE;
    const uint num_devices;

    // Each mmio device will map to a different queue on the host scratch buffer region
    // Maximum number of queues is 6x64 for wh and 8x64 for gs. Each chip will have 6 queues for wh and 8 for gs
    std::unordered_map<uint, uint32_t*> mmio_device_to_perf_scratch_buffer_base_addr;
    PerfHostPostprocessQueue *postprocessor_queue = nullptr;

    // For each mmio device, all remote chips that will be routed to it must be in the ordered set
    std::unordered_map<uint, set<uint> > mmio_device_to_mapped_devices;
    std::unordered_map<uint, uint> mmio_device_to_total_num_dram_channels;
    std::unordered_map<uint, std::vector<PerfHostScratchQueue> > mmio_device_to_device_dump_queues;
    uint num_mmio_devices;

    const bool testing_without_silicon = std::getenv("TT_BACKEND_TEST_PERF_WITHOUT_SILICON") ?
                                            atoi(std::getenv("TT_BACKEND_TEST_PERF_WITHOUT_SILICON")): false;
    
    // Variables for measuring the performance of the thread spinning on the host scratch queues
    uint64_t total_time_stalled_on_postprocessor = 0;
    uint64_t total_time_copying_perf_traces = 0;
    uint64_t total_time_updating_header = 0;
    uint64_t total_num_traces_received = 0;

    inline const uint32_t get_num_queues_each_mmio_device() const {
        return host_mem::address_map::NUM_HOST_PERF_QUEUES;
    }
    inline const uint32_t get_queue_size_bytes() const {
        return host_mem::address_map::HOST_PERF_QUEUE_SLOT_SIZE;
    }
    
    // Determines the total number of slots for each queue (each one of the 6x64 queues on host system memory)
    // The number of slots has to be power of 2. This is necessary because of how the atomic increment is implemented in ncirsc fw
    inline const uint32_t get_queue_num_slots() const {
        uint32_t each_queue_num_slots = get_queue_size_bytes() / slot_size_bytes;
        uint32_t num_queues_pow_2 = 1;
        while (num_queues_pow_2 < each_queue_num_slots) {
            num_queues_pow_2 *= 2;
        }
        num_queues_pow_2 /= 2;
        log_assert(num_queues_pow_2 > 1, "Each host scratch queue must have more than 1 entry available. Current numer = {}", num_queues_pow_2);
        log_assert(num_queues_pow_2 < UINT32_MAX, "Number of slots in each queue must be smaller than {}. Current numer = {}", UINT32_MAX, num_queues_pow_2);
        return num_queues_pow_2;
    }
    inline void check_valid_config() {
        log_assert(l1_mem::address_map::NCRISC_PERF_QUEUE_HEADER_ADDR % 32 == 0, "ncrisc perf base address must be 32B aligned");
        log_assert(l1_mem::address_map::PERF_QUEUE_HEADER_SIZE % sizeof(uint32_t) == 0, "performance queue header size must be divisable by 4b");
    }

public:
    const uint single_thread_dump_size;
    uint slot_size_bytes;

    inline void initialize() {
        log_assert(mmio_device_to_device_dump_queues.size() > 0, "PerfHostScratchBuffer is uninitialized");
        for (auto mmio_it: mmio_device_to_device_dump_queues) {
            log_assert(mmio_it.second.size() > 0, "PerfHostScratchBuffer is uninitialized for mmio device id {}", mmio_it.first);
            for (auto queue: mmio_it.second) {
                log_debug(tt::LogPerfInfra, "Resetting ptr header for core for core {} in address {}",
                    queue.core_with_host_ptrs.str(), l1_mem::address_map::PERF_QUEUE_PTRS);
                vector<uint32_t> reset_vec(l1_mem::address_map::PERF_QUEUE_HEADER_SIZE / sizeof(uint32_t), 0);
                update_host_queue_ptr(queue.core_with_host_ptrs, reset_vec);
            }
        }
    }

    inline void initialize_for_new_mmio_device(uint mmio_device_id) {
        if (mmio_device_to_mapped_devices.find(mmio_device_id) == mmio_device_to_mapped_devices.end()) {
            mmio_device_to_mapped_devices.insert({mmio_device_id, {}});
        }
        if (mmio_device_to_total_num_dram_channels.find(mmio_device_id) == mmio_device_to_total_num_dram_channels.end()) {
            mmio_device_to_total_num_dram_channels.insert({mmio_device_id, 0});
        }
        if (mmio_device_to_device_dump_queues.find(mmio_device_id) == mmio_device_to_device_dump_queues.end()) {
            mmio_device_to_device_dump_queues.insert({mmio_device_id, {}});
        }
    }
    
    // Initializes mmio_device_to_device_dump_queues with base addresses for each host queue
    // Iterates over all mmio chips, for each it iterates over all chips mapped to it, and for each chip,
    // iterates over all dram channels, and increments the host base address every time.
    PerfHostScratchBuffer(
        const std::unordered_map<chip_id_t, buda_SocDescriptor> chip_id_to_sdesc,
        std::unordered_map<uint, set<uint> > mmio_device_to_mapped_devices,
        PerfHostPostprocessQueue *postprocessor_queue,
        std::unordered_map<uint, uint32_t*> mmio_device_to_perf_scratch_buffer_base_addr,
        uint32_t single_thread_dump_size,
        std::function<void(tt_cxy_pair, vector<uint32_t>)> update_host_queue_ptr);
    ~PerfHostScratchBuffer ();

    void start_device_trace_poll();
    void stop_device_trace_poll();
    // Iterates over all queues, and if there are any new entries, memcpy's that trace to a different region of host memory
    void check_all_queues_for_new_dump();
    // Spins over all queues until the "active" signal is set to false
    void spin_on_all_queues();
    
    const void print() const;    
    const void print_final_summary() const;

    inline PerfHostScratchQueue get_queue_for_mmio_at_index(uint mmio_chip_id, uint local_chip_id) {
        return mmio_device_to_device_dump_queues.at(mmio_chip_id).at(local_chip_id);
    }
};

////////////////////////////////////////////////////////
//////////// Perormance file management
////////////////////////////////////////////////////////
string get_perf_out_directory(const string& output_dir, const string& override_perf_output_dir, bool clean);
string get_device_perf_out_directory(const string& output_dir, const perf::PerfDesc& perf_desc, bool clean);
string get_host_perf_out_directory(const string& output_dir, const string& override_perf_output_dir, bool clean);
string get_graph_descriptor_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean);
string get_queue_descriptor_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean);
string get_metadata_out_directory(const string &test_output_dir, const string &override_perf_output_dir, bool clean);
string get_host_profiler_report_path(const string& test_output_dir, const string& override_perf_output_dir, uint pid, uint thread_id, bool clean, bool device_alignment_report);
void remove_perf_output_directory(const string& output_dir, const string& override_perf_output_dir);

////////////////////////////////////////////////////////
//////////// Decoding the event name for event-id
////////////////////////////////////////////////////////
string decode_event_name(int event_id, int thread_id);
string decode_host_event_name(uint64_t event_id, int pid, uint thread_id, const vector<string> &all_labels);
string decode_ncrisc_event_name(int event_id);
string decode_brisc_event_name(int event_id);

// TODO: Move these to create_report.hpp
void print_perf_info_all_epochs(bool print_to_file, const string& output_dir, const PerfState &perf_state);
void create_epoch_info_report(const vector<tt::EpochPerfInfo>& all_epochs_info, const int &input_count, const string& output_path);
vector<EpochPerfInfo> get_epoch_info(const vector<InstructionInfo> &all_instructions_info);

struct EventProperties {
    uint outer_loop_idx;
    uint event_type;
    uint num_tiles;
    uint operand_idx;

    EventProperties(int event_id) {
        outer_loop_idx    = (event_id >> 20)  & 0xfff;
        event_type        = (event_id >> 16)  & 0xf;
        num_tiles         = (event_id >> 8)   & 0xff;
        operand_idx       = event_id          & 0xff;
    }
};

struct HostEventProperties {
    uint event_type;
    uint device_id;
    uint epoch_id;
    uint program_id;
    uint custom_event_label;

    HostEventProperties(uint64_t event_id) {
        custom_event_label  = (event_id >> 40)  & 0xffffff;
        program_id          = (event_id >> 32)  & 0xff;
        epoch_id            = (event_id >> 16)  & 0xffff;
        device_id           = (event_id >> 8)   & 0xff;
        event_type          = event_id          & 0xff;
    }
};

struct BriscEventProperties {
    uint epoch_id;
    uint event_type;
    uint operand_idx;

    BriscEventProperties(int event_id) {
        event_type        = (event_id)          & 0xf;
        epoch_id          = (event_id >> 4)     & 0xffff;
        operand_idx       = (event_id >> 20)    & 0xff;
    }
};


////////////////////////////////////////////////////////
//////////// Perf-Postprocess apis
////////////////////////////////////////////////////////
// Device postprocess
bool run_perf_postprocess(const map<tt_cxy_pair, vector<uint32_t>> &all_dram_events, PerfState &perf_state, const string& output_dir, std::shared_ptr<tt_device> device, const std::unordered_map<chip_id_t, buda_SocDescriptor>& sdesc_per_chip);
// Host postprocess
void run_host_perf_postprocess(
    const vector<uint64_t> &target_buffer,
    const vector<string> &all_labels,
    const string &test_output_dir,
    const string &perf_output_dir,
    const pid_t &process_id,
    const bool &dump_device_alignment,
    const int &total_input_count);
// Concurrent postprocess
thread_events run_postprocessor_single_thread_epoch(perf::PerfDumpHeader header, vector<uint32_t> perf_data);
// Analyzer: Runs the python postprocessor
void run_performance_analyzer(const PerfState &device_perf_state, const string &test_output_dir);

}
