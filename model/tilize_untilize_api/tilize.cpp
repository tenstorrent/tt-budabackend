// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tilize.h"

#ifdef TILIZE_PERF
#include <iostream>
#endif

#include <chrono>
#include <cstring>
#include <cmath>
#ifndef __ARM_ARCH
#include <immintrin.h>
#include "tilecopy.h"
#endif

#include "element_types.h"
#include "alignment.h"
#include "common/error_types.hpp"
#include "runtime/runtime_io.hpp"
#include "device/cpuset_lib.hpp"

#define NO_PTR_ALIAS __restrict__
extern perf::tt_backend_perf backend_profiler;

#ifndef __ARM_ARCH
#define PREFETCH(addr) _mm_prefetch((addr), _MM_HINT_T0)
// Ignore ignored attributes warning related to using __m256i types in the std::function arguments
// Warning raised due to alignment attributes  (implictly defined in the intrinsic type) not respected by the template
// We can safely ignore this warning, since we explictly handle alignment when populating this function
#pragma GCC diagnostic ignored "-Wignored-attributes"
 // these are either unaligned or aligned loads, depending on input tensor alignment
std::function<__m256i(const __m256i*)> simd_256_load;
std::function<__m128i(__m128i*)> simd_128_load; 
#endif

template<Tilizer tilizer_backend>
DeviceTilizer<tilizer_backend>::DeviceTilizer(const std::vector<tt_dram_io_desc>& grids, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride_wrapper, const std::uint32_t tile_height, const std::uint32_t tile_width, bool offline_tilize)
: grids_(grids), host_data_format(tensor.format), stride(stride_wrapper.stride), stride_start_offsets(stride_wrapper.xy_offsets), quad_height(tile_height), quad_width(tile_width), offline_tilize(offline_tilize)
{
    #ifdef __ARM_ARCH
    log_assert(grids[0].layout == IO_LAYOUT::Flat && grids_[0].bufq_target_format == host_data_format, "Host tilize is not supported for non x86 hosts.");
    #endif
    non_mmio_chip = false;
    workload = tt::io::get_workload(grids[0].netlist_path);
    cluster = tt::io::get_cluster(grids[0].netlist_path, grids[0].backend_type);
    if(!offline_tilize) {
        ndesc = cluster -> get_cluster_desc();
        non_mmio_chip = !(ndesc -> is_chip_mmio_capable(workload -> queues.at(grids[0].queue_name).my_queue_info.target_device));
    }
    initialize_conv_shuffler(tensor);
    init_from_grids(tensor.shape, tensor.format);
}

template<Tilizer tilizer_backend>
DeviceTilizer<tilizer_backend>::DeviceTilizer(const std::vector<tt_dram_io_desc>& grids,
    VersimDramWrType spill_to_dram_func,
    VersimDramRdType read_from_dram_func, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride_wrapper, const std::uint32_t tile_height, const uint32_t tile_width)
: grids_(grids), spill_to_dram_func(spill_to_dram_func), read_from_dram_func(read_from_dram_func), host_data_format(tensor.format), stride(stride_wrapper.stride), stride_start_offsets(stride_wrapper.xy_offsets), quad_height(tile_height), quad_width(tile_width)
{
    #ifdef __ARM_ARCH
    log_assert(grids[0].layout == IO_LAYOUT::Flat && grids_[0].bufq_target_format == host_data_format, "Host tilize is not supported for non x86 hosts.");
    #endif
    non_mmio_chip = false;
    workload  = tt::io::get_workload(grids[0].netlist_path);
    cluster = tt::io::get_cluster(grids[0].netlist_path, grids[0].backend_type);
    if(!offline_tilize) {
        ndesc = cluster -> get_cluster_desc();
        non_mmio_chip = !(ndesc -> is_chip_mmio_capable(workload -> queues.at(grids[0].queue_name).my_queue_info.target_device));
    }

    initialize_conv_shuffler(tensor);
    init_from_grids(tensor.shape, tensor.format);
}

template<Tilizer tilizer_backend>
inline std::uint32_t DeviceTilizer<tilizer_backend>::queue_state::queue_read_rptr(VersimDramRdType read_from_dram_func) const
{
    if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
        uint32_t read_ptr = read_from_dram_func(reinterpret_cast<uint64_t>(queue_address), channel, device);
        return read_ptr;
    } else {
        return reinterpret_cast<const std::uint32_t*>(queue_address)[0];
    }
}

// Both the queue and the queue's wptr are in WC memory. Ensure that the queue data is visible before the wptr is updated.
template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::queue_data_before_wptr_fence() const
{
    tt_driver_atomics::sfence();
}

// Ensure that the wptr write is sent in a timely manner, otherwise it might sit in a write combining buffer
// until the write combining buffer is closed for some unrelated reason.
template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::flush_wptr_write() const
{
    tt_driver_atomics::sfence();
}

template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::update_wptr_local(const queue_grid& grid, bool offline_tilize)
{
    if (grid.random_access) {
        write_address += grid.block_size;
        return;
    }

    if (++queue_wptr == 2*grid.queue_size) {
        queue_wptr = 0;
        write_address = queue_base_address(offline_tilize);
    } else if (queue_wptr == grid.queue_size) {
        write_address = queue_base_address(offline_tilize);
    } else {
        write_address += grid.block_size;
    }
}

template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::update_wptr_local_by_offset(const queue_grid& grid, std::uint32_t offset, bool offline_tilize)
{   
    if(grid.random_access){
        write_address += offset * grid.block_size;
        return;
    }
    write_address = queue_base_address(offline_tilize) + ((queue_wptr + offset) % grid.queue_size) * grid.block_size;
    queue_wptr = (queue_wptr + offset) % (2 * grid.queue_size);
}

template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::update_wptr_device(const queue_grid& grid, VersimDramWrType spill_to_dram_func, uint8_t* intermed_buf, bool offline_tilize)
{
    if (grid.random_access or offline_tilize) {
        return;
    }

    if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
        reinterpret_cast<std::uint32_t*>(intermed_buf)[0] = queue_wptr;
        spill_to_dram_func(reinterpret_cast<uint64_t>(queue_address) + 4, reinterpret_cast<uint8_t*>(intermed_buf), 4, channel, device);
    } else {
        queue_data_before_wptr_fence();
        reinterpret_cast<std::uint32_t*>(queue_address)[1] = queue_wptr;
        flush_wptr_write();
    }
}

// Warning: Provides backdoor access to overwrite the local and device wptr. Should only be used for testing.
template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::overwrite_wptr(std::uint32_t wptr_value) {
    for(auto& q : queue_states_) {
        q.queue_wptr = wptr_value;
        if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
            intermed_buf = new(std::align_val_t(16)) uint8_t [intermed_buf_size_bytes]; //force memory alignment to 16 bytes (same as device)
            reinterpret_cast<std::uint32_t*>(intermed_buf)[0] = wptr_value;
            spill_to_dram_func(reinterpret_cast<uint64_t>(q.queue_address) + 4, reinterpret_cast<uint8_t*>(intermed_buf), 4, q.channel, q.device);
            delete[] intermed_buf;
        }
        else {
            tt_driver_atomics::sfence();
            reinterpret_cast<std::uint32_t*>(q.queue_address)[1] = wptr_value;
            tt_driver_atomics::sfence();
        }
    }
}
template<Tilizer tilizer_backend>
inline void DeviceTilizer<tilizer_backend>::queue_state::update_wptr(const queue_grid& grid, VersimDramWrType spill_to_dram_func, uint8_t* intermed_buf, bool offline_tilize)
{
    update_wptr_local(grid, offline_tilize);
    update_wptr_device(grid, spill_to_dram_func, intermed_buf, offline_tilize);
}

template<Tilizer tilizer_backend>
inline bool DeviceTilizer<tilizer_backend>::queue_state::queue_has_space_no_poll(const queue_grid& grid) const
{
    if (queue_wptr >= queue_rptr) {
        return queue_wptr - queue_rptr != grid.queue_size;
    } else {
        return queue_rptr - queue_wptr != grid.queue_size;
    }
}

template<Tilizer tilizer_backend>
bool DeviceTilizer<tilizer_backend>::queue_state::queue_has_space(const queue_grid& grid, VersimDramRdType read_from_dram_func)
{
    if (grid.random_access) {
        return true; // unconditionally allow random access
    }

    #ifdef TILIZE_RPTR_SIM
    // Actual queue_has_space failures are very rare, say 2-3 in 12288.
    // But we need to update the rptr about 2700 times in 12288 or about 2 in 9.
    if (rptr_poll_counter % 9 == 4 || rptr_poll_counter % 9 == 8) {
        queue_rptr = queue_read_rptr(read_from_dram_func);
    }
    rptr_poll_counter++;
    return true;

    #else

    if (queue_has_space_no_poll(grid)) {
        return true;
    }

    queue_rptr = queue_read_rptr(read_from_dram_func);

    return queue_has_space_no_poll(grid);
#endif
}

