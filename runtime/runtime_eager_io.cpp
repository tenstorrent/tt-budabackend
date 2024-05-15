// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/cpuset_lib.hpp"
#include "common/tt_queue_ptr.hpp"
#include "runtime_eager_io.hpp"
#include "runtime_utils.hpp"

using namespace tt::io;
namespace tt::eager::io {

using tt_qptr_map = unordered_map<QUEUE_LOCATION, unordered_map<uint64_t, tt_queue_ptr>>;
using block_data_vec = vector<uint32_t>;

// Helper types and functions
std::vector<uint32_t> get_block_dims(const tt_py_desc &addr_tensor, const tt_py_desc &data_tensor) {
    uint32_t block_width = data_tensor.shape[3] / addr_tensor.shape[3];
    uint32_t block_height = data_tensor.shape[2] / addr_tensor.shape[2];
    log_assert(data_tensor.shape[3] % addr_tensor.shape[3] == 0, "Tensor is not evenly distributed across grid");
    log_assert(data_tensor.shape[2] % addr_tensor.shape[2] == 0, "Tensor is not evenly distributed across grid");
    return {block_height, block_width};
}

struct tt_qptr_cache {
    tt_qptr_map qptr_map;
    tt_queue_ptr &get_qptr(const QUEUE_LOCATION location, const int chip, const int chan, const uint32_t addr) {
        int _chan = chan;
        if (location == QUEUE_LOCATION::HOST) _chan = 0;
        uint64_t addr_tag = (uint64_t)chip << 48 | (uint64_t)_chan << 32 | addr;
        return qptr_map.at(location).at(addr_tag);
    }
    void add_qptr(const QUEUE_LOCATION location, const int chip, const int chan, const uint32_t addr, const int entries) {
        int _chan = chan;
        if (location == QUEUE_LOCATION::HOST) _chan = 0;
        uint64_t addr_tag = (uint64_t)chip << 48 | (uint64_t)_chan << 32 | addr;
        qptr_map[location].insert({addr_tag, tt_queue_ptr(entries)});
    }
};

tt_qptr_cache *get_qptr_cache(const std::string tag) {
    tt_qptr_cache *qptr_cache;
    if (tt_object_cache<tt_qptr_cache>::exists(tag)) {
        qptr_cache = tt_object_cache<tt_qptr_cache>::get(tag);
    } else {
        qptr_cache = new tt_qptr_cache();
        tt_object_cache<tt_qptr_cache>::set(tag, qptr_cache);
        log_debug(tt::LogIO, "{} cache MISS, added object to cache with tag '{}'", __FUNCTION__, tag);
    }
    return qptr_cache;
};

block_data_vec* get_block_data_vec(const std::string tag, uint32_t block_size) {
    block_data_vec *block_data;
    string lookup_tag = tag + to_string(block_size);
    if(tt_object_cache<block_data_vec>::exists(lookup_tag)) {
        block_data = tt_object_cache<block_data_vec>::get(lookup_tag);
    } else{
        block_data = new vector<uint32_t>(block_size / sizeof(uint32_t), 0);
        tt_object_cache<block_data_vec>::set(lookup_tag, block_data);
        log_debug(tt::LogIO, "{} cache MISS, added object to cache with tag '{}'", __FUNCTION__, lookup_tag);
    }
    return block_data;
}

void initialize(const tt::ARCH &arch, const std::set<chip_id_t> &target_device_ids) {
    tt::io::info.proc_id = getpid();
    std::string cluster_file = "";
    if ((arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0)) {
        cluster_file = tt::io::info.output_dir + "/cluster_desc.yaml";
        if (!fs::exists(cluster_file)) {
            generate_cluster_desc_yaml(tt::io::info.output_dir);
        }
    }
    tt_cluster *cluster = new tt_cluster; // freed by tt_object_cache
    cluster->open_device(arch, TargetDevice::Silicon, target_device_ids, get_soc_description_file(arch, TargetDevice::Silicon, tt::io::info.output_dir), cluster_file);
    cluster->start_device({.init_device = false});
    tt_object_cache<tt_cluster>::set(CACHE_TAG, cluster);
    // dummy workload to alloate host side memory for return tensors
    tt_runtime_workload *workload = new tt_runtime_workload;
    tt_object_cache<tt_runtime_workload>::set(CACHE_TAG, workload);
}

void finish() {
    log_assert(tt::io::info.proc_id == getpid(), "Current child process {} was not initialized!", getpid());
    tt_object_cache<tt_qptr_cache>::destroy(CACHE_TAG);
    tt_object_cache<tt_cluster>::destroy(CACHE_TAG);
    tt_object_cache<tt_runtime_workload>::destroy(CACHE_TAG);  // frees up any allocated memory
    tt_object_cache<block_data_vec>::destroy();
    tt::cpuset::tt_cpuset_allocator::clear_state_and_cpuset_pins();
}

void init_queue(const QUEUE_LOCATION &loc, const int target_device, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, const int &entries) {
    tt_cluster *cluster = get_cluster(CACHE_TAG, DEVICE::Silicon);
    tt_qptr_cache *qptr_cache = get_qptr_cache(CACHE_TAG);

    int chip_id = target_device;
    vector<uint32_t> header_vec = {0,0,0,0,0,0,0,0};

    log_assert(chan_tensor.format == DataFormat::RawUInt32, "chan tensor must be UInt32 format");
    log_assert(addr_tensor.format == DataFormat::RawUInt32, "addr tensor must be UInt32 format");

    const auto &shape = addr_tensor.shape;
    const auto addr_data = reinterpret_cast<const uint32_t*>(addr_tensor.ptr);
    const auto chan_data = reinterpret_cast<const uint32_t*>(chan_tensor.ptr);

    for (int gr=0; gr<shape[0]; gr++) {
        for (int gc=0; gc<shape[1]; gc++) {
            for (int br=0; br<shape[2]; br++) {
                for (int bc=0; bc<shape[3]; bc++) {
                    int block_id = gr*shape[1]*shape[2]*shape[3] + gc*shape[2]*shape[3] + br*shape[3] + bc;
                    int chan_id = chan_data[block_id];
                    uint32_t io_addr = addr_data[block_id];

                    if (loc == QUEUE_LOCATION::DRAM) {
                        cluster->write_dram_vec(header_vec, tt_target_dram{chip_id, chan_id, 0}, io_addr);
                    } else {
                        cluster->write_sysmem_vec(header_vec, io_addr, chan_id, chip_id);
                    }
                    qptr_cache->add_qptr(loc, chip_id, chan_id, io_addr, entries);
                }
            }
        }
    }
}

void push_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, const tt_py_desc &data_tensor, const IO_TYPE io_type, const int ram_ptr) {
    log_assert(!tt::is_tt_data_format(data_tensor.format), "data tensor must be a standard data format");  // format conversion spec not supported on the API
    log_assert(loc == QUEUE_LOCATION::DRAM, "Push to host queue is not supported!");
    tt_cluster *cluster = get_cluster(CACHE_TAG, DEVICE::Silicon);
    tt_qptr_cache *qptr_cache = get_qptr_cache(CACHE_TAG);

    const auto &shape = addr_tensor.shape;  // [grid_r, grid_c, block_r, block_c]
    const auto addr_data = reinterpret_cast<const uint32_t*>(addr_tensor.ptr);
    const auto chan_data = reinterpret_cast<const uint32_t*>(chan_tensor.ptr);
    const auto block_dims = get_block_dims(addr_tensor, data_tensor);

    // Calculate tile and block sizes
    const uint32_t tile_size = tt::size::get_tile_size_in_bytes(data_tensor.format, true /*include headers*/);
    const uint32_t tile_size_headerless = tt::size::get_tile_size_in_bytes(data_tensor.format, false /*exclude headers*/);
    const uint32_t block_tiles = block_dims[0]/tt::constants::TILE_HEIGHT * block_dims[1]/tt::constants::TILE_WIDTH;
    const uint32_t block_size = block_tiles * tile_size;
    vector<uint32_t> tile_header = {tile_size >> 4, 0, 0, 0};
    block_data_vec* block_data = get_block_data_vec(CACHE_TAG, block_size);

    // TODO: multithreading, try AVX ('-mavx2 -mfma' may already be doing this for us)
    uint32_t src_offset = 0;
    vector<uint32_t> rdptr, wrptr;
    for (int gr=0; gr<shape[0]; gr++) {
        for (int gc=0; gc<shape[1]; gc++) {
            for (int br=0; br<shape[2]; br++) {
                for (int bc=0; bc<shape[3]; bc++) {
                    int block_id = gr*shape[1]*shape[2]*shape[3] + gc*shape[2]*shape[3] + br*shape[3] + bc;
                    int chan_id = chan_data[block_id];
                    uint32_t io_addr = addr_data[block_id];
                    uint32_t wr_addr;
                    tt_target_dram dram = {chip_id, chan_id, 0/*subchan_id*/};

                    uint32_t dst_offset = 0;
                    for (int ti=0; ti<block_tiles; ti++) {
                        const auto src_data = reinterpret_cast<const uint32_t*>(data_tensor.ptr);
                        std::memcpy((*block_data).data() + dst_offset, tile_header.data(), 16);
                        std::memcpy((*block_data).data() + dst_offset + 4, src_data + src_offset, tile_size_headerless);
                        dst_offset += tile_size / sizeof(uint32_t);
                        src_offset += tile_size_headerless / sizeof(uint32_t);
                    }

                    if (io_type == IO_TYPE::Queue) {
                        // retrieve local shadow copy of the queue pointer to avoid to refresh values from dram
                        tt_queue_ptr &qptr = qptr_cache->get_qptr(loc, chip_id, chan_id, io_addr);
                        while (qptr.full()) {
                            cluster->read_dram_vec(rdptr, dram, io_addr, 4); // refresh and try again
                            qptr.rd_ptr = rdptr[0];
                        }
                        wr_addr = io_addr + tt::io::io_queue_header_size_bytes + (qptr.incr_get_wr() * block_size);
                        cluster->write_dram_vec((*block_data), dram, wr_addr);
                        wrptr = {qptr.wr_ptr};
                        cluster->write_dram_vec(wrptr, dram, io_addr + 4);
                    } else {
                        wr_addr = io_addr + tt::io::io_queue_header_size_bytes + (ram_ptr * block_size);
                        cluster->write_dram_vec((*block_data), dram, wr_addr);
                    }
                    
                }
            }
        }
    }
}

