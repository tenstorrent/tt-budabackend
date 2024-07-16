// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime/runtime_io.hpp"
#include "common/cache_lib.hpp"
#include "common/size_lib.hpp"
#include "common/tt_queue_ptr.hpp"
#include "common/error_types.hpp"
#include "device/cpuset_lib.hpp"
#include "common/data_binary_lib.hpp"
#include "mem_lib.hpp"

extern perf::tt_backend_perf backend_profiler;
namespace tt::io {
bool debug = false;

constexpr int TIMEOUT_CHECK_FREQ = 1000;

struct DataFormatPairHash {
    size_t operator()(const std::pair<DataFormat, DataFormat>& p) const {
        uint32_t combined = static_cast<uint32_t>(p.first)<<16 | static_cast<uint32_t>(p.second);
        return std::hash<uint32_t>{}(combined);
    }
};

bool force_sw_tilize(bool default_value = false) {
    return parse_env("TT_BACKEND_FORCE_SW_TILIZE", default_value);
};

tt_cluster *get_cluster(const std::string tag, const tt::DEVICE &backend) {
    if (tt_object_cache<tt_cluster>::exists(tag)) {
        return tt_object_cache<tt_cluster>::get(tag);
    } else {
        log_assert(backend == tt::DEVICE::Silicon, "Multi-processing is only supported for Silicon");
        tt_cluster *cluster = new tt_cluster;

        tt::ARCH arch = static_cast<tt::ARCH>(get_workload(tag)->device_info.arch);
        TargetDevice target_type = static_cast<TargetDevice>(backend);
        auto workload_target_device_ids = get_workload(tag)->compute_target_device_ids();
        
        // Set cluster file if wormhole machine and target type is silicon
        std::string cluster_file = "";

        if ((arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) && (target_type == TargetDevice::Silicon)) {
            cluster_file = tt::io::info.output_dir + "/cluster_desc.yaml";
        }
        cluster->open_device(arch, target_type, workload_target_device_ids, get_soc_description_file(arch, target_type, tt::io::info.output_dir), cluster_file);
        cluster->start_device({.init_device = false});
        tt_object_cache<tt_cluster>::set(tag, cluster);
        log_trace(tt::LogIO, "{} cache MISS, added object to cache with tag '{}'", __FUNCTION__, tag);
        return cluster;
    }
}

tt_runtime_workload *get_workload(const std::string tag) {
    tt_runtime_workload *workload;
    if (tt_object_cache<tt_runtime_workload>::exists(tag)) {
        workload = tt_object_cache<tt_runtime_workload>::get(tag);
    } else {
        workload = new tt_runtime_workload(tag);
        tt_object_cache<tt_runtime_workload>::set(tag, workload);
        log_trace(tt::LogIO, "{} cache MISS, added object to cache with tag '{}'", __FUNCTION__, tag);
    }
    return workload;
}

template<Tilizer tilizer_backend>
DeviceTilizer<tilizer_backend> *get_tilizer(const std::string tag, const std::vector<tt_dram_io_desc> &dram_io_desc, tt_cluster* cluster, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride, const uint32_t tile_height, const uint32_t tile_width) {
    DeviceTilizer<tilizer_backend> *tilizer;
    if (tt_object_cache<DeviceTilizer<tilizer_backend>>::exists(tag)) {
        tilizer = tt_object_cache<DeviceTilizer<tilizer_backend>>::get(tag);
    } else {
        if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
            std::function<void(uint32_t, uint8_t*, int, uint32_t, uint32_t)> spill_to_dram_func = [cluster](uint32_t dram_base_addr, uint8_t* intermed_buf_addr, int num_bytes, uint32_t channel, uint32_t device_id){
                log_assert(num_bytes % 4 == 0, "Expected chunks of 4-bytes to transfer to dram");
                std::vector<uint32_t> vec(reinterpret_cast<uint32_t*>(intermed_buf_addr), reinterpret_cast<uint32_t*>(intermed_buf_addr + num_bytes));
                cluster->write_dram_vec(vec, tt_target_dram(device_id, channel, 0), (uint64_t)dram_base_addr);
            };
            std::function<uint32_t(uint32_t, uint32_t, uint32_t)> read_from_dram_func = [cluster](uint32_t dram_base_addr, uint32_t channel, uint32_t device_id){
                std::vector<uint32_t> vec;
                cluster->read_dram_vec(vec, tt_target_dram(device_id, channel, 0), dram_base_addr, 4);
                return vec.at(0);
            };
            tilizer = new DeviceTilizer<tilizer_backend>(dram_io_desc, spill_to_dram_func, read_from_dram_func, tensor, stride, tile_height, tile_width);
        } else if(tilizer_backend == Tilizer::FastTilizeMMIOPush){
            tilizer = new DeviceTilizer<tilizer_backend>(dram_io_desc, tensor, stride, tile_height, tile_width);
        } else{
            log_assert(false, "Tilizer backend must be FastTilizeDevicePush or FastTilizeMMIOPush for a cached tilizer to be used.");
        }
        tt_object_cache<DeviceTilizer<tilizer_backend>>::set(tag, tilizer);
        log_trace(tt::LogIO, "{} cache MISS, added object to cache with tag '{}'", __FUNCTION__, tag);
    }
    return tilizer;
}

void free_object_cache() {
    // it is safe to destroy an empty cache, hence a super set is used
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::destroy();
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::destroy();
    tt_object_cache<tt_cluster>::destroy();
    tt_object_cache<tt_runtime_workload>::destroy();
}

bool is_host_queue_empty(const tt_dram_io_desc &desc) {
    log_assert(desc.bufq_start_addr_channel.size() == 1, "Expecting an untilized queue! Should only have one buffer queue");
    uint32_t num_entries = desc.bufq_num_slots;
    uint32_t *q_ptr = reinterpret_cast<uint32_t *>(desc.bufq_mapping.front());
    if (q_ptr == nullptr) {
        log_fatal("no host buffer was allocated");
    }

    // read rd_ptr and wr_ptr from queue
    uint16_t *rd_ptr = reinterpret_cast<uint16_t *>(q_ptr);
    uint16_t *wr_ptr = reinterpret_cast<uint16_t *>(q_ptr + 1);

    log_trace(tt::LogIO, "{} -- queue={} buf_addr=0x{:x} buf_ch={} rdptr={} wrptr={}",
        __FUNCTION__, desc.queue_name, desc.bufq_start_addr_channel[0].first, desc.bufq_start_addr_channel[0].second, *rd_ptr, *wr_ptr);
    return (*rd_ptr == *wr_ptr);
}

bool is_dram_queue_empty(const tt_dram_io_desc& desc, const tt_queue_info &queue_info, tt_cluster *cluster) {
    bool empty = true;
    for (auto &buffer : queue_info.alloc_info) {
        tt_target_dram dram = {queue_info.target_device, buffer.channel, 0};
        uint32_t num_entries = desc.bufq_num_slots;

        // read rd_ptr and wr_ptr from queue
        vector<uint32_t> buffer_ptr = {0,0};
        cluster->read_dram_vec(buffer_ptr, dram, buffer.address, 8);
        empty &= (buffer_ptr[0] & 0x0ffff) == (buffer_ptr[1] & 0xffff);
        log_trace(tt::LogIO, "{} -- queue={} buf_addr=0x{:x} buf_ch={} rdptr={} wrptr={}", __FUNCTION__, desc.queue_name, buffer.address, buffer.channel, (buffer_ptr[0] & 0x0ffff), (buffer_ptr[1] & 0xffff));
    }
    return empty;
}

bool is_queue_empty(const tt_dram_io_desc& desc, const tt_queue_info &queue_info, tt_cluster *cluster) {
    if (queue_info.type == IO_TYPE::RandomAccess) {
        return false; // ram is always accessible, ie. never empty
    }

    if (queue_info.loc == QUEUE_LOCATION::HOST) {
        return is_host_queue_empty(desc);
    } else {
        return is_dram_queue_empty(desc, queue_info, cluster);
    }
}

void translate_addresses(tt_dram_io_desc &io_desc) {
    tt_runtime_workload *workload = get_workload(io_desc.netlist_path);
    tt_cluster *cluster = get_cluster(io_desc.netlist_path, io_desc.backend_type);
    tt_queue_info &queue_info = workload->queues[io_desc.queue_name].my_queue_info;

    bool translatable_range = true;
    io_desc.bufq_mapping.clear();

    log_trace(tt::LogIO, "translate_address() with queue_name: {} loc: {} src_device_id: {}", queue_info.name, queue_info.loc, queue_info.src_device_id);
    uint32_t buf_size = tt::size::get_entry_size_in_bytes(io_desc.bufq_target_format, io_desc.layout == IO_LAYOUT::Tilized, io_desc.ublock_ct, io_desc.ublock_rt, io_desc.mblock_m, io_desc.mblock_n, io_desc.t, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info)) * queue_info.entries + tt::io::io_queue_header_size_bytes;
    if (queue_info.loc == QUEUE_LOCATION::DRAM) {
        // For Blackhole, translatable_range should always be true,
        // that is why we are checking it only for other archs.
        if (cluster->cluster_arch != tt::ARCH::BLACKHOLE) {
            for (auto &alloc : queue_info.alloc_info) {
                // check if the address is in the mmio range - chan, start, end
                translatable_range &=
                    (alloc.channel == tt::DRAM_HOST_MMIO_CHANNEL &&
                    alloc.address >= tt::DRAM_HOST_MMIO_RANGE_START &&
                    alloc.address + buf_size <= tt::DRAM_HOST_MMIO_RANGE_START + DRAM_HOST_MMIO_SIZE_BYTES);
                if (translatable_range) {
                    log_assert(
                        alloc.channel == tt::DRAM_HOST_MMIO_CHANNEL &&
                        (alloc.address + buf_size <= tt::DRAM_HOST_MMIO_RANGE_START + DRAM_HOST_MMIO_SIZE_BYTES), "Queue does not fit in MMIO range: {}", queue_info.name);
                }
            }
        }
    }

    if (translatable_range) {
        for (auto &addr_channel_pair : io_desc.bufq_start_addr_channel) {
            auto [offset, channel] = addr_channel_pair;

            if (queue_info.loc == QUEUE_LOCATION::HOST) {
                // check if the address is in the mmio range - chan, start, end
                for (auto &alloc : queue_info.alloc_info) {
                    log_assert(alloc.channel < host_mem::address_map::NUMBER_OF_CHANNELS, "Channel for Host Buffer must be less than {}", host_mem::address_map::NUMBER_OF_CHANNELS);
                    log_assert(
                        (alloc.address >= host_mem::address_map::ALLOCATABLE_QUEUE_REGION_START(alloc.channel)) &&
                        (alloc.address + buf_size <= host_mem::address_map::ALLOCATABLE_QUEUE_REGION_END(alloc.channel)),
                        "Host MMIO queue '{}' does not fit in MMIO range", queue_info.name);
                }
                if (queue_info.input == "HOST") {
                    // host producer queue is consumed by MMIO device
                    log_assert(
                        cluster->get_cluster_desc()->is_chip_mmio_capable(queue_info.target_device),
                        "All host queues must be consumed by MMIO-capable devices. Queue {}'s consumer is non-MMIO "
                        "device_id: {}",
                        io_desc.queue_name,
                        queue_info.target_device);
                } else {
                    // host consumer queue is produced by MMIO device
                    log_assert(
                        cluster->get_cluster_desc()->is_chip_mmio_capable(queue_info.src_device_id),
                        "All host queues must be produced by MMIO-capable devices. Queue {}'s producer is non-MMIO "
                        "device_id: {}",
                        io_desc.queue_name,
                        queue_info.src_device_id);
                }
                io_desc.bufq_mapping.push_back(cluster->host_dma_address(offset, queue_info.src_device_id, channel));
                log_assert(io_desc.bufq_mapping.back() != nullptr, "Must be able to translate host queue address for ch: {}. Possible hugepage issue, check for earlier warnings.", std::to_string(channel));
            } else if (queue_info.loc == QUEUE_LOCATION::DRAM) {
                if (cluster->type == TargetDevice::Versim || cluster->type == TargetDevice::Emulation) {
                    io_desc.bufq_mapping.push_back(reinterpret_cast<void*>(offset));
                } else {
                    if (cluster->get_cluster_desc()->is_chip_mmio_capable(queue_info.target_device)) {
                        tt_target_dram dram = {queue_info.target_device, channel, 0};
                        io_desc.bufq_mapping.push_back(cluster->channel_address(offset, dram));
                        log_assert(io_desc.bufq_mapping.back() != nullptr, "Must be able to translate dram queue address");
                    }
                }
            } else {
                log_error("Must be a valid queue location");
            }
        }
    }
}