template<Tilizer tilizer_backend>
template<Layout layout>
bool DeviceTilizer<tilizer_backend>::queue_state::advance_quad(const queue_grid& grid, block_state* next, Dim ublock_order, tt::DataFormat output_format, std::uint32_t quad_height)
{   
    if constexpr (layout == Layout::Flat) {
        data_in += grid.tensor_row_pitch;
        if(++next -> tile_y == grid.limit.tile_y * grid.limit.sub_y * quad_height) {
            next -> tile_y = 0;
            ++next -> z;
            data_in =  data_start + (next -> z) * grid.tensor_face_pitch;
            if(next -> z == grid.limit.z) {
                next -> z = 0;
                ++next_w;
                data_in = data_start + next_w * grid.w_offset;
                return false;
            }
        }
        return true;
    }

    if constexpr (layout == Layout::MegaRow) {
        // transfer raw data in mega row format
        if (++next->tile_x == grid.limit.tile_x) {
            next->tile_x = 0;
            if (++next->tile_y == grid.limit.tile_y) {
                    next->tile_y = 0;
                if (++next->sub_x == grid.limit.sub_x) {
                        next->sub_x = 0;
                
                    if (++next->sub_y == grid.limit.sub_y) {
                        next->sub_y = 0;
                        if (++next->z == grid.limit.z) {
                            next->z = 0;
                            ++next_w;
                            data_in = data_start + next_w * grid.w_offset;
                            return false;
                        }
                    }
                }
            }
        }

        uint32_t z_offset = (next -> z) * grid.z_offset;
        uint32_t mblock_x_offset = (next -> sub_x) * grid.mblock_offset_x;
        uint32_t ublock_x_offset = (next -> tile_x) * grid.quad_data_in_size;
        uint32_t ublock_y_offset = (next -> tile_y) *  grid.ublock_offset_y;
        uint32_t mblock_y_offset = (next -> sub_y) * grid.mblock_offset_y;

        data_in = data_start + z_offset + mblock_x_offset + ublock_x_offset + ublock_y_offset + mblock_y_offset;
        return true;
    }

    if constexpr (layout == Layout::Tilized) {
        data_in += grid.strides.tile_x;
        if (++next->tile_x == grid.limit.tile_x) {
            next->tile_x = 0;

            data_in += grid.strides.tile_y;
            if (++next->tile_y == grid.limit.tile_y) {
                next->tile_y = 0;

                if(ublock_order == Dim::R) {
                    data_in += grid.strides.sub_x;
                    if (++next->sub_x == grid.limit.sub_x) {
                        next->sub_x = 0;

                        data_in += grid.strides.sub_y;
                        if (++next->sub_y == grid.limit.sub_y) {
                            next->sub_y = 0;

                            data_in += grid.strides.z;
                            if (++next->z == grid.limit.z) {
                                next->z = 0;

                                data_in += grid.strides.w;
                                ++next_w;

                                // done with 3d volume for the block
                                return false;
                            }
                        }
                    }
                }
                else {
                    data_in += grid.strides.sub_y;
                    if(++next->sub_y == grid.limit.sub_y) {
                        next -> sub_y = 0;

                        data_in += grid.strides.sub_x;
                        if(++next -> sub_x == grid.limit.sub_x){
                            next -> sub_x = 0;

                            data_in += grid.strides.z;
                            if(++next->z == grid.limit.z) {
                                next -> z = 0;

                                data_in += grid.strides.w;
                                ++next_w;

                                return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
}
namespace
{

template <class T, class... Args>
std::array<T, sizeof...(Args)> make_array(Args... args)
{
    return { static_cast<T>(args)... };
}

} // anonymous namespace

template<Tilizer tilizer_backend>
std::size_t DeviceTilizer<tilizer_backend>::format_size(tt::DataFormat f)
{
    switch (f)
    {
        case tt::DataFormat::RawUInt32:
        case tt::DataFormat::Float32: return sizeof(float);

        case tt::DataFormat::RawUInt16:
        case tt::DataFormat::Float16:
        case tt::DataFormat::Float16_b: return sizeof(std::uint16_t);

        case tt::DataFormat::Bfp8_b:
        case tt::DataFormat::RawUInt8:
        case tt::DataFormat::Int8:
        case tt::DataFormat::UInt8:
        case tt::DataFormat::Bfp8: return sizeof(std::uint8_t);

        default: log_assert(false, "DataFormat not supported by tilize."); return 0;
    }
}

template<Tilizer tilizer_backend>
std::size_t DeviceTilizer<tilizer_backend>::get_shared_exponent_size(tt::DataFormat f) {
    if (f == tt::DataFormat::Bfp8 || f == tt::DataFormat::Bfp8_b) {
        // Pack exponents based on the tile height and width
        return tt::io::align_up(quad_height * quad_width / tile_width, 16); // One exp for each row in a single face
    } else {
        return 0;
    }
}
template<Tilizer tilizer_backend>
std::uint32_t DeviceTilizer<tilizer_backend>::queue_state::sync_local_wptr_with_device(VersimDramRdType read_from_dram_func){
    if(tilizer_backend == Tilizer::FastTilizeDevicePush){
        return read_from_dram_func(reinterpret_cast<uint64_t>(queue_address) + 4, channel, device);
    }
    else
        return reinterpret_cast<const std::uint32_t*>(queue_address)[1];
}

template<Tilizer tilizer_backend>
std::uint32_t DeviceTilizer<tilizer_backend>::queue_state::sync_local_rptr_with_device(VersimDramRdType read_from_dram_func){
    if(tilizer_backend == Tilizer::FastTilizeDevicePush){
        return read_from_dram_func(reinterpret_cast<uint64_t>(queue_address), channel, device);
    }
    else
        return reinterpret_cast<const std::uint32_t*>(queue_address)[0];
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::preload_quad_headers(queue_state& s, uint32_t quad_size, uint32_t num_slots) {
    #ifndef __ARM_ARCH
    __m128i header_data = _mm_set_epi16(0, 0, 0, 0, 0, 0, 0, quad_size / header_size_scale);

    for(auto i = 0; i < num_slots; i++) {
        if constexpr(tilizer_backend == Tilizer::FastTilizeMMIOPush) {
            _mm_store_si128(reinterpret_cast<__m128i*>(s.write_address + i * quad_size), header_data);
        }
        else if(tilizer_backend == Tilizer::FastTilizeDevicePush) {
            spill_to_dram_func(reinterpret_cast<uint64_t>(s.write_address + i * quad_size), reinterpret_cast<uint8_t*>(&header_data), 16, s.channel, s.device);
        }
    }
    #endif
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::load_quad_headers_to_intermed_buf(uint32_t quad_size, uint32_t num_quads_in_block) {
    #ifndef __ARM_ARCH
    // Load quad headers when write combining
    __m128i header_data = _mm_set_epi16(0, 0, 0, 0, 0, 0, 0, quad_size / header_size_scale);
    for(int i = 0; i < num_quads_in_block; i++) {
        _mm_store_si128(reinterpret_cast<__m128i*>(reinterpret_cast<std::byte*>(intermed_buf) + i * queue_grids_[0].quad_size), header_data);
    }
    #endif
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::preload_unary_exponents(queue_state& s, uint32_t quad_size, uint32_t num_slots, tt::DataFormat target_format) {
    #ifndef __ARM_ARCH
    // Preload exponents here for Int8 -> Bfp8/Bfp8_b tilization.
    log_assert(target_format == tt::DataFormat::Bfp8 || target_format == tt::DataFormat::Bfp8_b, "Exponents should only be preinitialized for Bfp8/Bfp8_b target data.");
    
    __m256i unary_exponents;
    // 2^6 exponent is represented as 21 in Bfp8 and 133 in Bfp8_b
    if(target_format == tt::DataFormat::Bfp8) {
        unary_exponents = _mm256_set1_epi8(21);
    }
    else {
        unary_exponents = _mm256_set1_epi8(133);
    } 
    // Preload 64 bytes of constant exponent data starting from queue_address + header_offset
    for(int i = 0; i < num_slots; i++) {
        if constexpr(tilizer_backend == Tilizer::FastTilizeMMIOPush) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(s.write_address + i * quad_size + 16), unary_exponents);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(s.write_address + i * quad_size + 48), unary_exponents);
        }
        else if(tilizer_backend == Tilizer::FastTilizeDevicePush) {
            if(write_combine_weights) {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(intermed_buf + i * quad_size + 16), unary_exponents);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(intermed_buf + i * quad_size + 48), unary_exponents);
            }
            else {
                spill_to_dram_func(reinterpret_cast<uint64_t>(s.write_address + i * quad_size + 16), reinterpret_cast<uint8_t*>(&unary_exponents), 32, s.channel, s.device);
                spill_to_dram_func(reinterpret_cast<uint64_t>(s.write_address + i * quad_size + 48), reinterpret_cast<uint8_t*>(&unary_exponents), 32, s.channel, s.device);
            }
        }
    }
    s.exponents_preloaded = true;
    #endif
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::preload_zeros_to_dram(queue_state& s, std::vector<uint8_t>& zeros, uint32_t queue_size_in_bytes) {
    #ifndef __ARM_ARCH
    if(tilizer_backend == Tilizer::FastTilizeMMIOPush){
        for(auto i = s.write_address; i < s.write_address + queue_size_in_bytes; i += 32){
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(i), _mm256_setzero_si256());
        }
    }
    else{
        spill_to_dram_func(reinterpret_cast<uint64_t>(s.write_address), reinterpret_cast<uint8_t*>(zeros.data()), queue_size_in_bytes, s.channel, s.device);
    }
    #endif
}

template<Tilizer tilizer_backend>
uint32_t DeviceTilizer<tilizer_backend>::set_num_tilize_threads(const std::array<std::uint32_t, 4>& tensor_shape, const tt_queue_info& queue_info, tt::DataFormat tensor_format) {
    uint32_t num_threads_local = 1;
    if(offline_tilize or parse_env("TT_BACKEND_DISABLE_MULTI_THREADED_PUSH", false)) {
        num_threads_local = 1;
    }
    else {
        std::uint32_t min_num_tiles_for_multi_threading = 32; // Empirically Determined: Giving each thread ~32 tiles ensures multithreading gives an adequate boost

        if(tensor_format == tt::DataFormat::Int8 && queue_info.loc == QUEUE_LOCATION::DRAM) {
            min_num_tiles_for_multi_threading = 512; // More data than usual needs to be consumed by each thread for Multithreading to work well with Int8
        }
        
        // Max of 16 threads. If CPU allocator is not enabled, give tilizer access to half the number of system threads. Otherwise, use up to number of cores allocated for device, or fair share of cpus visible by container.
        int allowed_threads = sysconf(_SC_NPROCESSORS_ONLN) / 2;
        if(parse_env("TT_BACKEND_CPUSET_ALLOCATOR_ENABLE", false)) {
            allowed_threads =  tt::cpuset::tt_cpuset_allocator::get_num_cpu_cores_allocated_to_device(queue_info.target_device);
        }

        int max_threads = std::min(16, allowed_threads);

        // Global override, mostly for debug purposes.
        const char* max_threads_env = std::getenv("TT_BACKEND_MULTI_THREADED_PUSH_MAX_THREADS");
        
        if (max_threads_env){
            max_threads = atoi(max_threads_env);
        }

        tt::cpuset::tt_cpuset_allocator::unbind_thread_from_cpuset();
        // Compute number of threads used for shuffling and tilize
        num_threads_local = std::min(static_cast<int>(init_microbatch_size), max_threads);
        
        if(!parse_env<bool>("TT_BACKEND_FORCE_MULTI_THREADED_PUSH", false)) { // Set for debug: Use multithreading regardless of the tensor size
            // Don't use multithreading if the microbatch size is too small, or if each thread will not be fed an adequate amount of data (in this case the cost of initialzing threads is more than actually doing the work)
            if(init_microbatch_size < max_threads || (tensor_shape[3] * tensor_shape[2] * tensor_shape[1] * init_microbatch_size / num_threads_local) < (min_num_tiles_for_multi_threading * 1024)) {
                num_threads_local = 1;
            }
        }
    }
    return num_threads_local;
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::init_from_grids(const std::array<std::uint32_t, 4>& tensor_shape, tt::DataFormat tensor_format)
{
    queue_grids_.clear();
    queue_states_.clear();
    unsigned int output_queue_max_state_index = 0;

    init_microbatch_size = tensor_shape[0];
    
    if(stride == -1) {
        num_rows_with_data = quad_height;
    }
    log_assert(quad_width && !(quad_width % tile_width), "Tilizer only supports face aligned fractional tiles in the x-dimension.");
    
    for (const tt_dram_io_desc &g : grids_) {
        queue_grid og;
        tt_queue_info queue_info = (workload->queues).at(g.queue_name).my_queue_info;
        target_device = queue_info.target_device;
        if(tilizer_backend == Tilizer::FastTilizeDevicePush) {
            write_combine_weights = !disable_write_combine; // Write combine flag for FastTilizeDevicePush
        }
        target_device_memory |= (queue_info.loc == QUEUE_LOCATION::DRAM);
        num_threads = set_num_tilize_threads(tensor_shape, queue_info, tensor_format);
        if(num_threads > 1) {
            push_threads.start(num_threads, ndesc, target_device); //Start thread pool upon tilizer creation. This thread pool is shared by shuffler and tilizer. If bufq target format is not Bfp8*, multithreading will only be used for shuffling.
        }
        if(tilizer_backend == Tilizer::FastTilizeMMIOPush){
            log_assert(g.bufq_mapping.size() == g.bufq_start_addr_channel.size(), "Mapping and start address vectors are not the same size");
            log_assert(g.bufq_mapping.size() == g.bufq_grid_dim_c * g.bufq_grid_dim_r, "Mapping vector size must match grid dimensions.");
        }
        og.layout = g.layout;
        og.write_header = g.layout != IO_LAYOUT::Flat;
        og.quad_num_elements = quad_width * quad_height;
        og.quad_data_size = og.quad_num_elements * format_size(g.bufq_target_format) + get_shared_exponent_size(g.bufq_target_format);
        og.quad_size = og.quad_data_size + quad_header_size;

        uint32_t aligned_quad_size = tt::io::align_up(og.quad_size, tt::io::tile_alignment_bytes);
        quad_trailer_size = aligned_quad_size - og.quad_size;
        og.quad_size += quad_trailer_size; // Only add trailer for padding if the quads are not 32 byte aligned
        og.dram_wr_address_update = og.quad_size;
        
        if(!og.write_header) {
            log_assert(!(g.bufq_target_format == tt::DataFormat::Bfp8 or g.bufq_target_format == tt::DataFormat::Bfp8_b), "Bfp8/Bfp8_b flat tensor not supported!");
            og.dram_wr_address_update = quad_width * format_size(g.bufq_target_format) * g.ublock_ct * g.mblock_n;
            og.quad_size = og.quad_data_size;
        }
        use_data_copy_for_transfer = (g.layout == IO_LAYOUT::Flat and g.bufq_target_format == host_data_format and g.bufq_grid_dim_c == 1 and g.bufq_grid_dim_r == 1);
        og.quad_data_in_size = quad_height * quad_width; // Assume same no format for now
        og.queue_size = g.bufq_num_slots;
        og.ublock_order = workload->get_ublock_scan(g.queue_name);

        unsigned int queue_count = g.bufq_grid_dim_c * g.bufq_grid_dim_r;

        output_queue_max_state_index += queue_count;
        og.max_queue_index = output_queue_max_state_index;

        queue_grids_.push_back(og);
        uint32_t num_quads_in_queue = g.mblock_m * g.mblock_n * g.ublock_ct * g.ublock_rt * g.t * og.queue_size;
        uint32_t queue_size_in_bytes = og.quad_size * num_quads_in_queue;
        vector<uint8_t> zeros;
        if(tilizer_backend == Tilizer::FastTilizeDevicePush and stride > 0) zeros = std::vector<uint8_t>(queue_size_in_bytes, 0);

        for (unsigned int queue_index = 0; queue_index < queue_count; queue_index++){
            queue_state s;
            auto &alloc = queue_info.alloc_info[queue_index];
            if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
                s.queue_address = reinterpret_cast<std::byte*>(g.bufq_start_addr_channel[queue_index].first);
            } else {
                s.queue_address = static_cast<std::byte*>(g.bufq_mapping[queue_index]);
            }

            s.channel = alloc.channel;
            s.device = queue_info.target_device;

            s.queue_wptr = s.sync_local_wptr_with_device(read_from_dram_func);
            s.queue_rptr = s.sync_local_rptr_with_device(read_from_dram_func);
            s.next_w = 0;
            s.write_address = s.queue_base_address(offline_tilize);
            if(stride > 0){
                preload_zeros_to_dram(s, zeros, queue_size_in_bytes);
            }
            if(og.write_header and !write_combine_weights) {
                // When write combining, quad headers are preloaded to the intermediate buffer instead of DRAM (see below) 
                preload_quad_headers(s, og.quad_size, num_quads_in_queue);
            }
            if(((host_data_format == tt::DataFormat::Int8 && g.bufq_target_format == tt::DataFormat::Bfp8) || (host_data_format == tt::DataFormat::Int8 && g.bufq_target_format == tt::DataFormat::Bfp8_b)) and !write_combine_weights) {
                // Load unary exponents once intermediate buffer is initialized downstream when write combining
                preload_unary_exponents(s, og.quad_size, num_quads_in_queue, g.bufq_target_format);
            }
            s.y_idx = queue_index / g.bufq_grid_dim_c;
            queue_states_.push_back(s);
        }
    }
    adjusted_tile_height = std::min(quad_height, tile_height);
    num_faces_y = ceil(float(quad_height) / tile_height);
    num_faces_x = ceil(float(quad_width) / tile_width);
    transfer_size = queue_grids_[0].dram_wr_address_update - queue_grids_[0].write_header * (quad_header_size + quad_trailer_size);
}

template<Tilizer tilizer_backend>
template<class Conversion>
void DeviceTilizer<tilizer_backend>::init_data_copy_function(IO_LAYOUT layout, bool conversion_from_b_to_a_format) {
    if(layout == IO_LAYOUT::Flat) {
        if(host_data_format == grids_[0].bufq_target_format) {
            if constexpr(tilizer_backend == Tilizer::FastTilizeMMIOPush) {
                copy_quad = [&] (const void* in, void* out, uint_fast32_t copy_size_bytes, vector<bool>& dummy_0, int dummy_1, uint32_t dummy_2, uint32_t dummy_3, uint32_t dummy_4, uint32_t dummy_5, uint8_t* dummy_6) 
                                {std::memcpy(reinterpret_cast<std::byte*>(out), reinterpret_cast<const std::byte*>(in), copy_size_bytes);};   
            }
            else {
                if(write_combine_weights) {
                    copy_quad = [&] (const void* in, void* out, uint_fast32_t copy_size_bytes, vector<bool>& dummy_0, int dummy_1, uint32_t dummy_2, uint32_t dummy_3, uint32_t dummy_4, uint32_t dummy_5, uint8_t* dummy_6) 
                                {std::memcpy(reinterpret_cast<std::byte*>(out), reinterpret_cast<const std::byte*>(in), copy_size_bytes);};   
                }
                else {
                    copy_quad = [&] (const void* in, void* out, uint_fast32_t copy_size_bytes, vector<bool>& dummy_0, int dummy_1, uint32_t dummy_2, uint32_t dummy_3, uint32_t dummy_4, uint32_t dummy_5, uint8_t* dummy_6) {};
                }
            }
        }
        #ifndef __ARM_ARCH
        else {
            copy_quad = [&](const void* in, void* out, uint_fast32_t copy_size_bytes, vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host)
                        {return row_copy<32, Conversion>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), copy_size_bytes);};
        }
        #endif
    }
    #ifndef __ARM_ARCH
    else {
        if(!(std::is_same<Conversion, RawUInt32_RawUInt32>::value || std::is_same<Conversion, RawUInt16_RawUInt16>::value || std::is_same<Conversion, RawUInt8_RawUInt8>::value)){ 
            if(stride > 0){
                if(conversion_from_b_to_a_format) {
                    // Invoke tilecopy functions with selective tilization, i.e.: do not tilize face sections which are just zeros (padding). Zeros were already preloaded to these addresses in init_from_grids
                    copy_quad = [&](const void* in, void* out, uint_fast32_t tensor_row_pitch,vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host) 
                                    {return tensor_to_quad_tiles<tile_width, tile_height, Conversion, true>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), tensor_row_pitch, true, fill_data_for_faces, num_rows_with_data, datum_col_offset_for_quad, quad_height, num_faces_y, num_faces_x, exp_host, truncate_bfp_mantissa);};
                }
                else {
                    // Invoke tilecopy functions with selective tilization, i.e.: do not tilize face sections which are just zeros (padding). Zeros were already preloaded to these addresses in init_from_grids
                    copy_quad = [&](const void* in, void* out, uint_fast32_t tensor_row_pitch,vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host) 
                                    {return tensor_to_quad_tiles<tile_width, tile_height, Conversion, true>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), tensor_row_pitch, false, fill_data_for_faces, num_rows_with_data, datum_col_offset_for_quad, quad_height, num_faces_y, num_faces_x, exp_host, truncate_bfp_mantissa);};
                }
            }
            else{ 
                // Invoke regular tilecopy functions, which tilize all sections in a face
                if(conversion_from_b_to_a_format) {
                    copy_quad = [&](const void* in, void* out, uint_fast32_t tensor_row_pitch, vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host) 
                                {return tensor_to_quad_tiles<tile_width, tile_height, Conversion, false>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), tensor_row_pitch, true, fill_data_for_faces, num_rows_with_data, datum_col_offset_for_quad, quad_height, num_faces_y, num_faces_x, exp_host, truncate_bfp_mantissa);};
                }
                else {
                    copy_quad = [&](const void* in, void* out, uint_fast32_t tensor_row_pitch, vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host) 
                                {return tensor_to_quad_tiles<tile_width, tile_height, Conversion, false>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), tensor_row_pitch, false, fill_data_for_faces, num_rows_with_data, datum_col_offset_for_quad, quad_height, num_faces_y, num_faces_x, exp_host, truncate_bfp_mantissa);};
                }
            }
        }
        else{
            // Invoke tilecopy functions for mega rows
            copy_quad = [&](const void* in, void* out, uint_fast32_t tensor_row_pitch, vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host) 
                            {return tensor_to_megarow_quad<32, 32, Conversion>(reinterpret_cast<const typename Conversion::in_type*>(in), reinterpret_cast<typename Conversion::out_type*>(out), tensor_row_pitch);};
        }
    }
    #endif
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::init_intermed_buf() {
    // Initialize intermediate buffer used for FastTilizeDevicePush mode
    if constexpr(tilizer_backend == Tilizer::FastTilizeDevicePush) {
        log_assert(intermed_buf == nullptr and wptr_buf == nullptr, "init_intermed_buf should not be called when buffers are already allocated");
        if(write_combine_weights) {
            intermed_buf = new(std::align_val_t(16)) uint8_t [queue_grids_[0].block_size];
            uint32_t num_quads_in_block = queue_grids_[0].block_size / queue_grids_[0].quad_size;
            // Preload quad headers to the intermediate buffer
            if(queue_grids_[0].write_header) {
                load_quad_headers_to_intermed_buf(queue_grids_[0].quad_size, num_quads_in_block);
            }
            if((host_data_format == tt::DataFormat::Int8 && grids_[0].bufq_target_format == tt::DataFormat::Bfp8) || (host_data_format == tt::DataFormat::Int8 && grids_[0].bufq_target_format == tt::DataFormat::Bfp8_b)) {
                preload_unary_exponents(queue_states_[0], queue_grids_[0].quad_size, num_quads_in_block, grids_[0].bufq_target_format);
            }
        }
        else {
            intermed_buf = new(std::align_val_t(16)) uint8_t [intermed_buf_size_bytes]; //force memory alignment to 16 bytes (same as device)
        }
        wptr_buf = new uint8_t[4]; // Allocate 4 bytes for wptr update buffer
    }
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::delete_intermed_buf() {
    // Delete intermediate buffers from host memory
    if(wptr_buf != nullptr) {
        delete[] wptr_buf;
        wptr_buf = nullptr;
    }
    if(intermed_buf != nullptr) {
        delete[] intermed_buf;
        intermed_buf = nullptr;
    }
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::init_from_tensors(const std::vector<tt_PytorchTensorDesc>& tensors, const int random_access_ptr)
{
    unsigned int state_index = 0;
    for (unsigned int tensor_index = 0; tensor_index < tensors.size(); tensor_index++) {
        const tt_PytorchTensorDesc &t = tensors[tensor_index];
        const tt_dram_io_desc &g = grids_[tensor_index];
        queue_grid &og = queue_grids_[tensor_index];
        
        auto data_ptr = static_cast<const std::byte*>(t.ptr);
        bool use_aligned_intrinsics_avx2 = !(reinterpret_cast<const std::uintptr_t>(t.ptr) % input_alignment_avx2);
        bool use_aligned_intrinsics_avx = !(reinterpret_cast<const std::uintptr_t>(t.ptr) % input_alignment_avx);
        
        #ifndef __ARM_ARCH
        if(use_aligned_intrinsics_avx2) {
            simd_256_load = ([](const __m256i* ptr){return _mm256_load_si256(ptr);});
        }
        else {
            simd_256_load = ([](const __m256i* ptr){return _mm256_loadu_si256(ptr);});
        }

        if(use_aligned_intrinsics_avx) {
            simd_128_load = ([](__m128i* ptr){return _mm_stream_load_si128(ptr);});
        }
        else {
            simd_128_load = ([](__m128i* ptr){return _mm_loadu_si128(ptr);});
        }
        #endif

        if(!(block_width && og.limit.w == t.shape[0] && og.input_tensor_format == t.format)) {
            // Initialize or reinitialize tensor parameters if not already set, or if the batch size/format changed
            // Assumption: the <x,y> shape cannot be changed, and is hence not checked
            og.input_tensor_format = t.format;
            // A "block" is the region of tensor that is assigned to each queue.
            // It must be a multiple of the quad size (32x32x1).
            // We require that the tensor divides evenly over the queue grid.
            log_assert(!(t.shape[3] % quad_width), "The input tensor cannot be evenly split into tiles across the col dimension! Number of columns must be a multiple of tile width (num_cols = {}, tile_width {}).", t.shape[3], quad_width);
            log_assert(!(t.shape[2] % quad_height), "The input tensor cannot be evenly split into tiles across the row dimension! Number of rows must be a multiple of tile height (num_rows = {}, tile height {}).", t.shape[2], quad_height);
            log_assert(!(t.shape[3] % g.bufq_grid_dim_c), "The input tensor cannot be evenly split across the column grid! num_cols = {}, grid_cols = {}", t.shape[3], g.bufq_grid_dim_c);
            log_assert(!(t.shape[2] % g.bufq_grid_dim_r), "The input tensor cannot be evenly split across the row grid! num_rows = {}, grid_rows = {}", t.shape[2], g.bufq_grid_dim_r);

            block_width = t.shape[3] / g.bufq_grid_dim_c;
            block_height = t.shape[2] / g.bufq_grid_dim_r;
            block_depth = t.shape[1];

            tensor_batch_size_in_bytes = t.shape[1] * t.shape[2] * t.shape[3] * format_size(t.format);
            unsigned int quads_per_block = (block_width / quad_width) * (block_height / quad_height) * block_depth;

            // Configure pointer for random access memory
            og.random_access = (g.io_type == tt::IO_TYPE::RandomAccess);

            bool valid_queue = (!og.random_access); //  and (random_access_ptr == -1);
            bool valid_ram = (og.random_access) and (random_access_ptr >= 0);
            log_assert(valid_queue or valid_ram, "Must be tilizing and pushing to either a valid queue or a valid ram (with random_access_ptr >=0)!", valid_queue, valid_ram);

            og.limit.tile_x = g.ublock_ct;
            og.limit.tile_y = g.ublock_rt;
            og.limit.sub_x = g.mblock_n;
            og.limit.sub_y = g.mblock_m;
            og.limit.z = t.shape[1];
            og.limit.w = t.shape[0];
            og.bufq_c = g.bufq_grid_dim_c;
            og.bufq_r = g.bufq_grid_dim_r;
            // Assert that the number of sub-blocks is consistent with the number of sub-blocks specified in the netlist
            log_assert(og.limit.sub_x == block_width / quad_width / g.ublock_ct, "Inferred number of sub-blocks in x ({}) does not match the number of sub-blocks in x specified in the grid ({})!", block_width / quad_width / g.ublock_ct, og.limit.sub_x);
            log_assert(og.limit.sub_y == block_height / quad_height / g.ublock_rt, "Inferred number of sub-blocks in y ({}) does not match the number of sub-blocks in y specified in the grid ({})!", block_height / quad_height / g.ublock_rt, og.limit.sub_y);

            // These are not proper strides, but rather the additional amount we need to add given that we've
            // already advanced the pointer along the previous dimension(s).
            og.strides.tile_x = t.itemsize * quad_width;
            og.strides.tile_y = quad_height * t.strides[2] - (og.strides.tile_x * og.limit.tile_x);
            if(og.ublock_order == Dim::R) {
                og.strides.sub_x = static_cast<std::int32_t>(og.strides.tile_x * og.limit.tile_x) - og.limit.tile_y * t.strides[2] * quad_height;
                og.strides.sub_y = og.limit.tile_y * quad_height * t.strides[2] - og.limit.sub_x * og.limit.tile_x * og.strides.tile_x;
                og.strides.z = t.strides[1] - quad_height * t.strides[2] * og.limit.tile_y * og.limit.sub_y;
            }
            else{
                og.strides.sub_y = 0;
                og.strides.sub_x = static_cast<std::int32_t>(og.strides.tile_x * og.limit.tile_x) - og.limit.tile_y * t.strides[2] * quad_height * og.limit.sub_y;
                og.strides.z = t.strides[1] - static_cast<std::int32_t>(og.strides.tile_x * og.limit.tile_x) * og.limit.sub_x;
            }
            
            og.strides.w = t.strides[0] - t.strides[1] * og.limit.z;

            og.tensor_row_pitch = t.strides[2];
            og.tensor_face_pitch = t.strides[1];
            og.block_size = block_width * block_height * block_depth * format_size(g.bufq_target_format);
            og.block_size += (get_shared_exponent_size(g.bufq_target_format)) * quads_per_block;
            
            if (og.write_header) {
                // Always add header and trailer unless it is flat layout, then we expect contiguous data, and no header/trailer needed
                og.block_size += (quad_trailer_size + quad_header_size) * quads_per_block;
            }

            if(tilizer_backend == Tilizer::FastTilizeDevicePush) {
                // Initialize intermediate data and wptr buffers
                if(!og.random_access) {
                    // Intermediate Buffer Memory Allocation Policy:
                    // - Tilizer speculates that queues will be pushed to multiple times (weak assumption) -> keep this buffer allocated until tilizer destruction
                    // - Tilizer speculates that rams will only be pushed to once (fairly strong assumption) -> don't need to keep these around after single push is done.
                    //   Allocate once in push function and immediately destroy after pushing. 

                    // If the push size changed during runtime and buffers need to be reallocated, delete the previous buffers to prevent memory leaks
                    delete_intermed_buf();
                    init_intermed_buf();
                }
            }
            // these offsets are used for transferring raw data in mega row format
            og.quad_data_in_size = quad_height * quad_width * t.itemsize; // Assume same no format for now
            og.w_offset = og.limit.z * og.limit.tile_x * og.limit.sub_x * og.limit.tile_y * og.limit.sub_y * og.bufq_c * og.bufq_r * og.quad_data_in_size;
            og.z_offset = og.limit.tile_x * og.limit.sub_x * og.limit.tile_y * og.limit.sub_y * og.bufq_c * og.bufq_r * og.quad_data_in_size;
            og.mblock_offset_x = og.limit.tile_x * og.quad_data_in_size;
            og.ublock_offset_y = og.limit.tile_x * og.limit.sub_x * og.bufq_c * og.quad_data_in_size;
            og.mblock_offset_y = og.limit.tile_x *og.limit.tile_y * og.limit.sub_x * og.bufq_c * og.quad_data_in_size;

            if ((t.format == tt::DataFormat::Float16_b  &&  g.bufq_target_format == tt::DataFormat::Float16_b) || 
                t.format == tt::DataFormat::Float16     &&  g.bufq_target_format == tt::DataFormat::Float16) {
                og.data_conversion = conversion::identity_16;
                init_data_copy_function<IdentityConverter<std::uint16_t>>(og.layout);
            } else if (t.format == tt::DataFormat::Float32 && g.bufq_target_format == tt::DataFormat::Float32) {
                og.data_conversion = conversion::identity_32;
                init_data_copy_function<Fp32_Fp32>(og.layout);
            } else if (t.format == tt::DataFormat::Float32 && g.bufq_target_format == tt::DataFormat::Float16_b) {
                og.data_conversion = conversion::fp32_fp16b;
                init_data_copy_function<Fp32_Fp16b>(og.layout);
            } else if (t.format == tt::DataFormat::Float32 && g.bufq_target_format == tt::DataFormat::Bfp8_b) {
                og.data_conversion = conversion::fp32_bfp8b;
                init_data_copy_function<Fp32_Bfp8_all>(og.layout);
            } else if (t.format == tt::DataFormat::Float16_b && g.bufq_target_format == tt::DataFormat::Bfp8_b) {
                og.data_conversion = conversion::fp16b_bfp8b;
                init_data_copy_function<Fp16b_Bfp8_all>(og.layout);
            } else if (t.format == tt::DataFormat::Float16 && g.bufq_target_format == tt::DataFormat::Bfp8) {
                og.data_conversion = conversion::fp16_bfp8;
                init_data_copy_function<Fp16_Bfp8>(og.layout);
            } else if (t.format == tt::DataFormat::Float32 && g.bufq_target_format == tt::DataFormat::Float16){
                og.data_conversion = conversion::fp32_fp16;
                init_data_copy_function<Fp32_Fp16>(og.layout);
            }else if (t.format == tt::DataFormat::Float16_b && g.bufq_target_format == tt::DataFormat::Float16){
                og.data_conversion = conversion::fp16b_fp16;
                init_data_copy_function<Fp16b_Fp16>(og.layout);
            } else if (t.format == tt::DataFormat::Float16_b && g.bufq_target_format == tt::DataFormat::Float32){
                og.data_conversion = conversion::fp16b_fp32;
                init_data_copy_function<Fp16b_Fp32>(og.layout);
            } else if (t.format == tt::DataFormat::Float16 && g.bufq_target_format == tt::DataFormat::Float32){
                og.data_conversion = conversion::fp16_fp32;
                init_data_copy_function<Fp16_Fp32>(og.layout);
            }  else if (t.format == tt::DataFormat::Float32 && g.bufq_target_format == tt::DataFormat::Bfp8){
                og.data_conversion = conversion::fp32_bfp8;
                init_data_copy_function<Fp32_Bfp8_all>(og.layout, true);
            } else if (t.format == tt::DataFormat::Float16_b && g.bufq_target_format == tt::DataFormat::Bfp8){
                og.data_conversion = conversion::fp16b_bfp8;
                init_data_copy_function<Fp16b_Bfp8_all>(og.layout, true);
            } else if (t.format == tt::DataFormat::Float16 && g.bufq_target_format == tt::DataFormat::Float16_b){
                og.data_conversion = conversion::fp16_fp16b;
                init_data_copy_function<Fp16_Fp16b>(og.layout);
            } else if (t.format == tt::DataFormat::Float16 && g.bufq_target_format == tt::DataFormat::Bfp8_b){
                og.data_conversion = conversion::fp16_bfp8b;
                init_data_copy_function<Fp16_Bfp8b>(og.layout);
            } else if (t.format == tt::DataFormat::RawUInt32 && g.bufq_target_format == tt::DataFormat::RawUInt32){
                og.data_conversion = conversion::raw32_raw32;
                init_data_copy_function<RawUInt32_RawUInt32>(og.layout);
            } else if (t.format == tt::DataFormat::RawUInt16 && g.bufq_target_format == tt::DataFormat::RawUInt16){
                og.data_conversion = conversion::raw16_raw16;
                init_data_copy_function<RawUInt16_RawUInt16>(og.layout);
            } else if (t.format == tt::DataFormat::RawUInt8 && g.bufq_target_format == tt::DataFormat::RawUInt8){
                og.data_conversion = conversion::raw8_raw8;
                init_data_copy_function<RawUInt8_RawUInt8>(og.layout);
            } else if(t.format == tt::DataFormat::Int8 && g.bufq_target_format == tt::DataFormat::Int8) {
                og.data_conversion = conversion::int8_int8;
                init_data_copy_function<Int8_Int8>(og.layout);
            } else if ((t.format == tt::DataFormat::Int8 && g.bufq_target_format == tt::DataFormat::Bfp8) || (t.format == tt::DataFormat::Int8 && g.bufq_target_format == tt::DataFormat::Bfp8_b)) {
                // The tilize function is identical for int8 -> both bfp8 formats
                og.data_conversion = conversion::int8_bfp8;
                init_data_copy_function<Int8_Bfp8>(og.layout);
            } else {
                log_assert(false, "Must use a supported data conversion for Fast Tilize!");
            }
            if(!(offline_tilize or parse_env("TT_BACKEND_DISABLE_MULTI_THREADED_PUSH", false))) {
                if(og.limit.w != init_microbatch_size) {
                    // If the microbatch size has changed on the fly, we may need to restart the thread pool
                    int allowed_threads = sysconf(_SC_NPROCESSORS_ONLN) / 2;
                    if(parse_env("TT_BACKEND_CPUSET_ALLOCATOR_ENABLE", false)) {
                        allowed_threads =  tt::cpuset::get_allowed_num_threads();
                    }
                    uint32_t num_threads_modified = set_num_tilize_threads(t.shape, (workload->queues).at(g.queue_name).my_queue_info, t.format);
                    if(num_threads_modified != num_threads) {
                        if(num_threads > 1) {
                            // Only wait for threads to complete if thread pool was actually started
                            while(push_threads.wait() || push_threads.num_free != num_threads) continue;
                            push_threads.stop();
                        }
                        
                        num_threads = num_threads_modified;
                        if(num_threads > 1) {
                            push_threads.start(num_threads, ndesc, target_device);
                        }
                    }
                    init_microbatch_size = og.limit.w;
                }
                // Limit the number of threads for tilizing to device under certain conditions
                if (target_device_memory) {
                    auto& dc = og.data_conversion;
                    bool multi_threaded_capable_formats =
                        (dc == conversion::fp32_bfp8b || dc == conversion::fp16b_bfp8b || dc == conversion::fp16_bfp8 ||
                         dc == conversion::fp32_bfp8 || dc == conversion::fp16b_bfp8 || dc == conversion::fp16_bfp8b ||
                         dc == conversion::fp32_fp16b || dc == conversion::fp32_fp16 || dc == conversion::int8_bfp8);

                    if (tilizer_backend == Tilizer::FastTilizeDevicePush || !multi_threaded_capable_formats) {
                        // From this point on, num_threads is used to indicate the number of threads needed for tilize.
                        // If bufq target_format is not bfp8*, do not perform multithreaded tilize. Only perform
                        // multithreaded shuffling.
                        num_threads = 1;
                    }
                }
            }
            copy_granularity = queue_grids_[0].tensor_row_pitch;
            if (grids_[0].layout == IO_LAYOUT::Flat) {
                flat_formats_match = host_data_format == grids_[0].bufq_target_format;
                copy_granularity = transfer_size;
            }
        }

        for (unsigned int queue_index = 0; queue_index < g.bufq_grid_dim_c * g.bufq_grid_dim_r; queue_index++) {
            queue_state &s = queue_states_[state_index++];

            unsigned int queue_x = queue_index % g.bufq_grid_dim_c;
            unsigned int queue_y = queue_index / g.bufq_grid_dim_c;
            
            if(og.layout == IO_LAYOUT::Flat) {
                s.data_in = data_ptr + queue_x * block_width * t.itemsize +  queue_y * block_height * block_width * g.bufq_grid_dim_c * t.itemsize;
                s.data_start = s.data_in;
            }
            
            else {
                if(og.data_conversion == conversion::raw32_raw32 || og.data_conversion == conversion::raw16_raw16 || og.data_conversion == conversion::raw8_raw8){
                    // Dealing with raw data in mega row format
                    s.data_in = data_ptr + queue_x*block_width*block_height*t.itemsize/(g.mblock_m*g.ublock_rt) +  queue_y * block_height * block_width * g.bufq_grid_dim_c*t.itemsize;
                    s.data_start = s.data_in;
                }
                else{
                    s.data_in = data_ptr + queue_y * block_height * og.tensor_row_pitch + queue_x * block_width * t.itemsize;
                }
            }
            s.next_w = 0;

            // Configure pointers to the slot specified for random access
            if (og.random_access) {
                s.queue_wptr = random_access_ptr;
                s.write_address = s.queue_base_address(offline_tilize) + og.block_size * random_access_ptr;
            }
        }
    }
}
template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::init_shuffle_idx(const tt::tt_PytorchTensorDesc& tensor) {
    log_assert(host_data_format == tt::DataFormat::Float16 || host_data_format == tt::DataFormat::Float16_b || host_data_format == tt::DataFormat::Float32,
                "Only Float16, Float16b and Float32 are supported for Convolution Shuffling!");
    uint32_t simd_width = 0;
    uint32_t data_format_shift;
    if(host_data_format == tt::DataFormat::Float16 or host_data_format == tt::DataFormat::Float16_b) {
        simd_width = 16;
        data_format_shift = 4; // Can reduce size of conv_output_idx by 2^4 (since simd width is 16)
    }
    else {
        simd_width = 8;
        data_format_shift = 3; // Can reduce size of conv_output_idx by 2^3 (since simd width is 8)
    }
    if(tensor.shape[3] % simd_width) {
        need_scalar_fallback = true;
        data_format_shift = 0;
    }

    conv_output_idx = std::vector<uint32_t>((tensor.shape[0] * tensor.shape[1] * input_face_shape) >> data_format_shift);
    for(int w_idx = 0; w_idx < tensor.shape[0]; w_idx++) {
        uint32_t w_offset = w_idx  * shuffled_row_size * num_rows;
        for(uint32_t z_idx = 0; z_idx < tensor.shape[1]; z_idx++){
            uint32_t z_offset = z_idx * shuffled_row_size;
            uint32_t channel_offset = 0;
            uint32_t row_idx = 0;

            uint32_t face_offset = w_offset + z_offset;
            
            for(uint32_t idx = 0; idx < input_face_shape; idx += std::min(tensor.shape[3] - (idx % tensor.shape[3]), simd_width)) {
                if(idx % tensor.shape[3] + simd_width > tensor.shape[3]) {
                    for(uint32_t i = idx; i < idx + tensor.shape[3] - (idx % tensor.shape[3]); i++) {
                        conv_scalar_output_idx.insert({w_idx * input_face_shape * tensor.shape[1] + z_idx * input_face_shape + i, w_offset + z_offset + channel_offset * stride * tensor.shape[1] * shuffled_row_size + ((i - row_idx * tensor.shape[3]) >> (stride >> 1)) + (i % stride) * shuffled_row_size * tensor.shape[1]});
                    }
                }
                else {
                    uint32_t base_idx = w_idx * input_face_shape * tensor.shape[1] + z_idx * input_face_shape + idx;
                    if(z_idx == 0 && w_idx == 0) {
                        input_idx.push_back(idx);  //Store the input indices for every vector of simd_width that needs to be loaded to shuffled data. Speeds up scalar fallback approach, since computing these indices is expensive
                    }
                    if (idx and !(idx % tensor.shape[3])){
                        channel_offset = (channel_offset + 1) % stride;
                        if((idx % (stride * tensor.shape[3]))) row_idx++;
                    }
                    conv_output_idx[base_idx >> data_format_shift] = face_offset + channel_offset * stride * tensor.shape[1] * shuffled_row_size + ((idx - row_idx * tensor.shape[3]) >> (stride >> 1));
                }
            }
        }
    }
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::init_pad_idx(const tt_PytorchTensorDesc& tensor) {
    // Initialize the output idx for each row when its copied over to the intermediate padded tensor
    tt::io::set_py_tensor_metadata(padded_tensor, {tensor.shape[0], tensor.shape[1], aligned_y, aligned_x}, tensor.itemsize, tensor.dim, tensor.format);

    uint32_t tensor_row_size = tensor.shape[3];
    uint32_t tensor_size = tensor.shape[0] * tensor.shape[1] * tensor.shape[2] * tensor.shape[3];
    uint32_t output_ptr = 0;
    uint32_t num_faces = 0;
    pad_output_idx = std::vector<uint32_t>(tensor_size, 0);
    for(uint32_t input_ptr = 0; input_ptr < tensor_size; input_ptr += tensor_row_size) {
        if(input_ptr and !(input_ptr % (tensor.shape[3] * tensor.shape[2]))) {
            num_faces += 1;
            output_ptr = num_faces * aligned_x * aligned_y;
        }
        pad_output_idx[input_ptr] = output_ptr;
        output_ptr += aligned_x;
    }
}

template<Tilizer tilizer_backend>
template<class T>
void DeviceTilizer<tilizer_backend>::pad_tensor(const tt_PytorchTensorDesc& tensor) {
    // Multithreaded function to pad input tensor
    uint32_t tensor_row_size = tensor.shape[3];
    uint32_t num_faces_in_tensor = tensor.shape[0] * tensor.shape[1];
    uint32_t num_datums_in_face = tensor.shape[2] * tensor.shape[3];
    uint32_t faces_per_thread = num_faces_in_tensor / num_padding_threads;
    uint32_t datums_per_thread = faces_per_thread * num_datums_in_face;
    std::vector<std::thread> threads = {};
    for(int th = 0; th < num_padding_threads; th++) {
        threads.push_back(std::thread([th, &datums_per_thread, &faces_per_thread, &tensor_row_size, &tensor, &num_faces_in_tensor, &num_datums_in_face, this] {
            uint32_t idx_ub = std::min((th + 1) * faces_per_thread, num_faces_in_tensor) * num_datums_in_face;
            uint32_t start_idx = th * datums_per_thread;
            for(uint32_t input_ptr = start_idx; input_ptr < idx_ub; input_ptr += tensor_row_size) {
                if constexpr(std::is_same<T,float>::value) {
                    std::memcpy(padded_vector.data() + pad_output_idx[input_ptr], reinterpret_cast<const float*>(tensor.ptr) + input_ptr, tensor_row_size * 4);
                }
                else {
                    std::memcpy(padded_vector_half.data() + pad_output_idx[input_ptr], reinterpret_cast<const uint16_t*>(tensor.ptr) + input_ptr, tensor_row_size * 2);
                }
            }
        }));
    }
    for(auto & th: threads) th.join();
}

template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::initialize_conv_shuffler(const tt::tt_PytorchTensorDesc& tensor){
    if(stride > 0){
        log_trace(tt::LogIO, "Initializing Shuffler for Convolution");
        log_assert(stride_start_offsets.size(), "Got Stride > 0 without any xy start offsets specified for convolution, for IO queue {}", grids_[0].queue_name);
        
        int kernel_x = (*std::max_element(stride_start_offsets.begin(), stride_start_offsets.end(), [](const auto a, const auto b) {return a.first < b.first;})).first 
                                            - (*std::min_element(stride_start_offsets.begin(), stride_start_offsets.end(), [](const auto a, const auto b) {return a.first < b.first;})).first + 1;
        int kernel_y = (*std::max_element(stride_start_offsets.begin(), stride_start_offsets.end(), [](const auto a, const auto b) {return a.second < b.second;})).second 
                                            - (*std::min_element(stride_start_offsets.begin(), stride_start_offsets.end(), [](const auto a, const auto b) {return a.second < b.second;})).second + 1;
        
        log_assert(kernel_x * kernel_y == stride_start_offsets.size(), "xy_offsets inside the Stride struct should form a bounding box to fully specify the shape of the Convolution Kernel");

        aligned_x = tt::io::align_up(tensor.shape[3], stride);
        aligned_y = tt::io::align_up(tensor.shape[2], stride);

        window_x = std::min(kernel_x, stride);
        window_y =  std::min(kernel_y, stride);
        num_rows_with_data = tensor.shape[1] * window_x * window_y;
        num_rows = ceil(static_cast<double>(num_rows_with_data)/static_cast<double>(quad_height)) * quad_height; // num rows to be used for shuffled tensor

        input_face_shape = aligned_x * aligned_y; // shape of input tensor (2d slices)
        shuffled_row_size = input_face_shape / (stride * stride); // num datums in a single row in the shuffled tensor
        shuffled_row_size = ceil(static_cast<double>(shuffled_row_size)/ static_cast<double>(quad_width)) * quad_width;
        uint32_t new_vector_size = tensor.shape[0] * shuffled_row_size * num_rows;

        if(host_data_format == tt::DataFormat::Float16 or host_data_format == tt::DataFormat::Float16_b) shuffled_data_half = aligned_vector<std::uint16_t>(new_vector_size, 0); // Initialize vector for storing shuffled tensor with 16 bit data
        else if(host_data_format == tt::DataFormat::Float32) shuffled_data = aligned_vector<std::uint32_t>(new_vector_size, 0); // Initialize vector for storing shuffled tensor with 32 bit data
        else if(host_data_format != tt::DataFormat::Invalid) log_error("Unsupported host data format for Convolution Shuffling!"); 

        if((tensor.shape[3] % stride) || (tensor.shape[2] % stride)) {
            // Input tensor is not aligned to stride. Create an intermediate tensor with padding.
            init_pad_idx(tensor);
            num_padding_threads = std::min(8, static_cast<int>(tensor.shape[0]));
            if(host_data_format == tt::DataFormat::Float16 or host_data_format == tt::DataFormat::Float16_b) {
                padded_vector_half = aligned_vector<std::uint16_t>(input_face_shape * tensor.shape[0] * tensor.shape[1], 0);
                padded_tensor.ptr = padded_vector_half.data();
            }
            else {
                padded_vector = aligned_vector<std::uint32_t>(input_face_shape * tensor.shape[0] * tensor.shape[1], 0);
                padded_tensor.ptr = padded_vector.data();
            }
            init_shuffle_idx(padded_tensor);
            tensor_padded = true;
        }
        else {
            // Tensor doesn't need to be padded.
            init_shuffle_idx(tensor);
        }
        can_use_fast_shuffler = (stride == 1 || stride == 2 || stride == 4) && (stride <= kernel_x) && (stride <= kernel_y);
    }
}

template<Tilizer tilizer_backend>
template<Layout layout>
void DeviceTilizer<tilizer_backend>::push_block(const queue_grid& grid, queue_state& queue, uint32_t thread_offset)
{
    block_state next = {0,};
    // We are guaranteed that the entire slot/block does not wrap the queue.
    std::byte* write_address = queue.write_address;
    if(write_combine_weights) {
        write_address = reinterpret_cast<std::byte*> (intermed_buf);
    }
    uint32_t thread_input_offset = tensor_batch_size_in_bytes * thread_offset;
    uint32_t num_datum_cols_per_grid = 32 * grid.limit.tile_y * grid.limit.sub_y;
    uint32_t datum_col_offset_for_grid = queue.y_idx * num_datum_cols_per_grid;
    // If exponents were preloaded, we use this for only writing the sign + mantissa values through the intermed buf. tilecopy.cpp takes care of doing this for MMIO push.
    uint32_t exponent_offset = 64 * queue.exponents_preloaded;
    uint8_t local_exp[64] = {0};
    do {
        uint32_t datum_col_offset_for_quad = (stride > 0) * (datum_col_offset_for_grid + 32 * grid.limit.tile_y * next.sub_y + 32 * next.tile_y)  + (stride <= 0) * 0; 
        vector<bool> fill_data_for_faces = {datum_col_offset_for_quad < num_rows_with_data, datum_col_offset_for_quad + 16 < num_rows_with_data};
        
        std::byte* data_address;
        if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
            data_address = reinterpret_cast<std::byte*> (intermed_buf);
            if(write_combine_weights) {
                data_address = write_address + quad_header_size * grid.write_header;
            }
        } else {
            data_address = write_address + quad_header_size * grid.write_header;
        }
    
        // advance_quad updates data_in
        auto in = reinterpret_cast<const void*>(queue.data_in + thread_input_offset);
        auto out = reinterpret_cast<void*>(data_address);
        
        copy_quad(in, out, copy_granularity, fill_data_for_faces, num_rows_with_data, datum_col_offset_for_quad, adjusted_tile_height, num_faces_y, num_faces_x, local_exp);
        if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
            if(!write_combine_weights) {
                uint8_t* data_in = flat_formats_match ? (uint8_t*)queue.data_in : intermed_buf;
                spill_to_dram_func(reinterpret_cast<uint64_t>(write_address + quad_header_size * grid.write_header + exponent_offset), data_in + exponent_offset, transfer_size - exponent_offset, queue.channel, queue.device);
            }
            
        } 
        write_address += grid.dram_wr_address_update;

    } while (queue.template advance_quad<layout>(grid, &next, grid.ublock_order, grids_[0].bufq_target_format, quad_height));
}

template<Tilizer tilizer_backend>
template<Layout layout>
void DeviceTilizer<tilizer_backend>::do_tilize_and_push()
{
    auto grid_count = queue_grids_.size();
    auto incomplete_queues = queue_states_.size();

    if(use_data_copy_for_transfer) {
        const auto& grid = queue_grids_[0];
        std::uint32_t copy_size = grid.limit.sub_x * grid.limit.tile_x * grid.limit.sub_y * grid.limit.tile_y * grid.limit.z * format_size(host_data_format) * grid.limit.w * tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH;
        while(!(offline_tilize or queue_states_[0].queue_has_space(grid, read_from_dram_func))) {}
        auto& queue = queue_states_[0];
        if constexpr (tilizer_backend == Tilizer::FastTilizeMMIOPush) {
            std::memcpy(queue.write_address, queue.data_in, copy_size);
        }
        else {
            uint8_t* data_in = (uint8_t*)queue.data_in;
            spill_to_dram_func(reinterpret_cast<uint64_t>(queue.write_address), data_in, copy_size, queue.channel, queue.device);
        }
        queue.update_wptr_local_by_offset(grid, grid.limit.w, offline_tilize);
        if (non_mmio_chip) {
            // Wait for data to be comitted before updating wptr for remote queues
            cluster -> wait_for_non_mmio_flush();
        }
        queue.update_wptr_device(grid, spill_to_dram_func, wptr_buf, offline_tilize);
    }
    else {
        while (incomplete_queues > 0) {
            unsigned int queue_index = 0;
            for (unsigned int grid_index = 0; grid_index < grid_count; grid_index++) {
                const auto& grid = queue_grids_[grid_index];

                for (; queue_index < grid.max_queue_index; queue_index++) {
                    auto& queue = queue_states_[queue_index];

                    if ((!queue_done(grid, queue) && queue.queue_has_space(grid, read_from_dram_func)) or offline_tilize) {
                        push_block<layout>(grid, queue, 0);
                        if constexpr(tilizer_backend == Tilizer::FastTilizeDevicePush) {
                            // Write combining is only enabled for Device Push
                            if(write_combine_weights) {
                                spill_to_dram_func(reinterpret_cast<uint64_t>(queue.write_address), reinterpret_cast<uint8_t*>(intermed_buf), grid.block_size, queue.channel, queue.device);
                            }
                        }
                        queue.update_wptr_local(grid, offline_tilize);

                        if (queue_done(grid, queue)) {
                            incomplete_queues--;
                        }
                    }
                }
            }
        }
        if (non_mmio_chip) {
            // Wait for data to be comitted before updating wptr for remote queues
            cluster -> wait_for_non_mmio_flush();
        }
        for (int queue_index = 0; queue_index < queue_grids_[0].max_queue_index; queue_index++) {
            auto& queue = queue_states_[queue_index];
            queue.update_wptr_device(queue_grids_[0], spill_to_dram_func, wptr_buf, offline_tilize);
        }
    }
}


// Version with programmable timeout. Don't update device pointers unless data can be pushed before timeout expires.
template<Tilizer tilizer_backend>
template<Layout layout>
void DeviceTilizer<tilizer_backend>::do_tilize_and_push_with_timeout(const int timeout_in_seconds)
{
    auto timeout_in_ms = 1000 * timeout_in_seconds;

    int loop_idx = 0;
    auto grid = queue_grids_[0];
    
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();

    // Check if there is space in the queue for all the data to be pushed. Throw timeout error if we wait too long.
    for (int queue_index = 0; queue_index < grid.max_queue_index; queue_index++) {
        auto queue = queue_states_[queue_index];
        for(unsigned int batch = 0; batch < grid.limit.w; batch++){
            while(!queue.queue_has_space(grid, read_from_dram_func)) {
                if(loop_idx++ == 1000) {
                    auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
                    if (elapsed_time_in_ms > std::chrono::milliseconds{timeout_in_ms}){
                        throw tt::error_types::timeout_error("Exceeded timeout of " + std::to_string(timeout_in_seconds) + " seconds pushing to device");
                    }
                    loop_idx = 0;
                }
            }
            queue.update_wptr_local(grid, offline_tilize);
        }
        queue_states_[queue_index].queue_rptr  = queue.queue_rptr;
    }

    if (use_data_copy_for_transfer) {
        auto& queue = queue_states_[0];
        std::uint32_t copy_size = grid.limit.sub_x * grid.limit.tile_x * grid.limit.sub_y * grid.limit.tile_y * grid.limit.z * format_size(host_data_format) * grid.limit.w * tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH;
        if constexpr (tilizer_backend == Tilizer::FastTilizeMMIOPush) {
            std::memcpy(queue.write_address, queue.data_in, copy_size);
        }
        else {
            uint8_t* data_in = (uint8_t*)queue.data_in;
            spill_to_dram_func(reinterpret_cast<uint64_t>(queue.write_address), data_in, copy_size, queue.channel, queue.device);
        }
        queue.update_wptr_local_by_offset(grid, grid.limit.w, offline_tilize);
    }
    
    else {
        if(num_threads == 1) {
            for(unsigned int queue_index = 0; queue_index < grid.max_queue_index; queue_index++) {
                auto& queue = queue_states_[queue_index];
                for(unsigned int batch = 0; batch < grid.limit.w; batch++){
                    push_block<layout>(grid, queue, 0);
                    if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
                        // Write combining is only enabled for Device Push
                        if(write_combine_weights) {
                            spill_to_dram_func(reinterpret_cast<uint64_t>(queue.write_address), reinterpret_cast<uint8_t*>(intermed_buf), grid.block_size, queue.channel, queue.device);
                        }
                    }
                    queue.update_wptr_local(grid, offline_tilize);
                }
            }
        }
        else {
            for(unsigned int queue_index = 0; queue_index < grid.max_queue_index; queue_index++) {
                auto& queue = queue_states_[queue_index];
                for(int th = 0; th < num_threads; th++) {
                    push_threads.queue_job([th, &grid, queue, this] {
                        uint32_t num_blocks = std::min((th + 1) * grid.limit.w / num_threads - th * grid.limit.w / num_threads, grid.limit.w - th * grid.limit.w / num_threads);
                        queue_state q = queue;
                        q.update_wptr_local_by_offset(grid, th * grid.limit.w / num_threads, offline_tilize);
                        for(int i = 0; i < num_blocks; i++) {
                            push_block<layout>(grid, q, (th * grid.limit.w)/ num_threads);
                            q.update_wptr_local(grid, offline_tilize);
                        }
                    });
                }
                
                while(push_threads.wait() || push_threads.num_free != num_threads) {
                    continue;
                }

                queue.update_wptr_local_by_offset(grid, grid.limit.w, offline_tilize);
            }
        }
    }
    if (non_mmio_chip) {
        // Wait for data to be comitted before updating wptr for remote queues
        cluster -> wait_for_non_mmio_flush();
    }
    // Finished pushing to all queues, and timeout was not seen, update device wrptr now.   
    for (int queue_index = 0; queue_index < grid.max_queue_index; queue_index++) {
        auto& queue = queue_states_[queue_index];
        queue.update_wptr_device(grid, spill_to_dram_func, wptr_buf, offline_tilize);
    }
}