void get_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor, tt_py_desc &data_tensor, const IO_TYPE io_type, const int ram_ptr, const bool untilized_output) {
    log_assert(!tt::is_tt_data_format(data_tensor.format), "data tensor must be a standard data format");  // format conversion spec not supported on the API
    tt_cluster *cluster = get_cluster(CACHE_TAG, DEVICE::Silicon);
    tt_qptr_cache *qptr_cache = get_qptr_cache(CACHE_TAG);
    tt_runtime_workload *workload = get_workload(CACHE_TAG);

    const auto &shape = addr_tensor.shape;  // [grid_r, grid_c, block_r, block_c]
    const auto addr_data = reinterpret_cast<const uint32_t*>(addr_tensor.ptr);
    const auto chan_data = reinterpret_cast<const uint32_t*>(chan_tensor.ptr);
    const auto block_dims = get_block_dims(addr_tensor, data_tensor);

    // Calculate tile and block sizes
    const uint32_t tile_size = tt::size::get_tile_size_in_bytes(data_tensor.format, !untilized_output /*include headers*/);
    const uint32_t tile_size_headerless = tt::size::get_tile_size_in_bytes(data_tensor.format, false /*exclude headers*/);
    const uint32_t block_tiles = block_dims[0]/tt::constants::TILE_HEIGHT * block_dims[1]/tt::constants::TILE_WIDTH;
    const uint32_t block_size = block_tiles * tile_size;
    const uint32_t block_size_headerless = block_tiles * tile_size_headerless;

    block_data_vec* block_data = get_block_data_vec(CACHE_TAG, block_size);

    // if reading a dram queue, we need to allocate host memory for the output tensor
    // TODO: can we reuse the memory from the data tensor if it's already been allocated for us?
    int num_bytes = data_tensor.shape[3] * data_tensor.shape[2] * data_tensor.shape[1] * data_tensor.shape[0] * data_tensor.itemsize;
    int num_elements = num_bytes / sizeof(uint32_t);
    shared_ptr<vector<uint32_t>> mem = workload->allocate_untilized_memory(num_elements);

    // static unordered_map<int, shared_ptr<vector<uint32_t>>> blah;
    // if (blah.find(num_elements) == blah.end()) {
    //     blah[num_elements] = workload->allocate_untilized_memory(num_elements);
    // }
    // mem = blah[num_elements];

    uint32_t dst_offset = 0;
    for (int gr=0; gr<shape[0]; gr++) {
        for (int gc=0; gc<shape[1]; gc++) {
            for (int br=0; br<shape[2]; br++) {
                for (int bc=0; bc<shape[3]; bc++) {
                    int block_id = gr*shape[1]*shape[2]*shape[3] + gc*shape[2]*shape[3] + br*shape[3] + bc;
                    int chan_id = chan_data[block_id];
                    uint32_t io_addr = addr_data[block_id];
                    uint32_t rd_addr = 0;

                    if (loc == QUEUE_LOCATION::DRAM) {
                        tt_target_dram dram = {chip_id, chan_id, 0/*subchan_id*/};
                        if (io_type == IO_TYPE::Queue) {
                            tt_queue_ptr &qptr = qptr_cache->get_qptr(loc, chip_id, chan_id, io_addr);
                            vector<uint32_t> wrptr;
                            while (qptr.empty()) {
                                cluster->read_dram_vec(wrptr, dram, io_addr + 4, 4); // refresh and try again
                                qptr.wr_ptr = wrptr[0];
                            }
                            rd_addr = io_addr + tt::io::io_queue_header_size_bytes + (qptr.get_rd_ptr() * block_size);
                        } else {
                            rd_addr = io_addr + tt::io::io_queue_header_size_bytes + (ram_ptr * block_size);
                        }
                        // read block from dram
                        cluster->read_dram_vec(*block_data, dram, rd_addr, block_size);
                        // copy block to host memory
                        if (untilized_output) {
                            std::memcpy(mem->data() + dst_offset, (*block_data).data(), block_size_headerless);
                            log_assert(block_size == block_size_headerless, "Data src and dst must have the same untilized layout");
                            dst_offset += block_size_headerless / sizeof(uint32_t);
                        } else {
                            uint32_t src_offset = 0;
                            for (int ti=0; ti<block_tiles; ti++) {
                                std::memcpy(mem->data() + dst_offset, (*block_data).data() + src_offset + 4, tile_size_headerless);
                                src_offset += tile_size / sizeof(uint32_t);
                                dst_offset += tile_size_headerless / sizeof(uint32_t);
                            }
                        }
                        data_tensor.ptr = mem->data();
                    } else if (loc == QUEUE_LOCATION::HOST) {
                        if (io_type == IO_TYPE::Queue) {
                            uint32_t *queue_header = reinterpret_cast<uint32_t *>(cluster->host_dma_address(io_addr, chan_id, chip_id));
                            tt_queue_ptr &qptr = qptr_cache->get_qptr(loc, chip_id, chan_id, io_addr);
                            while (qptr.empty()) qptr.wr_ptr = *(queue_header + 1); // refresh and try again
                            rd_addr = io_addr + tt::io::io_queue_header_size_bytes + (qptr.get_rd_ptr() * block_size);
                        } else {
                            rd_addr = io_addr + tt::io::io_queue_header_size_bytes + (ram_ptr * block_size);
                        }
                        uint32_t *data = reinterpret_cast<uint32_t *>(cluster->host_dma_address(rd_addr, chan_id, chip_id));
                        if (untilized_output) {
                            log_assert((shape[0]*shape[1]*shape[2]*shape[3] == 1), "Expected a single entry in queue grid");
                            data_tensor.ptr = data;
                        } else {
                            uint32_t src_offset = 0;
                            for (int ti=0; ti<block_tiles; ti++) {
                                std::memcpy(mem->data() + dst_offset, data + src_offset + 4, tile_size_headerless);
                                src_offset += tile_size / sizeof(uint32_t);
                                dst_offset += tile_size_headerless / sizeof(uint32_t);
                            }
                            data_tensor.ptr = mem->data();
                        }
                    } else {
                        log_assert(false, "Invalid queue location!");
                    }
                }
            }
        }
    }
    data_tensor.owner = OWNERSHIP::Backend;
}

