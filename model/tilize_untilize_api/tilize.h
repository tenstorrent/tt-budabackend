// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <tuple>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "common/base.hpp"
#include "visibility.h"
#include "runtime/runtime_workload.hpp"
#include "loader/tt_cluster.hpp"
#include <mutex>
#include <condition_variable>
#include <queue>
#include "common/io_lib.hpp"
#include "common/tt_threadpool.hpp"

using tt::tt_dram_io_desc;
using tt::tt_PytorchTensorDesc;
using tt::TargetDevice;
using VersimDramWrType = std::function<void(uint32_t, uint8_t*, int, uint32_t, uint32_t)>;
using VersimDramRdType = std::function<uint32_t(uint32_t, uint32_t, uint32_t)>;

// Tensor: 4D source data, shape & layout described by tt_PytorchTensorDesc.
// Tile: 16x16 source data, elements are stored in row major, each row must be 32B-aligned.
// Quad (Tile): 2x2 tiles on the source side. The tiles within a quad are copied in row-major order.
// So A B (where each is a 16x16 tile) is written to the destination as A,B,C,D.
//    C D
// Quads are the smallest unit that can be transferred - tensors and blocks must be a multiple of 32x32.
// In the destination, each quad is preceeded by a 16B tile header (containing size) and 16B of empty trailer for alignment.
// Sub-block: a 2D region of quads (i.e. from the same Z,W face of the tensor) that are written in row-major layout.
// Block: a 3D volume of sub-blocks (i.e. from the same W of the tensor) that are written to a destination queue together.
// A block is the entire portion of the 3D volume (X,Y,Z volume at the same W) that is written to a single queue.
// Each face of the input tensor is partitioned into blocks based on the shape of the queue grid.
// Slot: the space in a destination queue for a single block. The queue wptr counts slots.
// (Queue) Grid: 2D array of destination queues. Each tensor face is evenly partitioned over the grid. Each partition is a block.
//
// Elements within each tensor row must be packed. (i.e. X stride must be sizeof(element))
// Each row of the tensor must be 32B-aligned.
// The tensor X dimension must be divisible by 32 * grid X dimension.
// The tensor Y dimension must be divisible by 32 * grid Y dimension.
// Sub-block X & Y dimensions must divide block X & Y dimensions.

enum class Tilizer{
    SlowTilize = 0,
    FastTilizeDevicePush = 1,
    FastTilizeMMIOPush = 2,
};

enum class Layout {
    Tilized = 0,
    MegaRow = 1,
    Flat = 2,
};

template<Tilizer tilizer>
class DeviceTilizer
{
public:
    explicit DeviceTilizer(const std::vector<tt_dram_io_desc>& grids, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride_wrapper = Stride(), const std::uint32_t tile_height = 32, const std::uint32_t tile_width = 32, bool offline_tilize = false);
    
    DeviceTilizer(const std::vector<tt_dram_io_desc>& grids,
        VersimDramWrType spill_to_dram_func,
        VersimDramRdType read_from_dram_func, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride_wrapper = Stride(), const std::uint32_t tile_height = 32, const std::uint32_t tile_width = 32);
    
    ~DeviceTilizer() {
        // Read after Write Hazards exist when using PCIe in posted mode. 
        // In case this tilizer is used again (caching disabled), we need to ensure that write pointers for all queues are correctly updated,
        // before calling sync_local_wptr_with_device on next invocation of the same tilizer. Insert a memory barrier to ensure that wptr
        //  updates were pushed.
        auto grid = queue_grids_[0];
        if(!(offline_tilize or grid.random_access)) {
            // Only need to ensure wptr flush for queues and runtime tilize
            if(target_device_memory) {
                // HostDRAM barrier to ensure wptr update flushed to device
                std::unordered_set<uint32_t> channels_to_sync = {};
                std::uint32_t device = 0;
                for(auto &s : queue_states_) {
                    // Sync on all channels assigned to queue
                    device = s.device;
                    channels_to_sync.insert(s.channel);
                }
                cluster -> memory_barrier(tt::MemBarType::host_device_dram, device, channels_to_sync);
            }
            else {
                // mfence to ensure that wptr update flushed to host queue
                tt_driver_atomics::mfence();
            }
        }
        // Stop thread pool
        push_threads.stop();
        if constexpr(tilizer == Tilizer::FastTilizeDevicePush) {
            // Delete host side buffers if they're still allocated (this will not happen for RAMs)
            if(intermed_buf != nullptr) {
                delete[] intermed_buf;
            }
            if(wptr_buf != nullptr) {
                delete[] wptr_buf;
            }
        }
    }
    void tilize_and_push_to_device(const std::vector<tt_PytorchTensorDesc>& tensors, const int timeout_in_seconds, const int random_access_ptr = -1);
    tt_PytorchTensorDesc shuffle_tensor(const tt_PytorchTensorDesc& tensor);
    template<class T> void pad_tensor(const tt_PytorchTensorDesc& tensor);
    void init_pad_idx(const tt_PytorchTensorDesc& tensor);
    void overwrite_wptr(std::uint32_t wptr_value);