// Helper functions for reading pointers and writing data + pointers when using SW tilize.
void read_from_queue(tt_cluster* cluster, vector<uint32_t>& vec, const tt_target_dram& queue_target, uint64_t address, const QUEUE_LOCATION queue_loc ,uint32_t size) {
    if (queue_loc == QUEUE_LOCATION::HOST) {
        cluster->read_sysmem_vec(vec, address, std::get<1>(queue_target), size, std::get<0>(queue_target));
    } else {
        cluster->read_dram_vec(vec, queue_target, address, size);
    }
}

void write_to_queue(tt_cluster* cluster, vector<uint32_t>& vec, const tt_target_dram& queue_target, uint64_t address, const QUEUE_LOCATION queue_loc) {
    if (queue_loc == QUEUE_LOCATION::HOST) {
        cluster->write_sysmem_vec(vec, address, std::get<1>(queue_target), std::get<0>(queue_target));
    } else {
        cluster->write_dram_vec(vec, queue_target, address);
    }
}

void write_to_queue(tt_cluster* cluster, const std::uint32_t* mem_ptr, const std::uint32_t size, const tt_target_dram& queue_target, uint64_t address, const QUEUE_LOCATION queue_loc) {
    if(queue_loc == QUEUE_LOCATION::HOST) {
        cluster->write_sysmem_vec(mem_ptr, size, address, std::get<1>(queue_target), std::get<0>(queue_target));
    }
    else {
        cluster->write_dram_vec(mem_ptr, size, queue_target, address);
    }
}

void update_queue_ptr(tt_cluster* cluster, uint32_t ptr_val, const tt_target_dram& queue_target, uint64_t address, const QUEUE_LOCATION queue_loc, bool wait_for_previous_write_to_flush = false) {
    vector<uint32_t> ptr_vec = {ptr_val};
    tt_driver_atomics::sfence();
    if (queue_loc == QUEUE_LOCATION::HOST) {
        cluster->write_sysmem_vec(ptr_vec, address, std::get<1>(queue_target), std::get<0>(queue_target));
    } else {
        if(wait_for_previous_write_to_flush and !(cluster->get_cluster_desc()->is_chip_mmio_capable(std::get<0>(queue_target)))) {
            // Ordering: Flush data write over ethernet before updating wptr/
            // When writing over PCIe, the sfence above flushes data from the host WC buffer and Posted TLB ordering
            // + writes to the same NOC endpoint ensure ordering.
            cluster->wait_for_non_mmio_flush();
        }
        cluster->write_dram_vec(ptr_vec, queue_target, address);
    }
    tt_driver_atomics::sfence();
}

void flush_wptr_update(tt_cluster* cluster, QUEUE_LOCATION queue_loc, chip_id_t target_device, std::uint32_t chan) {
    if(queue_loc == QUEUE_LOCATION::HOST) {
        tt_driver_atomics::mfence(); // Ensure that the wptr is flushed to host memory and that no reads/writes are ordred around this update
    }
    else {
        // UMD Memory barrier to flush wptr to Device DRAM
        std::unordered_set<uint32_t> sync_chan = {chan};
        cluster -> memory_barrier(MemBarType::host_device_dram, target_device, sync_chan);
    }
}
void wait_until_wptr_updated(tt_cluster* cluster, const tt_target_dram& dram, const uint32_t wptr_update, const uint32_t w_ptr_address, const QUEUE_LOCATION queue_loc) {
    std::vector<uint32_t> temp_wptr(1);
    read_from_queue(cluster, temp_wptr, dram, w_ptr_address, queue_loc, 4);
    // keep polling until the device and host versions of wptr match
    while(temp_wptr[0] !=wptr_update) {
        read_from_queue(cluster, temp_wptr, dram, w_ptr_address, queue_loc, 4);
    }
}
// Wait until all entries desired to be pop/peaked are ready in DRAM.
// KCM FIXME - Consider making greedy get() to read queue before it is fully populated, and see if all can be read before timeout (similiar to push).
// But does that even make sense given a pointer to memory is returned?
void wait_until_dram_output_queue_populated(std::chrono::high_resolution_clock::time_point &start_time, tt_cluster *cluster, const tt_queue_allocation_info &alloc, const tt_target_dram &dram_loc, tt_queue_ptr &dram_ptr, int pop_count, const int timeout_in_seconds){

    log_trace(tt::LogIO, "DRAM Queue not fully populated, waiting up to timeout_in_seconds: {}.", timeout_in_seconds);

    log_assert(timeout_in_seconds > 0, "Only expected to be called with timeout_in_seconds>0");
    bool queue_is_fully_populated   = false;
    int loop_idx                    = 0;
    auto timeout_in_ms              = 1000 * timeout_in_seconds;

    while (!queue_is_fully_populated){
        if (loop_idx++ == TIMEOUT_CHECK_FREQ){
            cluster->read_dram_vec(dram_ptr.ptrs, dram_loc, alloc.address, 8);
            queue_is_fully_populated = dram_ptr.occupancy() >= pop_count;
            auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
            if (elapsed_time_in_ms > std::chrono::milliseconds{timeout_in_ms}){
                throw tt::error_types::timeout_error("Exceeded timeout of " + std::to_string(timeout_in_seconds) + " seconds to get/pop " + std::to_string(pop_count) + " entries from device DRAM queue.");
            }
            loop_idx = 0;
        }
    }
    if (backend_profiler.is_enabled()) {
        log_trace(tt::LogIO, "DRAM Queue outputs seen after waiting {} ms.", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count());
    }
}

// Wait until all entries desired to be pop/peaked are ready in HOST sysmem.
void wait_until_host_output_queue_populated(const tt_dram_io_desc &desc, uint32_t *q_ptr, tt_queue_ptr &sysmem_ptr, int pop_count, const int timeout_in_seconds){

    log_trace(tt::LogIO, "Host Queue not fully populated, waiting up to timeout_in_seconds: {}.", timeout_in_seconds);

    log_assert(timeout_in_seconds > 0, "Only expected to be called with timeout_in_seconds>0");
    bool queue_is_fully_populated   = false;
    int loop_idx                    = 0;
    auto timeout_in_ms              = 1000 * timeout_in_seconds;

    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    while (!queue_is_fully_populated){
        if (loop_idx++ == TIMEOUT_CHECK_FREQ){
            sysmem_ptr.wr_ptr = *(q_ptr + 1); // Just check wrptr, rdptr not expected to be changing after it's read once.
            queue_is_fully_populated = sysmem_ptr.occupancy() >= pop_count;
            auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
            if (elapsed_time_in_ms > std::chrono::milliseconds{timeout_in_ms}){
                throw tt::error_types::timeout_error("Exceeded timeout of " + std::to_string(timeout_in_seconds) + " seconds to get/pop " + std::to_string(pop_count) + " entries from device HOST queue.");
            }
            loop_idx = 0;
        }
    }
    if (backend_profiler.is_enabled()) {
        log_trace(tt::LogIO, "Host Queue outputs seen after waiting {} ms.", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count());
    }
}

// Wait until all entries desired to be pushed have free space in target DRAM.
void wait_until_input_queue_freed(std::chrono::high_resolution_clock::time_point &start_time, tt_cluster *cluster, const tt_queue_allocation_info &alloc, const tt_target_dram &q_target, const QUEUE_LOCATION q_location , tt_queue_ptr &q_ptr, int push_count, const int timeout_in_seconds){

    log_assert(timeout_in_seconds > 0, "Only expected to be called with timeout_in_seconds>0");
    bool queue_is_fully_freed       = false;
    int loop_idx                    = 0;
    auto timeout_in_ms              = 1000 * timeout_in_seconds;

    while (!queue_is_fully_freed){
        if (loop_idx++ == TIMEOUT_CHECK_FREQ){
            read_from_queue(cluster, q_ptr.ptrs, q_target, alloc.address, q_location, 8);
            queue_is_fully_freed = q_ptr.free_space() >= push_count;
            auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
            if (elapsed_time_in_ms > std::chrono::milliseconds{timeout_in_ms}){
                throw tt::error_types::timeout_error("Exceeded timeout of " + std::to_string(timeout_in_seconds) + " seconds to push " + std::to_string(push_count) + " entries to device DRAM queue.");
            }
            loop_idx = 0;
        }
    }
}

// Get a user space pointer to an untilized ptr in a dma buffer
void *get_untilized_from_sysmem(const tt_dram_io_desc &desc, const tt_queue_info& queue_info, int pop_count, int ram_ptr, const int timeout_in_seconds) {
    log_assert(desc.bufq_start_addr_channel.size() == 1, "Expecting an untilized queue! Should only have one buffer queue");
    uint32_t *q_ptr = reinterpret_cast<uint32_t *>(desc.bufq_mapping.front());
    uint32_t num_entries = desc.bufq_num_slots;
    const uint32_t entry_size = get_entry_size_in_bytes(queue_info, false);
    tt_queue_ptr sysmem_ptr(num_entries);
    uint32_t rd_ptr_to_range = 0;

    if (q_ptr == nullptr) {
        return q_ptr;
    }

    if (desc.io_type == IO_TYPE::Queue) {

        // Read ptrs from queue
        sysmem_ptr.rd_ptr = *q_ptr;
        sysmem_ptr.wr_ptr = *(q_ptr + 1);
        bool queue_is_fully_populated = sysmem_ptr.occupancy() >= pop_count;

        if (timeout_in_seconds > 0 && !queue_is_fully_populated){
            wait_until_host_output_queue_populated(desc, q_ptr, sysmem_ptr, pop_count, timeout_in_seconds);
        }else{
            log_assert(queue_is_fully_populated, "Cannot get {} entries from queue!", pop_count);
        }

        rd_ptr_to_range = sysmem_ptr.get_rd_ptr();
    }
    else if (desc.io_type == IO_TYPE::RandomAccess) {
        // Assign random access ptr
        rd_ptr_to_range = ram_ptr;
        log_assert(ram_ptr < num_entries, "RAM access out of range!");
    }

    log_assert((rd_ptr_to_range + pop_count) <= num_entries, "Entries read must reside in contiguous memory!");
    uint32_t offset = (entry_size * rd_ptr_to_range + tt::io::io_queue_header_size_bytes) / sizeof(uint32_t); // div by 4 for uint32 ptr arithmetic
    return q_ptr + offset;
}