void pop_tensor(const QUEUE_LOCATION &loc, const int chip_id, const tt_py_desc &chan_tensor, const tt_py_desc &addr_tensor) {
    tt_cluster *cluster = get_cluster(CACHE_TAG, DEVICE::Silicon);
    tt_qptr_cache *qptr_cache = get_qptr_cache(CACHE_TAG);

    const auto &shape = addr_tensor.shape;  // [grid_r, grid_c, block_r, block_c]
    const auto addr_data = reinterpret_cast<const uint32_t*>(addr_tensor.ptr);
    const auto chan_data = reinterpret_cast<const uint32_t*>(chan_tensor.ptr);

    for (int gr=0; gr<shape[0]; gr++) {
        for (int gc=0; gc<shape[1]; gc++) {
            for (int br=0; br<shape[2]; br++) {
                for (int bc=0; bc<shape[3]; bc++) {
                    int block_id = gr*shape[1]*shape[2]*shape[3] + gc*shape[2]*shape[3] + br*shape[3] + bc;
                    int chan_id = chan_data[block_id];
                    uint32_t io_addr = addr_data[block_id];

                    tt_queue_ptr &qptr = qptr_cache->get_qptr(loc, chip_id, chan_id, io_addr);
                    log_assert(!qptr.empty(), "Cannot pop an empty queue!");

                    qptr.incr_rd();
                    vector<uint32_t> rdptr = {qptr.rd_ptr};

                    if (loc == QUEUE_LOCATION::DRAM) {
                        cluster->write_dram_vec(rdptr, tt_target_dram{chip_id, chan_id, 0}, io_addr);
                    } else {
                        cluster->write_sysmem_vec(rdptr, io_addr, chan_id, chip_id);
                    }
                }
            }
        }
    }
}

} // namespace tt::io