    aligned_vector<std::uint32_t> shuffled_data;
    aligned_vector<std::uint16_t> shuffled_data_half;
private:
    tt_cluster* cluster;
    tt_cluster_description* ndesc;
    tt_runtime_workload* workload;
    bool non_mmio_chip = false;
    bool offline_tilize = false;
    chip_id_t target_device;
    static constexpr std::uint32_t tile_width = 16;
    static constexpr std::uint32_t tile_height = 16;

    static constexpr std::uint32_t input_quad_width = tile_width * 2;
    static constexpr std::uint32_t input_quad_height = tile_height * 2;
    static constexpr std::uint32_t quad_header_size = 16;
    static constexpr std::uint32_t header_size_scale = 16;
    static constexpr std::uint32_t queue_header_size = tt::io::io_queue_header_size_bytes;
    static constexpr std::uint32_t intermed_buf_size_bytes = 256 * 4 * 4 + 64;

    std::uint32_t quad_height = tile_height * 2;
    std::uint32_t quad_width = tile_width * 2;
    std::uint32_t quad_trailer_size = 16;    
    std::uint32_t tensor_batch_size_in_bytes = 0;
    tt::DataFormat host_data_format = tt::DataFormat::Invalid;
    
    std::vector<std::pair<int, int>> stride_start_offsets;

    enum class conversion : std::uint8_t {
        identity_16,
        identity_32,
        fp32_fp16b,
        fp32_bfp8b,
        fp16b_bfp8b,
        fp16_bfp8,
        fp32_fp16,
        fp16b_fp16,
        fp32_bfp8,
        fp16b_bfp8,
        fp16_fp16b,
        fp16_bfp8b,
        fp16b_fp32,
        fp16_fp32,
        raw32_raw32,
        raw16_raw16,
        raw8_raw8,
        int8_int8,
        int8_bfp8,
    };

    template <class T>
    struct subdimensions
    {
        T tile_x;
        T tile_y;
        T sub_x;
        T sub_y;
        T z;
        T w;
    };

    struct block_state
    {
        std::uint32_t tile_x;
        std::uint32_t tile_y;
        std::uint32_t sub_x;
        std::uint32_t sub_y;
        std::uint32_t z;
    };

    // All queues in a grid share some properties. These are constants over a tilize and push operation.
    struct queue_grid
    {
        subdimensions<std::uint32_t> limit; // Limit of quads to transfer, relative to queue's offset.

        subdimensions<std::int32_t> strides;   // End-of-quad to start-of-next-quad strides in bytes.

        // these offsets are used when raw data is transferred in mega row format
        std::uint32_t w_offset; // size of a single tensor (<z, y, x>) in bytes
        std::uint32_t z_offset; // size of a single tensor slice (along z dim) in bytes
        std::uint32_t mblock_offset_x; // horizontal size of a ublock in bytes
        std::uint32_t ublock_offset_y; // horizontal size of several ublocks across mblocks and cores
        std::uint32_t mblock_offset_y; // size of all mblocks in a row distributed across cores

        std::uint32_t queue_size;       // queue capacity in blocks
        std::uint32_t bufq_c;
        std::uint32_t bufq_r;
        std::uint32_t block_size;       // Size of an output block (face) in bytes.
        std::uint32_t quad_size;        // Size of an output quad in bytes.
        std::uint32_t dram_wr_address_update;
        std::uint32_t quad_data_size;  // Size of the actual data in the quad (discounting header + trailer)
        std::uint32_t quad_num_elements;  // Number of elements of the actual data in the quad (discounting header + trailer)
        std::uint32_t quad_data_in_size;  // Size of the actual data in the quad for (discounting header + trailer)
        
        std::uint32_t tensor_row_pitch; // Start-of-row to start-of-row input stride in bytes.
        std::uint32_t tensor_face_pitch;
        bool random_access;             // random access without flow control
    