// increment the read pointer. invalidates any live untilized tensors
void pop_from_sysmem(const tt_dram_io_desc &desc, int pop_count, const int timeout_in_seconds) {
    uint32_t num_entries = desc.bufq_num_slots;
    tt_queue_ptr sysmem_ptr(num_entries);

    for (int i = 0; i < desc.bufq_start_addr_channel.size(); i++) {
        uint32_t *q_ptr = reinterpret_cast<uint32_t *>(desc.bufq_mapping.at(i));
        if (q_ptr == nullptr) {
            log_fatal("Could not pop from device -- no host buffer was allocated");
        }

        sysmem_ptr.rd_ptr = *q_ptr;
        sysmem_ptr.wr_ptr = *(q_ptr + 1);

        bool queue_is_fully_populated = sysmem_ptr.occupancy() >= pop_count;
        if (timeout_in_seconds > 0 && !queue_is_fully_populated){
            wait_until_host_output_queue_populated(desc, q_ptr, sysmem_ptr, pop_count, timeout_in_seconds);
        }else{
            log_assert(queue_is_fully_populated, "Cannot pop {} entries from queue!", pop_count);
        }

        // Update buffer rdptr
        sysmem_ptr.incr_rd(pop_count);
        *q_ptr = sysmem_ptr.rd_ptr;
    }
}

// increment the read pointer. invalidates any live tensors
void pop_from_dram(const tt_queue_info &queue_info, const int pop_count, tt_cluster *cluster, const int timeout_in_seconds) {

    log_assert(queue_info.loc == QUEUE_LOCATION::DRAM, "Expected queue in DRAM");
    uint32_t num_entries = queue_info.entries;

    // Single timeout clock running for all queues, so need to loop over them all with a single start time first.
    if (timeout_in_seconds > 0){
        std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
        for (auto &alloc : queue_info.alloc_info) {
            tt_target_dram dram = {queue_info.target_device, alloc.channel, 0};
            tt_queue_ptr dram_ptr(num_entries);
            // Read ptrs from queue
            cluster->read_dram_vec(dram_ptr.ptrs, dram, alloc.address, 8);
            bool queue_is_fully_populated = dram_ptr.occupancy() >= pop_count;
            if (!queue_is_fully_populated){
                wait_until_dram_output_queue_populated(start_time, cluster, alloc, dram, dram_ptr, pop_count, timeout_in_seconds);
            }
        }
    }

    for (auto &alloc : queue_info.alloc_info) {
        tt_target_dram dram = {queue_info.target_device, alloc.channel, 0};
        tt_queue_ptr dram_ptr(num_entries);

        // Read ptrs from queue
        cluster->read_dram_vec(dram_ptr.ptrs, dram, alloc.address, 8);

        log_assert(dram_ptr.occupancy() >= pop_count, "Cannot pop {} entries from queue!", pop_count);

        // Buffer size check and update rdptr.
        for (int i=0; i<pop_count; i++) {
            log_assert(!dram_ptr.empty(), "Cannot pop from empty queue!");
            dram_ptr.incr_rd();
        }
        update_queue_ptr(cluster, dram_ptr.rd_ptr, dram, alloc.address, queue_info.loc);
    }
}

std::vector<uint32_t> pop_untilized_vector_from_sysmem(const tt_dram_io_desc &desc, const tt_queue_info& queue_info, bool update_rdptr, int pop_count, int ram_ptr, const int timeout_in_seconds) {
    uint32_t *data = static_cast<uint32_t *>(get_untilized_from_sysmem(desc, queue_info, pop_count, ram_ptr, timeout_in_seconds));
    uint32_t data_len = pop_count * desc.bufq_entry_size / sizeof(uint32_t);
    vector<uint32_t> rv(data, data + data_len);
    if (update_rdptr) pop_from_sysmem(desc, pop_count, timeout_in_seconds);
    return rv;
}

std::vector<uint32_t> pop_untilized_vector_from_dram(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, bool update_rdptr, int pop_count, int ram_ptr, const int timeout_in_seconds) {
    log_assert(queue_info.alloc_info.size() == 1, "Must have a single untilized buffer per queue");
    auto &alloc_info     = queue_info.alloc_info[0];
    uint32_t num_entries = desc.bufq_num_slots;
    const uint32_t entry_size = get_entry_size_in_bytes(queue_info, false);
    uint32_t total_size  = entry_size * pop_count;

    tt_target_dram dram_loc = {queue_info.target_device, alloc_info.channel, 0};
    tt_queue_ptr dram_ptr(num_entries);
    uint32_t dram_rd_ptr = 0;

    if (desc.io_type == IO_TYPE::Queue) {
        // Read ptrs from queue
        cluster->read_dram_vec(dram_ptr.ptrs, dram_loc, alloc_info.address, 8);
        bool queue_is_fully_populated = dram_ptr.occupancy() >= pop_count;

        if (!queue_is_fully_populated && timeout_in_seconds > 0){
            std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
            wait_until_dram_output_queue_populated(start_time, cluster, alloc_info, dram_loc, dram_ptr, pop_count, timeout_in_seconds);
        }else{
            log_assert(queue_is_fully_populated, "Cannot pop {} entries from queue!", pop_count);
        }

        dram_rd_ptr = dram_ptr.get_rd_ptr();
    }
    else if (desc.io_type == IO_TYPE::RandomAccess) {
        // Assign random access ptr
        dram_rd_ptr = ram_ptr;
        log_assert(ram_ptr < num_entries, "RAM access out of range!");
        log_assert(!update_rdptr, "RAM must never update ptrs!");
    }

    // Read buffer entry
    std::vector<std::uint32_t> rv(total_size / sizeof(uint32_t));
    uint32_t dram_rd_addr = alloc_info.address + tt::io::io_queue_header_size_bytes + (dram_rd_ptr * entry_size);
    cluster->read_dram_vec(rv, dram_loc, dram_rd_addr, total_size);
    log_trace(tt::LogIO, "Pop untilized from DRAM, entry_size={}, rd_ptr={}, rd_addr={}, {}", entry_size, dram_rd_ptr, dram_rd_addr, queue_info);

    // Update buffer rdptr
    if (update_rdptr) {
        for (int i=0; i<pop_count; i++) {
            log_assert(!dram_ptr.empty(), "Cannot pop from empty queue!");
            dram_ptr.incr_rd();
        }
        update_queue_ptr(cluster, dram_ptr.rd_ptr, dram_loc, alloc_info.address, queue_info.loc);
    }
    return rv;
}

std::vector<uint32_t> pop_untilized_vector(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, const int timeout_in_seconds) {
    bool update_rdptr = true;
    int32_t ram_ptr = -1;

    if (queue_info.type == IO_TYPE::RandomAccess) {
       update_rdptr = false;
       ram_ptr = 0;
    }
    if (queue_info.loc == QUEUE_LOCATION::HOST) {
        return pop_untilized_vector_from_sysmem(desc, queue_info, update_rdptr, 1/*pop_count*/, ram_ptr, timeout_in_seconds);
    } else {
        return pop_untilized_vector_from_dram(desc, queue_info, cluster, update_rdptr, 1/*pop_count*/, ram_ptr, timeout_in_seconds);
    }
}

// Peek untilized data from DRAM without touching the ptrs
void *get_untilized_from_dram(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, int pop_count, int ram_ptr, const int timeout_in_seconds) {
    // pop data from device
    vector<uint32_t> rv = pop_untilized_vector_from_dram(desc, queue_info, cluster, false/*update_rdptr*/, pop_count, ram_ptr, timeout_in_seconds);

    // calculate data chunk size
    int num_bytes_tensor = pop_count * get_tensor_size_in_bytes(queue_info, false);
    log_assert(num_bytes_tensor == rv.size()*4, "Need rv sizes to {} bytes to accept untilized tensor", num_bytes_tensor);
    // allocate memory to store the result
    shared_ptr<vector<uint32_t>> mem = get_workload(desc.netlist_path)->allocate_untilized_memory(desc.queue_name, pop_count);
    std::memcpy(mem->data(), rv.data(), num_bytes_tensor);
    return mem->data();
}

// Peek tilized data from DRAM without touching the ptrs
void *get_tilized_from_dram(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, int ram_ptr, int pop_count, const int timeout_in_seconds) {
    // pop data from device
    tt_runtime_workload &workload = *get_workload(desc.netlist_path);
    Dim ublock_scan = workload.get_ublock_scan(queue_info.name);
    tt_tensor result = pop_queue_tilized_output(queue_info, cluster, false/*update_rdptr*/, pop_count, ublock_scan, timeout_in_seconds, ram_ptr); // Unpack is done in this function now
    //result.unpack_data();
    // t-slices stacking operations
    if (desc.hstack_factor > 1 or desc.vstack_factor > 1) {
        if (desc.stack_row_major &&  desc.hstack_factor > 1) {
            // Nop if row major stacking order and hstack factor == 1, since data is already vstacked in row major order even without vstacking
            result = result.reshape_z_dim_into_c_dim(desc.hstack_factor).reshape_z_dim_into_r_dim(desc.vstack_factor);
        } else if(!desc.stack_row_major) {
            result = result.reshape_z_dim_into_r_dim(desc.vstack_factor).reshape_z_dim_into_c_dim(desc.hstack_factor);
        }
    }
    // calculate data chunk size
    int num_elements_tensor = pop_count * get_tensor_size_in_elements(queue_info);
    // allocate memory to store the result
    shared_ptr<vector<uint32_t>> mem = get_workload(desc.netlist_path)->allocate_untilized_memory(desc.queue_name, pop_count);
    if (result.get_data_format() == DataFormat::Float32 || result.get_data_format() == DataFormat::Tf32 || result.get_data_format() == DataFormat::RawUInt32) {
        vector<float> float_vector;
        result.untilize_to_flat_tensor_data(true, false, desc.layout == IO_LAYOUT::Flat, float_vector);
        log_assert(num_elements_tensor == float_vector.size(), "Size mismatch: Tensor cannot be copied to float_vector");
        std::memcpy(mem->data(), float_vector.data(), float_vector.size()*4);
    } else if (result.get_data_format() == DataFormat::Int8) {
        vector<int8_t> byte_vector;
        result.untilize_to_flat_tensor_data_byte(true, false, byte_vector);
        log_assert(num_elements_tensor == byte_vector.size(), "Size mismatch: Tensor cannot be copied to byte_vector");
        std::memcpy(mem->data(), byte_vector.data(), byte_vector.size());
    } else if (result.get_data_format() == DataFormat::UInt8) {
        vector<uint8_t> byte_vector;
        result.untilize_to_flat_tensor_data_unsigned_byte(true, false, byte_vector);
        TT_ASSERT(num_elements_tensor == byte_vector.size());
        std::memcpy(mem->data(), byte_vector.data(), byte_vector.size());
    } else if (result.get_data_format() == DataFormat::Int32) {
        vector<int32_t> dword_vector;
        result.untilize_to_flat_tensor_data_dword(true, false, dword_vector);
        log_assert(num_elements_tensor == dword_vector.size(), "Size mismatch: Tensor cannot be copied to dword_vector");
        std::memcpy(mem->data(), dword_vector.data(), dword_vector.size() * 4);
    } else {
        vector<uint16_t> half_float_vector;
        result.untilize_to_flat_tensor_data_half(true, false, half_float_vector);
        log_assert(num_elements_tensor == half_float_vector.size(), "Size mismatch: Tensor cannot be copied to half_float_vector");
        std::memcpy(mem->data(), half_float_vector.data(), half_float_vector.size()*2);
    }
    return mem->data();
}

