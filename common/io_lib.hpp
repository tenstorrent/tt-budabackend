// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <unistd.h>

#include "model/tensor.hpp"
#include "common/model/constants.hpp"
#include "netlist/tt_backend_api_types.hpp"
#include "model/tilize_untilize_api/alignment.h"
#include "common/aligned_allocator.h"
#include "l1_address_map.h" // Need this to determine the minimum tile alignment

template <class T, std::size_t Alignment = std::max(input_alignment, output_alignment)>
using aligned_vector = std::vector<T, AlignedAllocator<T, Alignment>>;
using tt_py_desc = tt::tt_PytorchTensorDesc;

namespace tt {
//! Helper function for io purposes
namespace io {
// Extract NOC Address Alignment Value from l1_address_map and explictly define that as the
// tile alignment and io_queue_header_size_bytes used in runtime IO 
static constexpr uint32_t tile_alignment_bytes = NOC_ADDRESS_ALIGNMENT;
static constexpr uint32_t io_queue_header_size_bytes = NOC_ADDRESS_ALIGNMENT;

struct tt_io_info {
    std::string output_dir;
    // Must set this variable to perf_desc.override_perf_output_dir which can be set by --perf-output-dir cmdline arg
    std::string perf_output_dir = "";
    pid_t proc_id;
    tt_io_info() {}
    tt_io_info(std::string output_dir, std::string perf_output_dir, pid_t proc_id): output_dir(output_dir), perf_output_dir(perf_output_dir), proc_id(proc_id) {}
};

extern tt_io_info info;

// Converts an untilized vector to a format that can be consumed by tt_tile::tilize
std::vector<float> process_raw_untilized(const uint32_t* mem_vector, int vector_len, DataFormat format);
std::vector<float> process_int8_untilized_with_twos_complement_conversion(std::vector<uint32_t> &mem_vector);
void convert_int8_pytorch_tensor_to_host_format(tt::tt_PytorchTensorDesc &py_tensor_desc);
std::vector<float> process_int32_untilized_with_twos_complement_conversion(std::vector<uint32_t> &mem_vector);
void convert_int32_pytorch_tensor_to_host_format(tt::tt_PytorchTensorDesc &py_tensor_desc);
// Converts a device format into a pytorch supported format that's lossless with the minimal storage requirements
DataFormat get_pytorch_tensor_format(DataFormat device_format);

template <class T>
tt_py_desc get_pytorch_tensor_desc_from_array(
    const T* array,
    unsigned int w_dim,
    unsigned int z_dim,
    unsigned int r_dim,
    unsigned int c_dim,
    DataFormat format,
    unsigned int dim = tt::PY_TENSOR_DIMS,
    unsigned int tile_height = tt::constants::TILE_HEIGHT,
    unsigned int tile_width = tt::constants::TILE_WIDTH) {
    tt_PytorchTensorDesc PytorchTensorDesc;
    PytorchTensorDesc.owner = tt::OWNERSHIP::Backend;
    PytorchTensorDesc.ptr = array;
    PytorchTensorDesc.itemsize = sizeof(T);
    PytorchTensorDesc.format = get_pytorch_tensor_format(format);
    PytorchTensorDesc.shape = {w_dim, z_dim, r_dim * tile_height, c_dim * tile_width};
    PytorchTensorDesc.dim = dim;

    PytorchTensorDesc.strides[3] = sizeof(T);
    PytorchTensorDesc.strides[2] = c_dim * tile_width * PytorchTensorDesc.strides[3];
    PytorchTensorDesc.strides[1] = r_dim * tile_height * PytorchTensorDesc.strides[2];
    PytorchTensorDesc.strides[0] = z_dim * PytorchTensorDesc.strides[1];
    return PytorchTensorDesc;
}

tt_py_desc expand_pytorch_tensor_dims(const tt_py_desc& py_tensor_desc);
tt_py_desc reduce_pytorch_tensor_dims(const tt_py_desc& py_tensor_desc);

//! Cpp env only: create pytorch tensor desc for host storaged raw vecctors
template <class T>
tt_py_desc get_pytorch_tensor_desc(tt_tensor &tensor, aligned_vector<T> &host_storage, bool force_megarow = false);
void set_py_tensor_metadata(tt_PytorchTensorDesc& tensor, std::array<std::uint32_t, PY_TENSOR_DIMS> shape, std::uint32_t itemsize, unsigned int dim, tt::DataFormat format);
tt::tt_PytorchTensorDesc shuffle_for_conv_fp32(const tt::tt_PytorchTensorDesc &py_tensor_desc, int& stride, aligned_vector<std::uint32_t>& shuffled_data, std::uint32_t& window_x, std::uint32_t& window_y, uint32_t num_rows, uint32_t input_face_shape, uint32_t shuffled_row_size);
tt::tt_PytorchTensorDesc shuffle_for_conv_fp16(const tt::tt_PytorchTensorDesc &py_tensor_desc, int& stride, aligned_vector<std::uint16_t>& shuffled_data, std::uint32_t& window_x, std::uint32_t& window_y, uint32_t num_rows, uint32_t input_face_shape, uint32_t shuffled_row_size);
uint32_t align_up(uint32_t dim, uint32_t alignment);
tt_tensor tilized_tensor_to_tt_tensor(const tt_TilizedTensorDesc& in, const tt_dram_io_desc& q_desc, Dim ublock_scan);
void slow_prestride(
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    tt::tt_PytorchTensorDesc& shuffled_tensor,
    const tt::tt_dram_io_desc& q_desc,
    aligned_vector<uint16_t>& shuffled_data_half,
    aligned_vector<uint32_t>& shuffled_data);
}  // namespace io
}  // namespace tt