        tt::DataFormat input_tensor_format = tt::DataFormat::Invalid; // Used to keep track of the dataformat being sent to this grid, in case it changes
        // End index of grid's queues in the queue_state vector.
        // Start is implied by the previous queue_grid in the queue_grid vector.
        std::uint32_t max_queue_index;
        Dim ublock_order;
        IO_LAYOUT layout = IO_LAYOUT::Invalid;
        bool write_header = true;
        conversion data_conversion;
    };

    struct queue_state
    {
        std::byte* queue_address;
        std::byte* write_address;
        const std::byte* data_in;       // Pytorch Tensor data pointer adjusted for this queue's input offset.
        const std::byte* data_start;
        
        std::uint32_t queue_wptr;       // last-known wptr, must be [0,queue_size), rptr=wptr => empty
        std::uint32_t queue_rptr;       // last-known rptr, must be [0,queue_size), rptr=wptr => empty

        std::uint32_t next_w;           // next 3d tensor to transfer
        std::uint32_t channel;
        std::uint32_t device;
        std::uint32_t y_idx;

        bool exponents_preloaded = false;
#ifdef TILIZE_RPTR_SIM
        std::uint8_t rptr_poll_counter = 0;
#endif

        std::byte* queue_base_address(bool offline_tilize = false) const { return queue_address + (!offline_tilize) * queue_header_size; }
        std::uint32_t queue_read_rptr(VersimDramRdType read_from_dram_func) const;
        void queue_data_before_wptr_fence() const;
        void flush_wptr_write() const;
        void update_wptr_local(const queue_grid& grid, bool offline_tilize = false);
        void update_wptr_local_by_offset(const queue_grid& grid, std::uint32_t offset, bool offline_tilize = false);
        void update_wptr_device(const queue_grid& grid, VersimDramWrType spill_to_dram_func, uint8_t* intermed_buf, bool offline_tilize = false);
        void update_wptr(const queue_grid& grid, VersimDramWrType spill_to_dram_func, uint8_t* intermed_buf, bool offline_tilize = false);
        template<Layout layout> TT_HIDDEN bool advance_quad(const queue_grid& grid, block_state* next, Dim ublock_order, tt::DataFormat output_format, std::uint32_t quad_height);
        TT_HIDDEN bool queue_has_space(const queue_grid& grid, VersimDramRdType read_from_dram_func);
        bool queue_has_space_no_poll(const queue_grid& grid) const;
        std::uint32_t sync_local_wptr_with_device(VersimDramRdType read_from_dram_func);
        std::uint32_t sync_local_rptr_with_device(VersimDramRdType read_from_dram_func);
    };

    TT_HIDDEN static std::size_t format_size(tt::DataFormat f);
    TT_HIDDEN std::size_t get_shared_exponent_size(tt::DataFormat f);
    TT_HIDDEN uint32_t set_num_tilize_threads(const std::array<std::uint32_t, 4>& tensor_shape,  const tt_queue_info& queue_info, tt::DataFormat tensor_format);
    TT_HIDDEN void init_from_grids(const std::array<std::uint32_t, 4>& tensor_shape, tt::DataFormat tensor_format);
    TT_HIDDEN void init_from_tensors(const std::vector<tt_PytorchTensorDesc>& tensors, const int random_access_ptr);
    TT_HIDDEN void initialize_conv_shuffler(const tt::tt_PytorchTensorDesc& tensor);
    TT_HIDDEN void init_shuffle_idx(const tt::tt_PytorchTensorDesc& tensor);
    TT_HIDDEN void init_intermed_buf();
    TT_HIDDEN void delete_intermed_buf();
    TT_HIDDEN void preload_zeros_to_dram(queue_state& s, std::vector<uint8_t>& zeros, uint32_t queue_size_in_bytes);
    TT_HIDDEN void preload_quad_headers(queue_state& s, uint32_t quad_size, uint32_t num_slots);
    TT_HIDDEN void load_quad_headers_to_intermed_buf(uint32_t quad_size, uint32_t num_quads_in_block);
    TT_HIDDEN void preload_unary_exponents(queue_state& s, uint32_t quad_size, uint32_t num_slots, tt::DataFormat target_format);
    template<Layout layout> TT_HIDDEN void do_tilize_and_push();
    template<Layout layout> TT_HIDDEN void do_tilize_and_push_with_timeout(const int timeout_in_seconds);
    template<Layout layout> TT_HIDDEN void push_block(const queue_grid& grid, queue_state& queue, uint32_t thread_id);
    template <class Conversion> TT_HIDDEN void init_data_copy_function(IO_LAYOUT layout, bool conversion_from_b_to_a_format = false);
    std::function<void(const void*, void*, uint_fast32_t, vector<bool>&, int, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*)> copy_quad;
    bool queue_done(const queue_grid& grid, const queue_state& queue) const { return grid.limit.w == queue.next_w; }