void *get_tilized_from_sysmem(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, int ram_ptr, int pop_count, const int timeout_in_seconds) {
    auto get_start = std::chrono::high_resolution_clock::now();
    // calculate data chunk size
    int num_elements_tensor = pop_count * get_tensor_size_in_elements(queue_info);
    // allocate memory to store the result
    shared_ptr<vector<uint32_t>> mem = get_workload(desc.netlist_path)->allocate_untilized_memory(desc.queue_name, pop_count);

    auto get_end0 = std::chrono::high_resolution_clock::now();

    // pop data from device
    tt_runtime_workload &workload = *get_workload(desc.netlist_path);
    Dim ublock_scan = workload.get_ublock_scan(queue_info.name);
    tt_tensor result = pop_queue_tilized_output_sysmem(desc, queue_info, cluster, false/*update_rdptr*/, pop_count, ublock_scan, timeout_in_seconds, ram_ptr); // Unpack is done in this function now
    auto get_end1 = std::chrono::high_resolution_clock::now();

    // t-slices stacking operations
    if (desc.hstack_factor > 1 or desc.vstack_factor > 1) {
        if (desc.stack_row_major && desc.hstack_factor > 1) {
            // Nop if row major stacking order and hstack factor == 1, since data is already in row major order even without vstacking
            result = result.reshape_z_dim_into_c_dim(desc.hstack_factor).reshape_z_dim_into_r_dim(desc.vstack_factor);
        } else if(!desc.stack_row_major){
            result = result.reshape_z_dim_into_r_dim(desc.vstack_factor).reshape_z_dim_into_c_dim(desc.hstack_factor);
        }
    }

    auto get_end2 = std::chrono::high_resolution_clock::now();

    if (result.get_data_format() == DataFormat::Float32 || result.get_data_format() == DataFormat::Tf32 || result.get_data_format() == DataFormat::RawUInt32) {
        vector<float> float_vector;
        result.untilize_to_flat_tensor_data(true, false, desc.layout == IO_LAYOUT::Flat, float_vector);
        log_assert(num_elements_tensor == float_vector.size(), "Size mismatch: Tensor cannot be copied to float_vector");
        std::memcpy(mem->data(), float_vector.data(), float_vector.size()*4);
    } else if (result.get_data_format() == DataFormat::Int8) {
        vector<int8_t> byte_vector;
        result.untilize_to_flat_tensor_data_byte(true, false, byte_vector);
        log_assert(num_elements_tensor == byte_vector.size(), "Size mismatch: Tensor cannot be copied to byte_vector");
        std::memcpy(mem->data(), byte_vector.data(), byte_vector.size());
    } else if (result.get_data_format() == DataFormat::UInt8) {
        vector<uint8_t> byte_vector;
        result.untilize_to_flat_tensor_data_unsigned_byte(true, false, byte_vector);
        TT_ASSERT(num_elements_tensor == byte_vector.size());
        std::memcpy(mem->data(), byte_vector.data(), byte_vector.size());
    } else if (result.get_data_format() == DataFormat::Int32) {
        vector<int32_t> dword_vector;
        result.untilize_to_flat_tensor_data_dword(true, false, dword_vector);
        log_assert(num_elements_tensor == dword_vector.size(), "Size mismatch: Tensor cannot be copied to dword_vector");
        std::memcpy(mem->data(), dword_vector.data(), dword_vector.size() * 4);
    }
    else {
        vector<uint16_t> half_float_vector;
        result.untilize_to_flat_tensor_data_half(true, false, half_float_vector);
        log_assert(num_elements_tensor == half_float_vector.size(), "Size mismatch: Tensor cannot be copied to half_float_vector");
        std::memcpy(mem->data(), half_float_vector.data(), half_float_vector.size()*2);
    }
    auto get_end3 = std::chrono::high_resolution_clock::now();

    auto get_dur0 = std::chrono::duration_cast<std::chrono::microseconds>(get_end0 - get_start).count();
    auto get_dur1 = std::chrono::duration_cast<std::chrono::microseconds>(get_end1 - get_end0).count();
    auto get_dur2 = std::chrono::duration_cast<std::chrono::microseconds>(get_end2 - get_end1).count();
    auto get_dur3 = std::chrono::duration_cast<std::chrono::microseconds>(get_end3 - get_end2).count();

    log_trace(tt::LogIO, "get_tilized_from_sysmem: allocate={}, pop={}, stack={}, untilize={}", get_dur0, get_dur1, get_dur2, get_dur3);
    return mem->data();
}

tt_tensor default_debug_tensor(tt_queue_info &queue_info, int entry_idx) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;
    tt_tensor tensor(md);
    tensor = 1.0f;
    return tensor;
}

// Construct tt_tensor from raw vector memory
tt_tensor reconstruct_tensor_from_untilized(std::vector<uint32_t> &mem_vector, const tt_dram_io_desc &desc, bool batched, bool use_twos_complement, DataFormat from_format, uint32_t w_dim) {
    log_assert(batched ? (w_dim > 0) : true, "w_dim must be set to a non-zero value when calling {} with batched = true", __FUNCTION__);
    tt_runtime_workload &workload = *get_workload(desc.netlist_path);
    tt_queue_info &q_info = workload.queues.at(desc.queue_name).my_queue_info;
    tt_tensor_metadata md;
    md.shape.rt = desc.mblock_m * desc.ublock_rt * desc.bufq_grid_dim_r * desc.vstack_factor;
    md.shape.ct = desc.mblock_n * desc.ublock_ct * desc.bufq_grid_dim_c * desc.hstack_factor;
    md.shape.z  = desc.t / (desc.hstack_factor * desc.vstack_factor);
    md.shape.w  = batched ? w_dim : 1;
    md.shape.tile_height = get_tile_dim_y(q_info);
    md.shape.tile_width = get_tile_dim_x(q_info);
    md.data_format = desc.bufq_target_format;
    md.is_tilized = false;
    tt_tensor tensor(md);

    log_assert(desc.t % desc.hstack_factor == 0, "hstack factor must divide t evenly!");
    log_assert(desc.t % desc.vstack_factor == 0, "vstack factor must divide t evenly!");

    // Allow pytorch API path to provide host_format->queue_format conversion here.
    if(desc.bufq_target_format == tt::DataFormat::Int8 && use_twos_complement) {
        // This data path is used for testing. Perform 2's complement conversion here, since get_output_from_device
        // and associated conversion will not be invoked.
        tensor.flat_tensor_data = tt::io::process_int8_untilized_with_twos_complement_conversion(mem_vector);
    }
    else if(desc.bufq_target_format == tt::DataFormat::Int32 && use_twos_complement) {
        // This data path is used for testing. Perform 2's complement conversion here, since get_output_from_device
        // and associated conversion will not be invoked.
        tensor.flat_tensor_data = tt::io::process_int32_untilized_with_twos_complement_conversion(mem_vector);
    }
    else {
        auto mem_vector_format = from_format != DataFormat::Invalid ? from_format : desc.bufq_target_format;
        tensor.flat_tensor_data = tt::io::process_raw_untilized(&mem_vector[0], mem_vector.size(), mem_vector_format);
    }
    // Untilized output is row major by spec -- look here if that changes!!
    bool c_is_contiguous = true;

    tensor.tilize_inplace(c_is_contiguous, false, desc.layout == IO_LAYOUT::Flat);
    return tensor;
}

// Construct tt_tensor from raw vector memory pointer
tt_tensor reconstruct_tensor_from_untilized(const tt_dram_io_desc &desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, bool batched, bool use_twos_complement, uint32_t w_dim_override) {
    const uint32_t *data = static_cast<const uint32_t *>(py_tensor_desc.ptr);
    int volume = 1;
    for (int dim = 0; dim < py_tensor_desc.shape.size() ; ++dim) {
        volume *= py_tensor_desc.shape[dim];
    }
    uint32_t data_len = (volume * tt::size::sizeof_format(py_tensor_desc.format)) / sizeof(uint32_t);
    vector<uint32_t> rv(data, data + data_len);
    uint32_t w_dim = py_tensor_desc.shape[0];
    if(w_dim_override > 0) {
        w_dim = w_dim_override;
    }
    return tt::io::reconstruct_tensor_from_untilized(rv, desc, batched, use_twos_complement, py_tensor_desc.format, w_dim);
}