template<Tilizer tilizer_backend>
void DeviceTilizer<tilizer_backend>::tilize_and_push_to_device(const std::vector<tt_PytorchTensorDesc>& tensors, int timeout_in_seconds, const int random_access_ptr)

{
    #ifdef TILIZE_PERF
    typedef std::chrono::high_resolution_clock clock;
    auto before_init = clock::now();
    #endif

    if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
        log_assert(spill_to_dram_func != nullptr, "The function for writing intermed buffers to dram must be populated when using FastTilizeDevicePush");
        log_assert(read_from_dram_func != nullptr, "The function for reading from dram must be populated for versim");
    }

    init_from_tensors(tensors, random_access_ptr);
    if constexpr (tilizer_backend == Tilizer::FastTilizeDevicePush) {
        if(queue_grids_[0].random_access) {
            // Allocate the intermediate buffer for RAMs locally, since we expect them to be accessed once
            init_intermed_buf();
        }
    }

    #ifdef TILIZE_PERF
    auto after_init = clock::now();
    #endif

    if (timeout_in_seconds > 0){
        if(queue_grids_[0].layout == IO_LAYOUT::Flat) {
            do_tilize_and_push_with_timeout<Layout::Flat>(timeout_in_seconds);
        }
        else if(queue_grids_[0].data_conversion ==  conversion::raw32_raw32 || queue_grids_[0].data_conversion ==  conversion::raw16_raw16 || queue_grids_[0].data_conversion ==  conversion::raw8_raw8) {
            do_tilize_and_push_with_timeout<Layout::MegaRow>(timeout_in_seconds);
        }
        else {
            do_tilize_and_push_with_timeout<Layout::Tilized>(timeout_in_seconds);
        }
    }else{
        if(queue_grids_[0].layout == IO_LAYOUT::Flat) {
            do_tilize_and_push<Layout::Flat>();
        }
        else if(queue_grids_[0].data_conversion ==  conversion::raw32_raw32 || queue_grids_[0].data_conversion ==  conversion::raw16_raw16 || queue_grids_[0].data_conversion ==  conversion::raw8_raw8) {
            do_tilize_and_push<Layout::MegaRow>();
        }
        else {
            do_tilize_and_push<Layout::Tilized>();
        }
    }

    if constexpr(tilizer_backend == Tilizer::FastTilizeDevicePush) {
        if(queue_grids_[0].random_access) {
            delete_intermed_buf();
        }
    }
    #ifdef TILIZE_PERF
    auto after_copy = clock::now();

    auto init_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(after_init - before_init).count();
    auto copy_us = std::chrono::duration_cast<std::chrono::microseconds>(after_copy - after_init).count();

    std::cout << "tilize init: " << init_ns << "ns, do_tilize_and_push: " << copy_us << "\u00b5s\n";

    std::size_t total_write_size = 0;

    for (unsigned int i = 0; i < tensors.size(); i++) {
        const auto& t = tensors[i];
        const auto& og = queue_grids_[i];

        std::size_t tensor_elements = (std::size_t)t.shape[0] * t.shape[1] * t.shape[2] * t.shape[3];

        std::size_t tensor_size;
        switch (og.data_conversion) {
            case conversion::identity_16: tensor_size = tensor_elements * sizeof(std::uint16_t); break;
            case conversion::identity_32: tensor_size = tensor_elements * sizeof(std::uint32_t); break;
            case conversion::fp32_fp16b: tensor_size = tensor_elements * sizeof(std::uint32_t); break;
        }

        total_write_size += tensor_size;
    }

    std::cout << "bytes written: " << total_write_size << '\n';
    #endif
}