    std::vector<tt_dram_io_desc> grids_;
    std::vector<queue_grid> queue_grids_;
    std::vector<queue_state> queue_states_;

    VersimDramWrType spill_to_dram_func = nullptr;
    VersimDramRdType read_from_dram_func = nullptr;
    uint8_t* intermed_buf = nullptr;
    uint8_t* wptr_buf = nullptr;
    // The following variables are used to shuffle data for Convolutions
    int num_rows_with_data = -1;
    int num_rows = -1;
    int input_face_shape = -1;
    int shuffled_row_size = -1;
    int block_depth = 0;
    int block_width = 0;
    int block_height = 0;
    int stride = -1;
    uint32_t window_x = 0;
    uint32_t window_y = 0;
    bool can_use_fast_shuffler = false;
    bool need_scalar_fallback = false;
    bool target_device_memory = false;
    bool pad_with_trailer = true;
    bool use_data_copy_for_transfer = false;
    std::uint32_t num_threads = 0; // number of threads that will be spawned for tilecopy
    std::uint32_t num_padding_threads = 0;
    TT_ThreadPool push_threads;
    uint32_t init_microbatch_size = 0;
    std::vector<uint32_t> conv_output_idx = {};
    std::unordered_map<uint32_t, uint32_t> conv_scalar_output_idx = {};
    uint32_t aligned_x;
    uint32_t aligned_y;
    uint32_t copy_granularity = 0;
    bool flat_formats_match = false;
    uint32_t transfer_size = 0;
    uint32_t adjusted_tile_height = 0;
    uint32_t num_faces_y = 0;
    uint32_t num_faces_x = 0;
    aligned_vector<std::uint32_t> padded_vector;
    aligned_vector<std::uint16_t> padded_vector_half;
    tt_PytorchTensorDesc padded_tensor;
    bool tensor_padded = false;
    std::vector<std::uint32_t> pad_output_idx;
    std::vector<uint32_t> input_idx = {};
    std::vector<uint32_t> conv_scalar_input_idx = {};

    const bool truncate_bfp_mantissa = std::getenv("TT_BACKEND_DISABLE_BFP_RTE") ?
                            atoi(std::getenv("TT_BACKEND_DISABLE_BFP_RTE")) : false; // Use round to nearest even approach in tilecopy conversion functions
    bool write_combine_weights = false;
    const bool disable_write_combine = std::getenv("TT_BACKEND_DISABLE_TILIZE_WC") ? 
                            atoi(std::getenv("TT_BACKEND_DISABLE_TILIZE_WC")) : false; // Flag to disable write combining
};

struct DataFormatPairHash {
    size_t operator()(const std::pair<DataFormat, DataFormat>& p) const {
        uint32_t combined = static_cast<uint32_t>(p.first)<<16 | static_cast<uint32_t>(p.second);
        return std::hash<uint32_t>{}(combined);
    }
};
const std::unordered_set<std::pair<DataFormat, DataFormat>, DataFormatPairHash> hw_tilizer_conversions = {
        {DataFormat::Float32, DataFormat::Float16_b},
        {DataFormat::Float32, DataFormat::Float16},
        {DataFormat::Float32, DataFormat::Bfp8_b},
        {DataFormat::Float32, DataFormat::Bfp8},
        {DataFormat::Float32, DataFormat::Float32},
        {DataFormat::Float16_b, DataFormat::Float16_b},
        {DataFormat::Float16_b, DataFormat::Bfp8_b},
        {DataFormat::Float16_b, DataFormat::Bfp8},
        {DataFormat::Float16_b, DataFormat::Float16},
        {DataFormat::Float16_b, DataFormat::Float32},
        {DataFormat::Float16, DataFormat::Float16},
        {DataFormat::Float16, DataFormat::Bfp8},
        {DataFormat::Float16, DataFormat::Float16_b},
        {DataFormat::Float16, DataFormat::Bfp8_b},
        {DataFormat::Float16, DataFormat::Float32},
        {DataFormat::RawUInt32, DataFormat::RawUInt32},
        {DataFormat::RawUInt16, DataFormat::RawUInt16},
        {DataFormat::RawUInt8, DataFormat::RawUInt8},
        {DataFormat::Int8, DataFormat::Int8},
        {DataFormat::Int8, DataFormat::Bfp8},
        {DataFormat::Int8, DataFormat::Bfp8_b},
    };
Tilizer get_tilizer_based_on_io_config(const tt_dram_io_desc &io, DataFormat host_data_format, DataFormat device_data_format, TargetDevice backend_type);