template<Tilizer tilizer_backend>
void push_host_inputs_with_hw_tilize(tt_runtime_workload &workload, tt_dram_io_desc &dram_io_desc, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes, int ram_ptr, tt_cluster *cluster) {
    // Host fast tilize and data push
    std::string object_cache_tag = dram_io_desc.queue_name;


    const int timeout_in_seconds = 0; // Currently used for pybuda API only.

    for (int i=0; i<num_pushes; i++) {
        // Provide host side storage for data since only data ptrs are provided to tilizer
        aligned_vector<float> host_stored_float_vectors = aligned_vector<float>{};
        aligned_vector<uint16_t> host_stored_half_vectors = aligned_vector<uint16_t>{};
        aligned_vector<int8_t> host_stored_byte_vectors = aligned_vector<int8_t>{};
        aligned_vector<uint8_t> host_stored_unsigned_byte_vectors = aligned_vector<uint8_t>{};
        aligned_vector<int32_t> host_stored_dword_vectors = aligned_vector<int32_t>{};
        tt::tt_PytorchTensorDesc tensor_desc;

        tt_queue_info &queue_info = workload.queues.at(dram_io_desc.queue_name).my_queue_info;
        tt_tensor input = get_tensor_callback(queue_info, i);
        log_assert(queue_info.input == "HOST" or queue_info.type == IO_TYPE::RandomAccess, "Only host or ram input can be pushed to device");
        if (i >= queue_info.entries) {
            log_warning(tt::LogIO, "IO {} of entires={} cannot fit push #{}, skipping...", queue_info.name, queue_info.entries, i);
            continue; // handle param input only once
        }
        
        for (auto &alloc : queue_info.alloc_info) {
            // We can push any address on Blackhole using Fast MMIO push, no need to check address range.
            if (cluster->cluster_arch != tt::ARCH::BLACKHOLE) {
               log_assert(tilizer_backend == Tilizer::FastTilizeDevicePush ||  (alloc.channel == tt::DRAM_HOST_MMIO_CHANNEL && alloc.address >= tt::DRAM_HOST_MMIO_RANGE_START) || queue_info.loc == QUEUE_LOCATION::HOST, 
                        "HW tilize path only supports DRAM channel 0 above tt::DRAM_HOST_MMIO_RANGE_START or Input Queues on Host"); 
            }
        }

        log_debug(tt::LogIO, "HW tilize input queue {} push #{}, host data_format={}, device data_format={}", dram_io_desc.queue_name, i, input.get_data_format(), queue_info.data_format);
        if (debug) input.dump();
        if (input.get_data_format() == DataFormat::Float32 or input.get_data_format() == DataFormat::RawUInt32) {
            tensor_desc = get_pytorch_tensor_desc<float>(input, host_stored_float_vectors, dram_io_desc.layout == IO_LAYOUT::Flat);
        } else if (input.get_data_format() == DataFormat::Int8) {
            tensor_desc = get_pytorch_tensor_desc<int8_t>(input, host_stored_byte_vectors, dram_io_desc.layout == IO_LAYOUT::Flat);
        } else if (input.get_data_format() == DataFormat::UInt8) {
            tensor_desc = get_pytorch_tensor_desc<uint8_t>(input, host_stored_unsigned_byte_vectors, dram_io_desc.layout == IO_LAYOUT::Flat);
        } else if (input.get_data_format() == DataFormat::Int32) {
            tensor_desc = get_pytorch_tensor_desc<int32_t>(input, host_stored_dword_vectors, dram_io_desc.layout == IO_LAYOUT::Flat);
        } else {
            tensor_desc = get_pytorch_tensor_desc<uint16_t>(input, host_stored_half_vectors, dram_io_desc.layout == IO_LAYOUT::Flat);
        }
        
        perf::ScopedEventProfiler profile(perf::HostEventType::HW_TILIZE);
        DeviceTilizer<tilizer_backend> *hw_tilizer = get_tilizer<tilizer_backend>(object_cache_tag, {dram_io_desc}, cluster, tensor_desc, dram_io_desc.s_descriptor, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
        hw_tilizer->tilize_and_push_to_device({tensor_desc}, timeout_in_seconds, ram_ptr);
    }
}

void push_host_inputs_with_sw_tilize(tt_runtime_workload &workload, tt_dram_io_desc &dram_io_desc, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes, int ram_ptr, tt_cluster *cluster) {
    // Host sw pre-tilized data push
    log_assert(cluster != nullptr, "Expected cluster to be populated");
    const int timeout_in_seconds = 0; // Only supported for pybuda API path, not this (backend only).
    Dim ublock_scan = workload.get_ublock_scan(dram_io_desc.queue_name);
    for (int i=0; i<num_pushes; i++) {
        tt_queue_info &queue_info = workload.queues.at(dram_io_desc.queue_name).my_queue_info;
        tt_tensor input = get_tensor_callback(queue_info, i);
        log_assert(queue_info.input == "HOST" or queue_info.type == IO_TYPE::RandomAccess, "Only host or ram input can be pushed to device");
        if (i >= queue_info.entries) {
            log_warning(tt::LogIO, "IO {} of entires={} cannot fit push #{}, skipping...", queue_info.name, queue_info.entries, i);
            continue; // handle param input only once
        }
        log_debug(tt::LogIO, "SW tilize input queue {} push #{}, host data_format={}, device data_format={}", dram_io_desc.queue_name, i, input.get_data_format(), queue_info.data_format);
        if (debug) input.dump();
        push_queue_tilized_input(queue_info, input, cluster, timeout_in_seconds, ram_ptr, ublock_scan);
    }
}

void push_host_inputs(std::vector<tt_dram_io_desc> &dram_io_desc, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes, bool sw_tilize){
    const int default_ram_ptr = 0;
    for (tt_dram_io_desc &io : dram_io_desc) {
        push_host_input(io, get_tensor_callback, num_pushes, default_ram_ptr, sw_tilize);
    }
}

// Check each queue, and automatically pick HW-Tilize when conditions are met otherwise fall back to SW-Tilize. Force flags apply to all queues.
void push_host_input(tt_dram_io_desc &io, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes, int ram_ptr, bool sw_tilize) {
    tt_runtime_workload &workload = *get_workload(io.netlist_path);
    tt_cluster *cluster           = get_cluster(io.netlist_path, io.backend_type);
    tt_queue_info &queue_info     = workload.queues.at(io.queue_name).my_queue_info;
    tt_tensor first_input         = get_tensor_callback(queue_info, 0);
    DataFormat host_data_format   = first_input.get_data_format();
    DataFormat device_data_format = queue_info.data_format;

    int calc_num_pushes = (num_pushes > 0) ? num_pushes : queue_info.entries;

    Tilizer tilizer_backend = get_tilizer_based_on_io_config(io, host_data_format, device_data_format, cluster -> type);
    if (force_sw_tilize(sw_tilize) or tilizer_backend == Tilizer::SlowTilize){
        log_debug(tt::LogIO, "Using Slow SW Tilize for queue: {}", io.queue_name);
        push_host_inputs_with_sw_tilize(workload, io, get_tensor_callback, calc_num_pushes, ram_ptr, cluster);
    }
    else{
        if (tilizer_backend == Tilizer::FastTilizeDevicePush) {
            log_debug(tt::LogIO, "Using HW Tilize with writes through the TLB for queue: {}", io.queue_name);
            push_host_inputs_with_hw_tilize<Tilizer::FastTilizeDevicePush>(workload, io, get_tensor_callback, calc_num_pushes, ram_ptr, cluster);
        }
        else {
            log_debug(tt::LogIO, "Using HW Tilize with MMIO writes for queue: {}", io.queue_name);
            push_host_inputs_with_hw_tilize<Tilizer::FastTilizeMMIOPush>(workload, io, get_tensor_callback, calc_num_pushes, ram_ptr, cluster);
        }
    }

}

void set_ublock_data(std::vector<uint32_t>& host_buf, tt_tensor& input_tensor, const tt_queue_info &queue_info, const int& tile_size, uint32_t& offset, int zi, int wi, int gr, int gc, int ubr, int ubc) {
    for (int tr=0; tr<queue_info.dim.ublock_rt; ++tr) {
        for (int tc=0; tc<queue_info.dim.ublock_ct; ++tc) {
            int rt_index = gr*queue_info.dim.ublock_rt*queue_info.dim.mblock_m + ubr*queue_info.dim.ublock_rt + tr;
            int ct_index = gc*queue_info.dim.ublock_ct*queue_info.dim.mblock_n + ubc*queue_info.dim.ublock_ct + tc;
            tt_tile *tile = input_tensor.get_tile_ptr(rt_index, ct_index, zi, wi, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
            std::memcpy(host_buf.data() + offset, tile->packed_data.data(), tile_size);
            offset += tile_size / sizeof(uint32_t);
        }
    }
}
// WARNING: Slow tile by tile method, intended for testing purposes only
// production should use fast hw tilize method with batched push via DeviceTilizer
void push_queue_tilized_input(const tt_queue_info &queue_info, tt_tensor &input_tensor, tt_cluster *cluster, const int timeout_in_seconds, int ram_ptr, Dim ublock_scan) {
    perf::ScopedEventProfiler profile(perf::HostEventType::SW_TILIZE);
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();

    std::string queue_name = queue_info.name;
    // If tensor and queue data-formats are different, change the tensor data-format and re-pack the tiles
    bool same_formats = queue_info.data_format == input_tensor.get_data_format();
    if (!same_formats) {
        if (input_tensor.packed_data_present()) {
            input_tensor.clear_packed_data();
        }
        input_tensor.set_data_format(queue_info.data_format);
    }
    if (!input_tensor.packed_data_present()) input_tensor.pack_data(get_tile_dim_y(queue_info), get_tile_dim_x(queue_info)); // skip pack if possible
    log_assert(input_tensor.getw() == 1 or input_tensor.getw() == queue_info.input_count or input_tensor.getw() == queue_info.entries, "Incorrect w-dim when pushing tilized input.");
    log_assert(input_tensor.getz() == queue_info.dim.t, "Tensor z-dim does not match queue z-dim");
    log_assert(input_tensor.getrt() == queue_info.grid_size.r * queue_info.dim.ublock_rt * queue_info.dim.mblock_m, "Tensor r-dim does not match queue r-dim");
    log_assert(input_tensor.getct() == queue_info.grid_size.c * queue_info.dim.ublock_ct * queue_info.dim.mblock_n, "Tensor c-dim does not match queue c-dim");
    log_assert(queue_info.alloc_info.size() == queue_info.grid_size.r * queue_info.grid_size.c, "Num buffers in queue don't match grid size");
 
    const bool include_tile_header_size = queue_info.layout != IO_LAYOUT::Flat;
    const uint32_t entry_size = get_entry_size_in_bytes(queue_info, include_tile_header_size);
    const uint32_t tile_size = tt::size::get_tile_size_in_bytes(queue_info.data_format, include_tile_header_size, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    std::vector<std::uint32_t> rv(entry_size / sizeof(uint32_t));

    tt_target_dram q_target = {queue_info.target_device, -1/*chan*/, 0/*subchan*/};
    tt_queue_ptr q_ptr(queue_info.entries);
    uint32_t q_wr_ptr = 0;
    uint32_t push_count = input_tensor.getw();

    int alloc_index = 0;
    for (auto &alloc : queue_info.alloc_info) {
        log_assert(queue_info.loc == QUEUE_LOCATION::DRAM || queue_info.loc == QUEUE_LOCATION::HOST, "Invalid queue location");
        std::get<1>(q_target) = alloc.channel;

        if (queue_info.type == IO_TYPE::Queue) {
            // Read ptrs from queue
            read_from_queue(cluster, q_ptr.ptrs, q_target, alloc.address, queue_info.loc, 8);
            if (timeout_in_seconds > 0 && q_ptr.free_space() < push_count) {
                wait_until_input_queue_freed(start_time, cluster, alloc, q_target, queue_info.loc, q_ptr, push_count, timeout_in_seconds);
            }
        } else if (queue_info.type == IO_TYPE::RandomAccess) {
            // Assign random access ptr
            q_ptr.wr_ptr = ram_ptr;
            q_ptr.rd_ptr = -1;
            log_assert(ram_ptr < queue_info.entries, "RAM access out of range!");
        }

        int gr = alloc_index / queue_info.grid_size.c;
        int gc = alloc_index % queue_info.grid_size.c;

        for(int wi=0; wi<input_tensor.getw(); ++wi) {
            // log_debug(tt::LogIO, "Pushing to queue = {}, rd_ptr = {}, wr_ptr = {}", queue_name, q_ptr.rd_ptr, q_ptr.wr_ptr);
            log_assert(!q_ptr.full(), "Cannot push to a full queue on device! Queue = {}, rd_ptr = {}, wr_ptr = {}", queue_name, q_ptr.rd_ptr, q_ptr.wr_ptr);
            q_wr_ptr = q_ptr.incr_get_wr();

            uint32_t offset = 0;
            for(int zi=0; zi<input_tensor.getz(); ++zi) {
                if(ublock_scan == Dim::R) {
                    for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                        for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                            set_ublock_data(rv, input_tensor, queue_info, tile_size, offset, zi, wi, gr, gc, ubr, ubc);
                        }
                    }
                } else if (ublock_scan == Dim::C) {
                    for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                        for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                            set_ublock_data(rv, input_tensor, queue_info, tile_size, offset, zi, wi, gr, gc, ubr, ubc);
                        }
                    }
                } else{
                    log_fatal("Invalid ublock scan order!");
                }
            }
            uint64_t dram_wr_addr = alloc.address + tt::io::io_queue_header_size_bytes + (q_wr_ptr * entry_size);
            write_to_queue(cluster, rv, q_target, dram_wr_addr, queue_info.loc);
            
            if (queue_info.type == IO_TYPE::Queue) {
                update_queue_ptr(cluster, q_ptr.wr_ptr, q_target, alloc.address + 4, queue_info.loc, true);
            }
        }
        // Before the next push to this buffer in the queue (which requires reading the wptr from device), ensure that
        // wptr update is flushed to dram. This is because of a RAW hazard when using PCIe in posted mode.
        if(queue_info.type == IO_TYPE::Queue) {
            flush_wptr_update(cluster, queue_info.loc, queue_info.target_device, alloc.channel);
        }
        alloc_index++;
    }

}