template<Tilizer tilizer_backend>
tt_PytorchTensorDesc DeviceTilizer<tilizer_backend>::shuffle_tensor(const tt_PytorchTensorDesc& tensor) {
    log_assert(stride > 0, "Convolution Shuffler called with no stride specified");
    tt_PytorchTensorDesc tensor_to_shuffle(tensor); // tensor object is provided by FE. Explicitly invoke copy constructor to ensure refcount is appropriately incremented.
    if(host_data_format == tt::DataFormat::Float32){
        
        if(tensor_padded) {
            // Extra step: more overhead
            pad_tensor<float>(tensor);
            tensor_to_shuffle = padded_tensor;
        }
        #ifndef __ARM_ARCH
        if(can_use_fast_shuffler){
            if(need_scalar_fallback) {
                if(stride == 1){
                    return shuffle_for_conv_fp32_stride1_scalar_fallback(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx, conv_scalar_output_idx);
                }
                if(stride == 2){
                    return shuffle_for_conv_fp32_stride2_scalar_fallback(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, input_idx, conv_output_idx, conv_scalar_output_idx);
                }
                if(stride == 4){
                    return shuffle_for_conv_fp32_stride4_scalar_fallback(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx, conv_scalar_output_idx);
                }
            }
            else {
                if(stride == 1){
                    return shuffle_for_conv_fp32_stride1(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
                if(stride == 2){
                    return shuffle_for_conv_fp32_stride2(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
                if(stride == 4){
                    return shuffle_for_conv_fp32_stride4(tensor_to_shuffle, shuffled_data, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
            }
        }
        #endif
        return tt::io::shuffle_for_conv_fp32(tensor_to_shuffle, stride, shuffled_data, window_x, window_y, num_rows, input_face_shape, shuffled_row_size); 
    }
    else if(host_data_format == tt::DataFormat::Float16 or host_data_format == tt::DataFormat::Float16_b){
        if(tensor_padded) {
            pad_tensor<uint16_t>(tensor);
            tensor_to_shuffle = padded_tensor;
        }
        #ifndef __ARM_ARCH
        if(can_use_fast_shuffler){
            if(need_scalar_fallback) {
                if(stride == 1){
                    return shuffle_for_conv_fp16_stride1_scalar_fallback(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx, conv_scalar_output_idx);
                }
                if(stride == 2){
                    return shuffle_for_conv_fp16_stride2_scalar_fallback(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, input_idx, conv_output_idx, conv_scalar_output_idx);
                }
                if(stride == 4){
                    return shuffle_for_conv_fp16_stride4_scalar_fallback(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx, conv_scalar_output_idx);
                }
            }
            else {
                if(stride == 1){
                    return shuffle_for_conv_fp16_stride1(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
                if(stride == 2){
                    return shuffle_for_conv_fp16_stride2(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
                if(stride == 4){
                    return shuffle_for_conv_fp16_stride4(tensor_to_shuffle, shuffled_data_half, num_rows, input_face_shape, shuffled_row_size, push_threads, conv_output_idx);
                }
            }
        }
        #endif
        return tt::io::shuffle_for_conv_fp16(tensor_to_shuffle, stride, shuffled_data_half, window_x, window_y, num_rows, input_face_shape, shuffled_row_size);
    }
    return tensor;
}

template class DeviceTilizer<Tilizer::FastTilizeDevicePush>;
template class DeviceTilizer<Tilizer::FastTilizeMMIOPush>;

Tilizer get_tilizer_based_on_io_config(const tt_dram_io_desc &io, DataFormat host_data_format, DataFormat device_data_format, TargetDevice backend_type) {
    #ifdef __ARM_ARCH
    bool fast_tilize_support = (host_data_format == device_data_format) && (io.layout == tt::IO_LAYOUT::Flat);
    if(!fast_tilize_support) return Tilizer::SlowTilize;

    bool supported_tlb_range = io.bufq_mapping.size() > 0 && (backend_type != TargetDevice::Versim);
    if(supported_tlb_range) return Tilizer::FastTilizeMMIOPush;
    return Tilizer::FastTilizeDevicePush;
    #else
    tt_queue_info &queue_info = (*tt::io::get_workload(io.netlist_path)).queues.at(io.queue_name).my_queue_info;
    bool supported_conversion = hw_tilizer_conversions.find({host_data_format, device_data_format}) != hw_tilizer_conversions.end();
    bool supported_tile_shape = !(get_tile_dim_x(queue_info) % 16); // HW tilizer only supports face aligned tile dims
    if(not (supported_conversion && supported_tile_shape)) return Tilizer::SlowTilize;
    bool supported_tlb_ranges = io.bufq_mapping.size() > 0;

    tt_cluster *cluster = tt::io::get_cluster(io.netlist_path, io.backend_type);
    bool queue_on_mmio_chip = cluster->get_cluster_desc()->is_chip_mmio_capable(queue_info.target_device);
    bool use_hw_tilize = supported_conversion && supported_tlb_ranges && queue_on_mmio_chip;

    if(use_hw_tilize and !(backend_type == TargetDevice::Versim or backend_type == TargetDevice::Emulation)) return Tilizer::FastTilizeMMIOPush;
    return Tilizer::FastTilizeDevicePush;
    #endif
}