void get_ublock_data(
    const tt_queue_info &queue_info,
    const tt_tensor &tensor,
    const std::vector<std::uint32_t> &rv,
    const int global_offset,
    const int buf_index,
    uint32_t &buf_offset,
    const int wi,
    const int zi,
    const int ubr,
    const int ubc)
{
    int gr = buf_index / queue_info.grid_size.c;
    int gc = buf_index % queue_info.grid_size.c;
    for (int tr=0; tr<queue_info.dim.ublock_rt; ++tr) {
        for (int tc=0; tc<queue_info.dim.ublock_ct; ++tc) {
            int rt_index = gr*queue_info.dim.ublock_rt*queue_info.dim.mblock_m + ubr*queue_info.dim.ublock_rt + tr;
            int ct_index = gc*queue_info.dim.ublock_ct*queue_info.dim.mblock_n + ubc*queue_info.dim.ublock_ct + tc;
            tt_tile *tile = tensor.get_tile_ptr(rt_index, ct_index, zi, wi, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
            log_assert(!tile->packed_data_present(), "Did not expect packed data to be be populated.");
            uint32_t tile_size = tile->size_bytes(true);
            tile->packed_data.resize(tile_size);
            std::memcpy(tile->packed_data.data(), rv.data() + global_offset + buf_offset, tile_size);
            tile->packed_data_to_tile();
            buf_offset += tile_size / sizeof(uint32_t);
        }
    }
}

tt_tensor pop_queue_tilized_output(const tt_queue_info &queue_info, tt_cluster *cluster, bool update_rdptr, int pop_count, Dim ublock_scan, const int timeout_in_seconds, int ram_ptr) {

    std::string queue_name = queue_info.name;

    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = pop_count;
    md.shape.tile_height = get_tile_dim_y(queue_info);
    md.shape.tile_width = get_tile_dim_x(queue_info);
    md.is_tilized = true;
    md.data_format = queue_info.data_format;
    tt_tensor tensor(md);
    tensor.reserve_tile_tensor();

    const uint32_t entry_size = get_entry_size_in_bytes(queue_info, true);
    const uint32_t batch_size = entry_size * tensor.getw();

    std::vector<std::uint32_t> rv(batch_size); // rv stores a batch
    log_assert(queue_info.alloc_info.size() == queue_info.grid_size.r * queue_info.grid_size.c, "Queue grid size and number of bufs mismatch.");

    tt_target_dram dram_loc = {queue_info.target_device, -1/*chan*/, 0/*subchan*/};
    tt_queue_ptr dram_ptr(queue_info.entries);
    uint32_t dram_rd_ptr = 0;

    // Single timeout clock running for all queues, so need to loop over them all with a single start time first.
    if (timeout_in_seconds > 0 && queue_info.type == IO_TYPE::Queue){
        std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
        for (auto &alloc : queue_info.alloc_info) {
            std::get<1>(dram_loc) = alloc.channel;
            // Read ptrs from queue
            cluster->read_dram_vec(dram_ptr.ptrs, dram_loc, alloc.address, 8);
            bool queue_is_fully_populated = dram_ptr.occupancy() >= pop_count;
            if (!queue_is_fully_populated){
                wait_until_dram_output_queue_populated(start_time, cluster, alloc, dram_loc, dram_ptr, pop_count, timeout_in_seconds);
            }
        }
    }

    int alloc_index = 0;
    for (auto &alloc : queue_info.alloc_info) {
        log_assert(queue_info.loc == QUEUE_LOCATION::DRAM, "Expected queue to be in DRAM");
        std::get<1>(dram_loc) = alloc.channel;

        if (queue_info.type == IO_TYPE::Queue) {
            // Read ptrs from queue
            cluster->read_dram_vec(dram_ptr.ptrs, dram_loc, alloc.address, 8);
        } else if (queue_info.type == IO_TYPE::RandomAccess) {
            dram_ptr.rd_ptr = ram_ptr;
            dram_ptr.wr_ptr = -1;
            log_assert(ram_ptr < queue_info.entries, "RAM access out of range!");
            log_assert(!update_rdptr, "RAM must never update ptrs!");
        }

        int gr = alloc_index / queue_info.grid_size.c;
        int gc = alloc_index % queue_info.grid_size.c;

        dram_rd_ptr = dram_ptr.get_rd_ptr();
        uint64_t dram_rd_addr = alloc.address + tt::io::io_queue_header_size_bytes + (dram_rd_ptr * entry_size);

        log_assert(!(dram_ptr.empty() or dram_ptr.empty_during_batched_read(tensor.getw())), "Cannot pop {} slots from queue on device! Queue = {}, rd_ptr = {}, wr_ptr = {}", tensor.getw(), queue_name, dram_ptr.rd_ptr, dram_ptr.wr_ptr);
        if(dram_rd_ptr + tensor.getw() < queue_info.entries){
            // The data for this batch is stored in DRAM contiguously
            cluster->read_dram_vec(rv, dram_loc, dram_rd_addr, batch_size);
        }
        else{
            // The data is not contiguous (it wraps around)
            int first_batch_size = (queue_info.entries - dram_rd_ptr) * entry_size;
            cluster->read_dram_vec(rv, dram_loc, dram_rd_addr, first_batch_size);
            vector<std::uint32_t> tmp;
            uint64_t second_dram_rd_addr = alloc.address + tt::io::io_queue_header_size_bytes;
            cluster->read_dram_vec(tmp, dram_loc, second_dram_rd_addr, batch_size - first_batch_size);
            rv.insert(rv.end(), tmp.begin(), tmp.end());
        }
        
        for (int wi=0; wi<tensor.getw(); ++wi) {
            uint32_t offset = 0;
            for (int zi=0; zi<tensor.getz(); ++zi) {
                if (ublock_scan == Dim::R) {
                    for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                        for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                            // tt::io::get_ublock_data()
                            get_ublock_data(queue_info, tensor, rv,  wi*(entry_size/sizeof(uint32_t)), alloc_index, offset, wi, zi, ubr, ubc);
                        }
                    }
                } else if (ublock_scan == Dim::C) {
                    for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                        for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                            get_ublock_data(queue_info, tensor, rv,  wi*(entry_size/sizeof(uint32_t)), alloc_index, offset, wi, zi, ubr, ubc);
                        }
                    }
                } else {
                    log_fatal("Invalid ublock scan order!");
                }
            }
            if (update_rdptr) {
                dram_ptr.incr_rd(tensor.getw());
                update_queue_ptr(cluster, dram_ptr.rd_ptr, dram_loc, alloc.address, queue_info.loc);
            }
        }
        alloc_index++;
    }
    if (update_rdptr) {
        tt_driver_atomics::sfence();
    }
    return tensor;
}


tt_tensor pop_queue_tilized_output_sysmem(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, bool update_rdptr, int pop_count, Dim ublock_scan, const int timeout_in_seconds, int ram_ptr) {

    std::string queue_name = queue_info.name;

    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = pop_count;
    md.shape.tile_height = get_tile_dim_y(queue_info);
    md.shape.tile_width = get_tile_dim_x(queue_info);
    md.is_tilized = true;
    md.data_format = queue_info.data_format;
    tt_tensor tensor(md);
    tensor.reserve_tile_tensor();

    const uint32_t num_entries = desc.bufq_num_slots;
    const uint32_t entry_size = get_entry_size_in_bytes(queue_info, true);
    const uint32_t batch_size = entry_size * tensor.getw();
    log_assert(desc.bufq_mapping.size() == desc.bufq_grid_dim_r * desc.bufq_grid_dim_c, "Queue grid size and number of bufs mismatch.");

    // tt_target_dram dram_loc = {queue_info.target_device, -1/*chan*/, 0/*subchan*/};
    // tt_queue_ptr dram_ptr(queue_info.entries);
    // uint32_t dram_rd_ptr = 0;

    uint32_t *q_ptr = nullptr;
    uint32_t rd_ptr_to_range = 0;
    tt_queue_ptr sysmem_ptr(num_entries);

    // Single timeout clock running for all queues, so need to loop over them all with a single start time first.
    if (queue_info.type == IO_TYPE::Queue){
        std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
        for (int i=0; i<queue_info.alloc_info.size(); i++) {
            q_ptr = reinterpret_cast<uint32_t *>(desc.bufq_mapping.at(i));
            sysmem_ptr.rd_ptr = *q_ptr;
            sysmem_ptr.wr_ptr = *(q_ptr + 1);
            bool queue_is_fully_populated = sysmem_ptr.occupancy() >= pop_count;
            if (timeout_in_seconds > 0 && !queue_is_fully_populated){
                wait_until_host_output_queue_populated(desc, q_ptr, sysmem_ptr, pop_count, timeout_in_seconds);
            } else{
                log_assert(queue_is_fully_populated, "Cannot get {} entries from queue!", pop_count);
            }
        }
        rd_ptr_to_range = sysmem_ptr.get_rd_ptr();
    }
    else if (desc.io_type == IO_TYPE::RandomAccess) {
        // Assign random access ptr
        rd_ptr_to_range = ram_ptr;
        log_assert(ram_ptr < num_entries, "RAM access out of range!");
    }

    for (int alloc_index=0; alloc_index < desc.bufq_mapping.size(); ++alloc_index) {
        log_assert((rd_ptr_to_range + pop_count) <= num_entries, "Entries read must reside in contiguous memory!");
        q_ptr = reinterpret_cast<uint32_t *>(desc.bufq_mapping.at(alloc_index));
        uint32_t offset = (entry_size * rd_ptr_to_range + tt::io::io_queue_header_size_bytes) / sizeof(uint32_t); // div by 4 for uint32 ptr arithmetic
        uint32_t *data = q_ptr + offset;
        vector<uint32_t> rv(data, data + (batch_size/sizeof(uint32_t)));

        for (int wi=0; wi<tensor.getw(); ++wi) {
            uint32_t offset = 0;
            for (int zi=0; zi<tensor.getz(); ++zi) {
                if (ublock_scan == Dim::R) {
                    for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                        for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                            get_ublock_data(queue_info, tensor, rv,  wi*(entry_size/sizeof(uint32_t)), alloc_index, offset, wi, zi, ubr, ubc);
                        }
                    }
                } else if (ublock_scan == Dim::C) {
                    for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                        for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                            get_ublock_data(queue_info, tensor, rv,  wi*(entry_size/sizeof(uint32_t)), alloc_index, offset, wi, zi, ubr, ubc);
                        }
                    }
                } else {
                    log_fatal("Invalid ublock scan order!");
                }
            }
            if (update_rdptr) {
                sysmem_ptr.incr_rd(tensor.getw());
                *q_ptr = sysmem_ptr.rd_ptr;
            }
        }
    }
    return tensor;
}

void pop_outputs_to_tensor_queues(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds) {

    tt_runtime_workload &workload = *get_workload(dram_io_desc.at(0).netlist_path);
    for (tt_dram_io_desc &io : dram_io_desc) {
        std::vector<tt_dram_io_desc> io_desc = {io};
        if (workload.has_untilized_data(io.queue_name)) {
            pop_outputs_to_tqs_hw_untilize(io_desc, timeout_in_seconds);
        } else {
            pop_outputs_to_tqs_sw_untilize(io_desc, timeout_in_seconds);
        }
    }
}

void pop_outputs_to_tqs_hw_untilize(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds) {
    std::string &netlist = dram_io_desc.at(0).netlist_path;
    tt::DEVICE target_type = dram_io_desc.at(0).backend_type;
    tt_cluster *cluster = get_cluster(netlist, target_type);
    tt_runtime_workload &workload = *get_workload(netlist);
    for (tt_dram_io_desc &io : dram_io_desc) {
        int entry_id = 0;
        tt_queue_wrap &queue_wrap = workload.queues[io.queue_name];
        tt_queue_info &queue_info = workload.queues[io.queue_name].my_queue_info;

        // Pop each queue from device until empty
        while (!is_queue_empty(io, queue_info, cluster)) {
            vector<uint32_t> rv = pop_untilized_vector(io, queue_info, cluster, timeout_in_seconds);
            tt_tensor result = reconstruct_tensor_from_untilized(rv, io, false/*batched*/, true);
            log_debug(tt::LogIO, "HW untilized output queue {} pop #{}", queue_info.name, entry_id++);
            if (debug) result.dump();
            // Push to workload queue so it can be used for checking
            if (queue_info.type == IO_TYPE::RandomAccess) {
                queue_wrap.my_io->clear();
                queue_wrap.my_io->push(std::make_shared<tt_tensor>(result));
                break;
            } else {
                queue_wrap.my_io->push(std::make_shared<tt_tensor>(result));
            }
        }
    }
}

void pop_outputs_to_tqs_sw_untilize(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds) {
    std::string &netlist = dram_io_desc.at(0).netlist_path;
    tt::DEVICE target_type = dram_io_desc.at(0).backend_type;
    tt_cluster *cluster = get_cluster(netlist, target_type);
    tt_runtime_workload &workload = *get_workload(netlist);
    for (tt_dram_io_desc &io : dram_io_desc) {
        int entry_id = 0;
        tt_queue_wrap &queue_wrap = workload.queues[io.queue_name];
        tt_queue_info &queue_info = workload.queues[io.queue_name].my_queue_info;
        Dim ublock_scan = workload.get_ublock_scan(queue_info.name);

        bool update_rdptr = true;
        int32_t ram_ptr = -1;
        if (queue_info.type == IO_TYPE::RandomAccess) {
            update_rdptr = false;
            ram_ptr = 0;
        }

        // Pop each queue from device until empty
        while (!is_queue_empty(io, queue_info, cluster)) {
            bool update_rdptr = queue_info.type != IO_TYPE::RandomAccess;
            tt_tensor result = pop_queue_tilized_output(queue_info, cluster, update_rdptr, 1, ublock_scan, timeout_in_seconds, ram_ptr); // result has unpacked data in tile after calling this function
            //result.unpack_data();
            log_debug(tt::LogIO, "SW untilized output queue {} pop #{}", queue_info.name, entry_id++);
            if (debug) result.dump();
            // Push to workload queue so it can be used for checking
            if (queue_info.type == IO_TYPE::RandomAccess) {
                queue_wrap.my_io->clear();
                queue_wrap.my_io->push(std::make_shared<tt_tensor>(result));
                break;
            } else {
                queue_wrap.my_io->push(std::make_shared<tt_tensor>(result));
            }
        }
    }
}

void create_tilized_data_vector(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, tt::tt_TilizedTensorDesc &tilized_tensor_desc, std::vector<uint32_t>& packed_data) {
    tt_dram_io_desc temp_q_desc = q_desc; // This object will be modified to have its buffers on host
    temp_q_desc.bufq_num_slots = py_tensor_desc.shape[0];
    temp_q_desc.io_type = tt::IO_TYPE::Queue;
    std::uint32_t buffer_size = tilized_tensor_desc.buf_size_bytes / 4; // Number of uint32_t entries in the buffer

    temp_q_desc.bufq_mapping.clear();
    
    for(int buf_idx = 0; buf_idx < temp_q_desc.bufq_start_addr_channel.size(); buf_idx++) {
        temp_q_desc.bufq_mapping.push_back(reinterpret_cast<void*>(packed_data.data() + buffer_size * buf_idx)); // Assign host buffers to temp_q_desc object
    }
    static constexpr Tilizer binary_tilizer_backend = Tilizer::FastTilizeMMIOPush;
    auto tilizer = new DeviceTilizer<Tilizer::FastTilizeMMIOPush>({temp_q_desc}, py_tensor_desc, temp_q_desc.s_descriptor, temp_q_desc.tile_height, temp_q_desc.tile_width, true); // offline tilize
    tilizer -> tilize_and_push_to_device({py_tensor_desc}, 0, 0); // "Device" buffers here are those initialized in host_grid.
    
    delete tilizer;
}

void create_tilized_data_scalar(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, tt::tt_TilizedTensorDesc &tilized_tensor_desc, std::vector<uint32_t>& packed_data) {
    auto temp_q_desc = q_desc;
    temp_q_desc.input_count = py_tensor_desc.shape[0];
    tt_runtime_workload &workload   = *get_workload(q_desc.netlist_path);
    tt_queue_info &queue_info       = workload.queues.at(q_desc.queue_name).my_queue_info;

    tt_tensor input_tensor = tt::io::reconstruct_tensor_from_untilized(q_desc, py_tensor_desc, true);
    input_tensor.set_data_format(q_desc.bufq_target_format);
    input_tensor.pack_data(q_desc.tile_height, q_desc.tile_width);

    const bool include_tile_header_size = queue_info.layout != IO_LAYOUT::Flat;
    const uint32_t tile_size = tt::size::get_tile_size_in_bytes(queue_info.data_format, include_tile_header_size, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    
    for(std::uint32_t alloc_idx = 0; alloc_idx < q_desc.bufq_grid_dim_c * q_desc.bufq_grid_dim_r; alloc_idx++) {
        uint32_t offset = alloc_idx * tilized_tensor_desc.buf_size_bytes / 4;
        int gr = alloc_idx / queue_info.grid_size.c;
        int gc = alloc_idx % queue_info.grid_size.c;

        for(int wi = 0; wi < input_tensor.getw(); wi++) {
            for(int zi=0; zi<input_tensor.getz(); ++zi) {
                if(workload.get_ublock_scan(q_desc.queue_name) == Dim::R) {
                    for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                        for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                            set_ublock_data(packed_data, input_tensor, queue_info, tile_size, offset, zi, wi, gr, gc, ubr, ubc);
                        }
                    }
                }
                else if (workload.get_ublock_scan(q_desc.queue_name) == Dim::C) {
                    for (int ubc=0; ubc<queue_info.dim.mblock_n; ++ubc) {
                        for (int ubr=0; ubr<queue_info.dim.mblock_m; ++ubr) {
                            set_ublock_data(packed_data, input_tensor, queue_info, tile_size, offset, zi, wi, gr, gc, ubr, ubc);
                        }
                    }
                }
                else {
                    log_fatal("Invalid ublock scan order!");
                }
            }
        }
    }
}

tt::tt_TilizedTensorDesc tilize_tensor(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc) {
    std::uint32_t packed_data_size = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, q_desc.layout == IO_LAYOUT::Tilized, 
                                    q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width) * py_tensor_desc.shape[0];
    tt_TilizedTensorDesc rval;
    rval.owner = OWNERSHIP::Backend; // Backend allocates memory for this tensor
    rval.format = q_desc.bufq_target_format;
    rval.num_buffers = q_desc.bufq_grid_dim_r * q_desc.bufq_grid_dim_c;
    rval.buf_size_bytes = packed_data_size;

    packed_data_size = (packed_data_size * q_desc.bufq_grid_dim_c * q_desc.bufq_grid_dim_r) / 4;   
    std::vector<uint32_t> concatenated_packed_data(packed_data_size, 0);
    bool force_slow_offline_tilize = parse_env("TT_BACKEND_FORCE_SW_OFFLINE_TILIZE", false);
    if(hw_tilizer_conversions.find({py_tensor_desc.format, q_desc.bufq_target_format}) != hw_tilizer_conversions.end() and !force_slow_offline_tilize) {
        create_tilized_data_vector(q_desc, py_tensor_desc, rval, concatenated_packed_data);
    }
    else {
        create_tilized_data_scalar(q_desc, py_tensor_desc, rval, concatenated_packed_data);
    }
    shared_ptr<vector<uint32_t>> mem = get_workload(q_desc.netlist_path)->allocate_tilized_memory(q_desc.queue_name, py_tensor_desc.shape[0]);
    std::memcpy(mem->data(), concatenated_packed_data.data(), packed_data_size * 4);

    rval.ptr = mem->data();
    return rval;
}

template<typename T>
void binarize_tensor(const T& tensor, const std::string& file_path) {
    if constexpr(is_same<T, tt::tt_TilizedTensorDesc>()) {
        tt::data_binary::dump_file(file_path, reinterpret_cast<const std::uint32_t*>(tensor.ptr), tensor.buf_size_bytes * tensor.num_buffers);
    } else {
        std::uint32_t tensor_size_bytes = tensor.shape[0] *  tensor.shape[1] *  tensor.shape[2] *  tensor.shape[3] * tensor.itemsize;
        tt::data_binary::dump_file(file_path, reinterpret_cast<const std::uint32_t*>(tensor.ptr), tensor_size_bytes);
    }
}

template<typename T>
void debinarize_tensor(T& tensor, const std::string& file_path) {
    auto debinarized_data = std::make_shared<std::vector<uint32_t>>();
    tt::data_binary::read_file(file_path, *(debinarized_data.get()));
    tt::mem::host_shared_memory.insert({reinterpret_cast<void *>(debinarized_data->data()), debinarized_data});
    tensor.ptr = debinarized_data -> data();
    tensor.owner = OWNERSHIP::Backend;
}

bool push_input_to_device(const tt::tt_dram_io_desc &q_desc, const tt::tt_TilizedTensorDesc &tilized_tensor, const int timeout_in_seconds, const int ram_ptr) {
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    tt_cluster *cluster = get_cluster(q_desc.netlist_path, q_desc.backend_type);
    std::uint32_t entry_size = tilized_tensor.buf_size_bytes;
    std::uint32_t block_size = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, q_desc.layout == IO_LAYOUT::Tilized, 
                                    q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width);
    log_debug(tt::LogIO, "Pushing Tilized Tensor to {}", q_desc.queue_name);
    log_assert(!(entry_size % block_size), "When pushing pretilized data, tilized_tensor.buf_size_bytes should be divisible by individual entry size");
    log_assert(tilized_tensor.format == q_desc.bufq_target_format, "Tilized tensor format does not match queue format");
    std::uint32_t push_count = entry_size / block_size;    
    std::uint32_t alloc_idx = 0;
    tt_runtime_workload &workload   = *get_workload(q_desc.netlist_path);
    tt_queue_info &queue_info = workload.queues.at(q_desc.queue_name).my_queue_info;
    tt_queue_ptr q_ptr(queue_info.entries);
    
    log_assert(tilized_tensor.num_buffers == queue_info.alloc_info.size(), "Num buffers specified in tilized tensor do not match num buffers in queue grid");
    uint32_t q_wr_ptr = 0;
    for(const auto& alloc : queue_info.alloc_info) {
        tt_target_dram dram = {queue_info.target_device, alloc.channel, 0};
        if(q_desc.io_type == IO_TYPE::Queue) {
            wait_until_input_queue_freed(start_time, cluster, alloc, dram, queue_info.loc, q_ptr, push_count, timeout_in_seconds);
            read_from_queue(cluster, q_ptr.ptrs, dram, alloc.address, queue_info.loc, 8);
            q_wr_ptr = q_ptr.get_wr_ptr();
            q_ptr.incr_wr(push_count);
        }
        else {
            q_wr_ptr = ram_ptr;
        }
        std::vector<uint32_t> vec_to_write(reinterpret_cast<const uint32_t*>(tilized_tensor.ptr) + alloc_idx * entry_size / 4, reinterpret_cast<const uint32_t*>(tilized_tensor.ptr) + alloc_idx * entry_size / 4 + entry_size / 4);
        std::uint32_t wr_addr = alloc.address + tt::io::io_queue_header_size_bytes + q_wr_ptr * block_size;
        const uint32_t* data_ptr = reinterpret_cast<const uint32_t*>(tilized_tensor.ptr) + alloc_idx * entry_size / 4;
        write_to_queue(cluster, data_ptr, entry_size, dram, wr_addr, queue_info.loc);
        alloc_idx++;
        if(q_desc.io_type == IO_TYPE::Queue) {
            update_queue_ptr(cluster, q_ptr.wr_ptr, dram, alloc.address + 4, queue_info.loc, true);
            flush_wptr_update(cluster, queue_info.loc, queue_info.target_device, alloc.channel);
        }
    }
    return true;
}
bool push_input_to_device(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, const bool push_one, const int timeout_in_seconds, const int ram_ptr) {
    tt_cluster *cluster = get_cluster(q_desc.netlist_path, q_desc.backend_type);
    vector<tt::tt_dram_io_desc> io_desc = {q_desc};
    tt_runtime_workload &workload   = *get_workload(q_desc.netlist_path);
    tt_queue_info &queue_info       = workload.queues.at(q_desc.queue_name).my_queue_info;
    Dim ublock_scan = workload.get_ublock_scan(q_desc.queue_name);
    if (q_desc.backend_type == DEVICE::Silicon){
        tt::cpuset::tt_cpuset_allocator::bind_thread_to_cpuset(cluster->get_cluster_desc(), queue_info.target_device);
    }
    // Determine whether HW Tilize or SW Tilize path will be used.
    Tilizer tilizer_backend = get_tilizer_based_on_io_config(q_desc, py_tensor_desc.format, q_desc.bufq_target_format, cluster -> type);
    if (force_sw_tilize() or tilizer_backend == Tilizer::SlowTilize){
        // tt_runtime_workload &workload   = *get_workload(q_desc.netlist_path);
        // tt_queue_info &queue_info       = workload.queues.at(q_desc.queue_name).my_queue_info;
        log_assert(q_desc.bufq_target_format == queue_info.data_format, "Unexpected data format difference");
        log_debug(tt::LogIO, "Using Slow SW Tilize for queue: {}", q_desc.queue_name);
        log_assert(q_desc.layout != tt::IO_LAYOUT::Flat, "Flat layout is not supported for SW Tilize");
        
        // Initialize shuffler values and perform prestriding if needed
        tt_PytorchTensorDesc shuffled_tensor(py_tensor_desc);
        aligned_vector<uint16_t> shuffled_data_half = {};
        aligned_vector<uint32_t> shuffled_data = {};

        tt::io::slow_prestride(py_tensor_desc, shuffled_tensor, q_desc, shuffled_data_half, shuffled_data);
        tt_tensor input = tt::io::reconstruct_tensor_from_untilized(q_desc, shuffled_tensor, !push_one);

        push_queue_tilized_input(queue_info, input, cluster, timeout_in_seconds, ram_ptr, ublock_scan);
    } else {
        if(tilizer_backend == Tilizer::FastTilizeDevicePush){
            log_debug(tt::LogIO, "Using HW Tilize with writes through the TLB for queue: {}", q_desc.queue_name);
            DeviceTilizer<Tilizer::FastTilizeDevicePush> *fast_tilizer = get_tilizer<Tilizer::FastTilizeDevicePush>(q_desc.queue_name, io_desc, cluster, py_tensor_desc, q_desc.s_descriptor, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
            perf::ScopedEventProfiler profile(perf::HostEventType::HW_TILIZE);
            vector<tt::tt_PytorchTensorDesc> py_desc;
            if(q_desc.s_descriptor.stride > 0){
                py_desc = {fast_tilizer -> shuffle_tensor(py_tensor_desc)}; // Shuffle tensor so device can perform convolution on it
            }
            else{
                py_desc = {py_tensor_desc};
            }
            fast_tilizer->tilize_and_push_to_device(py_desc, timeout_in_seconds, ram_ptr);
        }
        else{
            log_debug(tt::LogIO, "Using HW Tilize with MMIO writes for queue: {}", q_desc.queue_name);
            DeviceTilizer<Tilizer::FastTilizeMMIOPush> *fast_tilizer = get_tilizer<Tilizer::FastTilizeMMIOPush>(q_desc.queue_name, io_desc, cluster, py_tensor_desc, q_desc.s_descriptor, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
            perf::ScopedEventProfiler profile(perf::HostEventType::HW_TILIZE);
            vector<tt::tt_PytorchTensorDesc> py_desc;
            if(q_desc.s_descriptor.stride > 0){
                py_desc = {fast_tilizer -> shuffle_tensor(py_tensor_desc)}; // Shuffle tensor so device can perform convolution on it
            }
            else{
                py_desc = {py_tensor_desc};
            }
            fast_tilizer->tilize_and_push_to_device(py_desc, timeout_in_seconds, ram_ptr);
        }
    }
    return true;
}

bool pop_output_from_device(const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds) {
    perf::ScopedEventProfiler profile(perf::HostEventType::POP_OUTPUT);
    tt_runtime_workload &workload = *get_workload(q_desc.netlist_path);
    tt_queue_info &queue_info = workload.queues[q_desc.queue_name].my_queue_info;
    tt_cluster *cluster = get_cluster(q_desc.netlist_path, q_desc.backend_type);

    if (q_desc.backend_type == DEVICE::Silicon && queue_info.loc == QUEUE_LOCATION::DRAM){
        tt::cpuset::tt_cpuset_allocator::bind_thread_to_cpuset(cluster->get_cluster_desc(), queue_info.target_device);
    }


    int pop_count = pop_one ? 1 : queue_info.input_count;
    log_assert(q_desc.io_type == IO_TYPE::Queue, "Only queues can be popped!");

    if (queue_info.loc == QUEUE_LOCATION::HOST) {
        log_assert(q_desc.bufq_mapping.size() != 0, "Must have translated queue address in IO descriptor");
        pop_from_sysmem(q_desc, pop_count, timeout_in_seconds);
    } else {
        pop_from_dram(queue_info, pop_count, cluster, timeout_in_seconds);
    }
    return true;
}

bool get_output_from_device(const tt::tt_dram_io_desc &q_desc, tt::tt_PytorchTensorDesc &py_tensor_desc, const bool get_one, const int timeout_in_seconds, const int ram_ptr) {
    tt_cluster *cluster = get_cluster(q_desc.netlist_path, q_desc.backend_type);
    tt_runtime_workload &workload = *get_workload(q_desc.netlist_path);
    std::string queue_name = q_desc.queue_name;
    tt_queue_info &queue_info = workload.queues[queue_name].my_queue_info;

    if (q_desc.backend_type == DEVICE::Silicon && queue_info.loc == QUEUE_LOCATION::DRAM){
        tt::cpuset::tt_cpuset_allocator::bind_thread_to_cpuset(cluster->get_cluster_desc(), queue_info.target_device);
    }


    int num_entries = get_one ? 1 : queue_info.input_count;
    int tensor_ydim = q_desc.mblock_m * q_desc.ublock_rt * q_desc.bufq_grid_dim_r * q_desc.vstack_factor;
    int tensor_xdim = q_desc.mblock_n * q_desc.ublock_ct * q_desc.bufq_grid_dim_c * q_desc.hstack_factor;
    int tensor_zdim = q_desc.t / (q_desc.hstack_factor * q_desc.vstack_factor);
    int tensor_wdim = num_entries;

    if (workload.has_untilized_data(queue_name)) {
        log_assert(q_desc.hstack_factor == 1, "Cannot hstack an already untilized tensor!");
        log_assert(q_desc.vstack_factor > 1 ? q_desc.stack_row_major : true, "Vstack on untilized data is only supported for row major order.");
    } 
    log_assert(q_desc.t % q_desc.hstack_factor == 0, "hstack factor must divide t evenly!");
    log_assert(q_desc.t % q_desc.vstack_factor == 0, "vstack factor must divide t evenly!");

    if (q_desc.bufq_target_format == DataFormat::Float32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<float>(
            nullptr, tensor_wdim, tensor_zdim, tensor_ydim, tensor_xdim, q_desc.bufq_target_format, py_tensor_desc.dim, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    } else if(q_desc.bufq_target_format == DataFormat::Int8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int8_t>(
            nullptr, tensor_wdim, tensor_zdim, tensor_ydim, tensor_xdim, q_desc.bufq_target_format, py_tensor_desc.dim, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    } else if(q_desc.bufq_target_format == DataFormat::UInt8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint8_t>(
            nullptr, tensor_wdim, tensor_zdim, tensor_ydim, tensor_xdim, q_desc.bufq_target_format, py_tensor_desc.dim, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    } else if(q_desc.bufq_target_format == DataFormat::Int32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int32_t>(
            nullptr, tensor_wdim, tensor_zdim, tensor_ydim, tensor_xdim, q_desc.bufq_target_format, py_tensor_desc.dim, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    }  
    else {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint16_t>(
            nullptr, tensor_wdim, tensor_zdim, tensor_ydim, tensor_xdim, q_desc.bufq_target_format, py_tensor_desc.dim, get_tile_dim_y(queue_info), get_tile_dim_x(queue_info));
    }

    if (queue_info.loc == QUEUE_LOCATION::HOST) {
        log_assert(q_desc.bufq_mapping.size() != 0, "Must have translated queue address in IO descriptor");
        if (workload.has_untilized_data(queue_name)) {
            perf::ScopedEventProfiler profile(perf::HostEventType::GET_UNTILIZED_OUTPUT);
            py_tensor_desc.ptr = get_untilized_from_sysmem(q_desc, queue_info, num_entries, ram_ptr, timeout_in_seconds);
        } else {
            perf::ScopedEventProfiler profile(perf::HostEventType::GET_TILIZED_OUTPUT);
            py_tensor_desc.ptr = get_tilized_from_sysmem(q_desc, queue_info, cluster, ram_ptr, num_entries, timeout_in_seconds);
        }
    } else if (queue_info.loc == QUEUE_LOCATION::DRAM) {
        if (workload.has_untilized_data(queue_name)) {
            perf::ScopedEventProfiler profile(perf::HostEventType::GET_UNTILIZED_OUTPUT);
            py_tensor_desc.ptr = get_untilized_from_dram(q_desc, queue_info, cluster, num_entries, ram_ptr, timeout_in_seconds) ;
        } else {
            perf::ScopedEventProfiler profile(perf::HostEventType::GET_TILIZED_OUTPUT);
            py_tensor_desc.ptr = get_tilized_from_dram(q_desc, queue_info, cluster, ram_ptr, num_entries, timeout_in_seconds);
        }
    } else {
        py_tensor_desc.ptr = nullptr;
        return false;
    }

    // If data format is Int8 we need to convert the data to 2's complement.
    // Tilized functions implement this conversion internally
    if (workload.has_untilized_data(queue_name) && queue_info.data_format == DataFormat::Int8) {
       tt::io::convert_int8_pytorch_tensor_to_host_format(py_tensor_desc);
    } else if (workload.has_untilized_data(queue_name) && queue_info.data_format == DataFormat::Int32) {
       tt::io::convert_int32_pytorch_tensor_to_host_format(py_tensor_desc);
    }
    return true;
}
}
template void tt::io::binarize_tensor<tt::tt_TilizedTensorDesc>(const tt::tt_TilizedTensorDesc&, const std::string&);
template void tt::io::binarize_tensor<tt::tt_PytorchTensorDesc>(const tt::tt_PytorchTensorDesc&, const std::string&);
template void tt::io::debinarize_tensor<tt::tt_TilizedTensorDesc>(tt::tt_TilizedTensorDesc&, const std::string&);
template void tt::io::debinarize_tensor<tt::tt_PytorchTensorDesc>(tt::tt_PytorchTensorDesc&, const std::string&);