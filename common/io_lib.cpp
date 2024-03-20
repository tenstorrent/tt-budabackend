// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/io_lib.hpp"

#include <cstdint>
#include <vector>

#include "common/size_lib.hpp"
#include "common/env_lib.hpp"
#include "model/tile.hpp"
#include "tt_backend_api_types.hpp"

namespace tt::io {

tt_io_info info;

//! Converts an untilized vector to a format that can be consumed by tt_tile::tilize
std::vector<float> process_raw_untilized(const uint32_t* mem_vector, int vector_len, DataFormat format) {
    float wf;
    uint32_t wd;
    std::vector<float> rv;
    if (format == DataFormat::Float16 || format == DataFormat::Float16_b || format == DataFormat::RawUInt16 || format == DataFormat::UInt16) {
        rv.resize(vector_len * 2);
    } else if (format == DataFormat::Int8 || format == DataFormat::UInt8 || format == DataFormat::RawUInt8) {
        rv.resize(vector_len * 4);
    } else {
        rv.resize(vector_len);
    }
    int index = 0;

    for (int i = 0; i < vector_len; i++) {
        uint32_t datum = mem_vector[i];
        switch (format) {
            case DataFormat::Float16:
                // ieee compliant fp16 conversion, possible to improve w/ hw assumptions
                for (int i = 0; i < 2; i++) {
                    wd = (datum & 0xffff) << 16;
                    uint32_t nonsign = wd & UINT32_C(0x7FFFFFFF);
                    uint32_t renorm_shift = __builtin_clz(nonsign);
                    renorm_shift = renorm_shift > 5 ? renorm_shift - 5 : 0;
                    int32_t inf_nan_mask = ((int32_t)(nonsign + 0x04000000) >> 8) & INT32_C(0x7F800000);
                    int32_t zero_mask = (int32_t)(nonsign - 1) >> 31;
                    wd = (wd & UINT32_C(0x80000000)) |
                         ((((nonsign << renorm_shift >> 3) + ((0x70 - renorm_shift) << 23)) | inf_nan_mask) &
                          ~zero_mask);
                    wf = *((float *)(&wd));
                    rv[index++] = wf;
                    datum >>= 16;
                }
                break;
            case DataFormat::Float16_b:
                // first datum, only look at the bottom 16 bits
                for (int i = 0; i < 2; i++) {
                    wd = (datum & 0xffff) << 16;
                    wf = *((float *)(&wd));
                    rv[index++] = wf;
                    datum >>= 16;
                }
                break;
            case DataFormat::Float32:
                wf = *reinterpret_cast<float *>(&datum);
                rv[index++] = wf;
                break;
            case DataFormat::Int8:
                for (int i = 0; i < 4; i++) {
                    wf = (float)((int8_t)(datum & 0xff));
                    rv[index++] = wf;
                    datum >>= 8;
                }
                break;
            case DataFormat::UInt8:
                for (int i = 0; i < 4; i++) {
                    wf = (float)((uint8_t)(datum & 0xff));
                    rv[index++] = wf;
                    datum >>= 8;
                }
                break;
            case DataFormat::Int32:
                wf = (float)((int32_t)(datum));
                rv[index++] = wf;
                break;
            case DataFormat::RawUInt8:
                for (int i = 0; i < 4; i++) {
                    wd = (datum & 0xff);
                    wf = *((float *)(&wd));
                    rv[index++] = wf;
                    datum >>= 8;
                }
                break;
            case DataFormat::RawUInt16:
                for (int i = 0; i < 2; i++) {
                    wd = (datum & 0xffff);
                    wf = *((float *)(&wd));
                    rv[index++] = wf;
                    datum >>= 16;
                }
                break;
            case DataFormat::RawUInt32:
                wf = *reinterpret_cast<float *>(&datum);
                rv[index++] = wf;
                break;
            case DataFormat::UInt16:
                for (int i = 0; i < 2; i++) {
                    wd = (datum & 0xffff);
                    wf = ((float)(wd));
                    rv[index++] = wf;
                    datum >>= 16;
                }
                break;
            default: log_fatal("tt::io::process_raw_untilized does not support this format"); break;
        }
    }
    return rv;
}

std::vector<float> process_int8_untilized_with_twos_complement_conversion(std::vector<uint32_t> &mem_vector) {
    std::vector<float> rv;
    uint8_t byte;
    for (uint32_t datum : mem_vector) {
        for (int i = 0; i < 4; i++) {
            byte = datum & 0xff;
            if(datum & 0x80) {
                rv.push_back((float)(0x80 | (~(byte & 0x7f) + 1)));
            }
            else {
                rv.push_back((float)((int8_t)(datum & 0xff)));
            }
            datum >>= 8;
        }
    }
    return rv;
}

void convert_int8_pytorch_tensor_to_host_format(tt::tt_PytorchTensorDesc &py_tensor_desc) {
    if(parse_env<bool>("TT_BACKEND_FORCE_SLOW_UNTILIZE", false)) {
        tt::untilize_utils::convert_device_int8_representation_to_host((uint8_t*)py_tensor_desc.ptr, py_tensor_desc.shape[0] * py_tensor_desc.shape[1] * py_tensor_desc.shape[2] * py_tensor_desc.shape[3]);
    }
    else {
        tt::untilize_utils::convert_device_int8_representation_to_host_vectorized((uint8_t*)py_tensor_desc.ptr, py_tensor_desc.shape[0] * py_tensor_desc.shape[1] * py_tensor_desc.shape[2] * py_tensor_desc.shape[3]);
    }
}

std::vector<float> process_int32_untilized_with_twos_complement_conversion(std::vector<uint32_t> &mem_vector) {
    std::vector<float> rv;
    for (uint32_t datum : mem_vector) {
        if(datum & 0x80000000) {
            rv.push_back((float)((int32_t)(~(datum & 0x7fffffff) + 1)));
        }
        else {
            rv.push_back((float)((int32_t)(datum)));
        }
    }
    return rv;
}

void convert_int32_pytorch_tensor_to_host_format(tt::tt_PytorchTensorDesc &py_tensor_desc) {
    if(parse_env<bool>("TT_BACKEND_FORCE_SLOW_UNTILIZE", false)) {
        tt::untilize_utils::convert_device_int32_representation_to_host((uint32_t*)py_tensor_desc.ptr, py_tensor_desc.shape[0] * py_tensor_desc.shape[1] * py_tensor_desc.shape[2] * py_tensor_desc.shape[3]);
    }
    else {
        tt::untilize_utils::convert_device_int32_representation_to_host_vectorized((uint32_t*)py_tensor_desc.ptr, py_tensor_desc.shape[0] * py_tensor_desc.shape[1] * py_tensor_desc.shape[2] * py_tensor_desc.shape[3]);
    }
}
// Reversable swap of outermost dims, ie. can be called to expand and reduce dimentionality
// Backend  - fixed positions [w, z, y, x] with 0 being batch dim
// Frontend - variable positions based on `dim` with outermost being batch dim
// To reconcile the differences we locate the outermost dims and perform a swap, non-swapped fields are assumed to be valid
void swap_outermost_dims(tt::tt_PytorchTensorDesc &py_tensor_desc) {
    if (py_tensor_desc.dim < tt::PY_TENSOR_DIMS) {
        const int backend_dim = 0;
        const int frontend_dim = tt::PY_TENSOR_DIMS - py_tensor_desc.dim;
        std::swap(py_tensor_desc.shape[backend_dim], py_tensor_desc.shape[frontend_dim]);
        py_tensor_desc.dim = tt::PY_TENSOR_DIMS;
    }
}

tt::tt_PytorchTensorDesc expand_pytorch_tensor_dims(const tt::tt_PytorchTensorDesc &py_tensor_desc) {
    tt::tt_PytorchTensorDesc tmp = py_tensor_desc;
    if (py_tensor_desc.dim < tt::PY_TENSOR_DIMS) {
        // check if tensor dims are expandable
        for (unsigned int dim=0; dim<(tt::PY_TENSOR_DIMS-py_tensor_desc.dim); dim++) {
            log_assert(tmp.shape[dim] == 1, "Unused outermost dims must have shape=1");
            // replicate strides to outermost dims
            tmp.strides[dim] = tmp.strides[tt::PY_TENSOR_DIMS-py_tensor_desc.dim];
        }
        // swap dims
        swap_outermost_dims(tmp);
    }
    return tmp;
}

tt::tt_PytorchTensorDesc reduce_pytorch_tensor_dims(const tt::tt_PytorchTensorDesc &py_tensor_desc) {
    tt::tt_PytorchTensorDesc tmp = py_tensor_desc;
    if (py_tensor_desc.dim < tt::PY_TENSOR_DIMS) {
        log_assert(py_tensor_desc.owner == OWNERSHIP::Backend, "Only BE owned tensor dims can be reduced!");
        swap_outermost_dims(tmp);
        // check if tensor reduced dims are valid
        for (unsigned int dim=0; dim<(tt::PY_TENSOR_DIMS-py_tensor_desc.dim); dim++) {
            log_assert(tmp.shape[dim] == 1, "Unused outermost dims must have shape=1");
            // fill in missing outermost strides
            tmp.strides[dim] = tmp.strides[tt::PY_TENSOR_DIMS-py_tensor_desc.dim] * tmp.shape[tt::PY_TENSOR_DIMS-py_tensor_desc.dim];
        }
    }
    return tmp;
}

template <>
tt::tt_PytorchTensorDesc get_pytorch_tensor_desc<float>(tt_tensor &tensor, aligned_vector<float> &host_storage, bool force_megarow) {
    tt_tensor_metadata md = tensor.metadata;
    host_storage.clear();

    // Flatten tt_tensor to underlying pytorch float data vector
    vector<float> flat_tensor;
    tensor.untilize_to_flat_tensor_data(true, false, force_megarow, flat_tensor);
    host_storage.insert(std::end(host_storage), std::begin(flat_tensor), std::end(flat_tensor));

    // Convert to pybuda tensor for tilize push
    tt_PytorchTensorDesc tensor_desc;
    tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<float>(host_storage.data(), md.shape.w, md.shape.z, md.shape.rt, md.shape.ct, md.data_format, 4, md.shape.tile_height, md.shape.tile_width);
    return tensor_desc;
}

template <>
tt_PytorchTensorDesc get_pytorch_tensor_desc<uint16_t>(tt_tensor &tensor, aligned_vector<uint16_t> &host_storage, bool force_megarow) {
    tt_tensor_metadata md = tensor.metadata;
    host_storage.clear();

    // Flatten tt_tensor to underlying pytorch float data vector
    vector<uint16_t> flat_tensor;
    tensor.untilize_to_flat_tensor_data_half(true, false, flat_tensor);
    host_storage.insert(std::end(host_storage), std::begin(flat_tensor), std::end(flat_tensor));

    // Convert to pybuda tensor for tilize push
    tt_PytorchTensorDesc tensor_desc;
    tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint16_t>(host_storage.data(), md.shape.w, md.shape.z, md.shape.rt, md.shape.ct, md.data_format, 4, md.shape.tile_height, md.shape.tile_width);
    return tensor_desc;
}

template<>
tt_PytorchTensorDesc get_pytorch_tensor_desc<int8_t>(tt_tensor &tensor, aligned_vector<int8_t> &host_storage, bool force_megarow) {
    tt_tensor_metadata md = tensor.metadata;
    host_storage.clear();

     // Flatten tt_tensor to underlying pytorch float data vector
    vector<int8_t> flat_tensor;
    tensor.untilize_to_flat_tensor_data_byte(true, false, flat_tensor);
    host_storage.insert(std::end(host_storage), std::begin(flat_tensor), std::end(flat_tensor));

    // Convert to pybuda tensor for tilize push
    tt_PytorchTensorDesc tensor_desc;
    tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int8_t>(host_storage.data(), md.shape.w, md.shape.z, md.shape.rt, md.shape.ct, md.data_format, 4, md.shape.tile_height, md.shape.tile_width);
    return tensor_desc;
}

template<>
tt_PytorchTensorDesc get_pytorch_tensor_desc<uint8_t>(tt_tensor &tensor, aligned_vector<uint8_t> &host_storage, bool force_megarow) {
    tt_tensor_metadata md = tensor.metadata;
    host_storage.clear();

     // Flatten tt_tensor to underlying pytorch float data vector
    vector<uint8_t> flat_tensor;
    tensor.untilize_to_flat_tensor_data_unsigned_byte(true, false, flat_tensor);
    host_storage.insert(std::end(host_storage), std::begin(flat_tensor), std::end(flat_tensor));

    // Convert to pybuda tensor for tilize push
    tt_PytorchTensorDesc tensor_desc;
    tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint8_t>(host_storage.data(), md.shape.w, md.shape.z, md.shape.rt, md.shape.ct, md.data_format, 4, md.shape.tile_height, md.shape.tile_width);
    return tensor_desc;
}

template<>
tt_PytorchTensorDesc get_pytorch_tensor_desc<int32_t>(tt_tensor &tensor, aligned_vector<int32_t> &host_storage, bool force_megarow) {
    tt_tensor_metadata md = tensor.metadata;
    host_storage.clear();

     // Flatten tt_tensor to underlying pytorch float data vector
    vector<int32_t> flat_tensor;
    tensor.untilize_to_flat_tensor_data_dword(true, false, flat_tensor);
    host_storage.insert(std::end(host_storage), std::begin(flat_tensor), std::end(flat_tensor));

    // Convert to pybuda tensor for tilize push
    tt_PytorchTensorDesc tensor_desc;
    tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int32_t>(host_storage.data(), md.shape.w, md.shape.z, md.shape.rt, md.shape.ct, md.data_format, 4, md.shape.tile_height, md.shape.tile_width);
    return tensor_desc;
}

void set_py_tensor_metadata(tt_PytorchTensorDesc& tensor, std::array<std::uint32_t, PY_TENSOR_DIMS> shape, std::uint32_t itemsize, unsigned int dim, tt::DataFormat format) {
    tensor.shape = shape;
    tensor.itemsize = itemsize;
    tensor.format = format;
    tensor.dim = dim;
    tensor.strides[3] = itemsize;
    tensor.strides[2] = tensor.shape[3] * tensor.strides[3];
    tensor.strides[1] = tensor.shape[2] * tensor.strides[2];
    tensor.strides[0] = tensor.shape[1] * tensor.strides[1];
}

// Slow data shuffling functions for convolutions. Shared by silicon and golden
tt::tt_PytorchTensorDesc shuffle_for_conv_fp32(const tt::tt_PytorchTensorDesc &py_tensor_desc, int& stride, aligned_vector<std::uint32_t>& shuffled_data, std::uint32_t& window_x, std::uint32_t& window_y, uint32_t num_rows, uint32_t input_face_shape, uint32_t shuffled_row_size) {
    uint32_t input_channel_count = py_tensor_desc.shape[1];

    for(uint32_t w_idx = 0; w_idx < py_tensor_desc.shape[0]; w_idx++){
        uint32_t w_offset = w_idx  * shuffled_row_size * num_rows;
        for(uint32_t z_idx = 0; z_idx < input_channel_count; z_idx++){
            uint32_t z_offset = z_idx * shuffled_row_size; // the 3d index of this tensor face [0:3]
            uint32_t channel_offset = 0; // (1st 2 channels or 2nd 2 channels)
            uint32_t row_idx = 0;
            // iterate over all datums in this 2d tensor block and assign them to the corresponding locations in the shuffled tensor
            for(uint32_t idx = 0; idx < input_face_shape; idx++){
                if (idx and !(idx % py_tensor_desc.shape[3])){
                    channel_offset = (channel_offset + 1) % stride;
                    if((idx % (stride * py_tensor_desc.shape[3]))) row_idx++;
                }

                if(channel_offset < window_y and idx % stride < window_x){
                    uint32_t input_datum = *(static_cast<const uint32_t*>(py_tensor_desc.ptr) + w_idx * input_face_shape * input_channel_count + z_idx * input_face_shape + idx);
                    uint32_t shuffled_idx = w_offset + z_offset + channel_offset * window_x * input_channel_count * shuffled_row_size + (idx % stride) * shuffled_row_size * input_channel_count + ((idx - row_idx * py_tensor_desc.shape[3]) / (stride));
                    shuffled_data.at(shuffled_idx) = input_datum;
                }
            }
        }
    }
    return tt::io::get_pytorch_tensor_desc_from_array(shuffled_data.data(), py_tensor_desc.shape[0], 1, num_rows / tt::constants::TILE_HEIGHT, shuffled_row_size/tt::constants::TILE_WIDTH, py_tensor_desc.format);
}

tt::tt_PytorchTensorDesc shuffle_for_conv_fp16(const tt::tt_PytorchTensorDesc &py_tensor_desc, int& stride, aligned_vector<std::uint16_t>& shuffled_data, std::uint32_t& window_x, std::uint32_t& window_y, uint32_t num_rows, uint32_t input_face_shape, uint32_t shuffled_row_size) {
    uint32_t input_channel_count =  py_tensor_desc.shape[1];

    for(uint32_t w_idx = 0; w_idx < py_tensor_desc.shape[0]; w_idx++){
        uint32_t w_offset = w_idx  * shuffled_row_size * num_rows;
        for(uint32_t z_idx = 0; z_idx < input_channel_count; z_idx++){
            uint32_t z_offset = z_idx * shuffled_row_size; // the 3d index of this tensor face [0:3]
            uint32_t channel_offset = 0; // (1st 2 channels or 2nd 2 channels)
            uint32_t row_idx = 0;
            // iterate over all datums in this 2d tensor block and assign them to the corresponding locations in the shuffled tensor
            for(uint32_t idx = 0; idx < input_face_shape; idx++){
                if (idx and !(idx % py_tensor_desc.shape[3])){
                    channel_offset = (channel_offset + 1) % stride;
                    if((idx % (stride * py_tensor_desc.shape[3]))) row_idx++;
                }
                if(channel_offset < window_y and idx % stride < window_x){
                    uint16_t input_datum = *(static_cast<const uint16_t*>(py_tensor_desc.ptr) + w_idx * input_face_shape * input_channel_count + z_idx * input_face_shape + idx);
                    uint32_t shuffled_idx = w_offset + z_offset + channel_offset * window_x * input_channel_count * shuffled_row_size + (idx % stride) * shuffled_row_size * input_channel_count + ((idx - row_idx * py_tensor_desc.shape[3]) / (stride));
                    shuffled_data.at(shuffled_idx) = input_datum;
                }
            }
        }
    }
    return tt::io::get_pytorch_tensor_desc_from_array(shuffled_data.data(), py_tensor_desc.shape[0], 1, num_rows / tt::constants::TILE_HEIGHT, shuffled_row_size/tt::constants::TILE_WIDTH, py_tensor_desc.format);
}

uint32_t align_up(uint32_t dim, uint32_t alignment) {
    uint32_t new_dim = dim - 1;
    return new_dim - (new_dim % alignment) + alignment;
}


template <class T>
void pad_tensor(
    const tt_PytorchTensorDesc& tensor,
    tt_PytorchTensorDesc& padded_tensor,
    aligned_vector<T>& padded_vector) {
    // Single threaded function to apply padding before conv. Used only for verification
    uint32_t tensor_row_size = tensor.shape[3];
    uint32_t tensor_size = tensor.shape[0] * tensor.shape[1] * tensor.shape[2] * tensor.shape[3];
    uint32_t output_ptr = 0;
    uint32_t num_faces = 0;
    for(uint32_t input_ptr = 0; input_ptr < tensor_size; input_ptr += tensor_row_size) {
        if(input_ptr and !(input_ptr % (tensor.shape[3] * tensor.shape[2]))) {
            num_faces += 1;
            output_ptr = num_faces * padded_tensor.shape[2] * padded_tensor.shape[3];
        }
        if constexpr(std::is_same<T,uint32_t>::value) {
            std::memcpy(padded_vector.data() + output_ptr, reinterpret_cast<const uint32_t*>(tensor.ptr) + input_ptr, tensor_row_size * 4);
        }
        else {
            std::memcpy(padded_vector.data() + output_ptr, reinterpret_cast<const uint16_t*>(tensor.ptr) + input_ptr, tensor_row_size * 2);
        }

        output_ptr += padded_tensor.shape[3];
    }
    padded_tensor.ptr = padded_vector.data();
}

void get_conv_shuffler_params(
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    const tt::tt_dram_io_desc &q_desc,
    uint32_t &window_x,
    uint32_t &window_y,
    uint32_t &num_rows,
    uint32_t &input_face_shape,
    uint32_t &shuffled_row_size,
    tt_PytorchTensorDesc& tensor_to_shuffle) {
    std::vector<std::pair<int, int>> stride_offsets = q_desc.s_descriptor.xy_offsets;
    int kernel_x = (*std::max_element(
                        stride_offsets.begin(),
                        stride_offsets.end(),
                        [](const auto a, const auto b) { return a.first < b.first; })).first -
                   (*std::min_element(
                        stride_offsets.begin(),
                        stride_offsets.end(),
                        [](const auto a, const auto b) { return a.first < b.first; }))
                       .first + 1;
    int kernel_y = (*std::max_element(
                        stride_offsets.begin(),
                        stride_offsets.end(),
                        [](const auto a, const auto b) { return a.second < b.second; })).second -
                   (*std::min_element(
                        stride_offsets.begin(),
                        stride_offsets.end(),
                        [](const auto a, const auto b) { return a.second < b.second; })).second + 1;
    log_assert(
        kernel_x * kernel_y == static_cast<int>(stride_offsets.size()),
        "xy_offsets inside the Stride struct should form a bounding box to fully specify the shape of the Convolution "
        "Kernel");

    window_x = std::min(kernel_x, q_desc.s_descriptor.stride);
    window_y = std::min(kernel_y, q_desc.s_descriptor.stride);

    num_rows = ceil(
                   static_cast<double>(py_tensor_desc.shape[1] * window_x * window_y) /
                   static_cast<double>(tt::constants::TILE_HEIGHT)) *
               tt::constants::TILE_HEIGHT;
    
    uint32_t aligned_x = tt::io::align_up(py_tensor_desc.shape[3], q_desc.s_descriptor.stride);
    uint32_t aligned_y = tt::io::align_up(py_tensor_desc.shape[2], q_desc.s_descriptor.stride);

    input_face_shape = aligned_x * aligned_y;
    shuffled_row_size = input_face_shape / (q_desc.s_descriptor.stride * q_desc.s_descriptor.stride);
    shuffled_row_size = ceil(static_cast<double>(shuffled_row_size) / static_cast<double>(tt::constants::TILE_WIDTH)) *
                        tt::constants::TILE_WIDTH;
    tt::io::set_py_tensor_metadata(tensor_to_shuffle, {py_tensor_desc.shape[0], py_tensor_desc.shape[1], aligned_y, aligned_x}, py_tensor_desc.itemsize, py_tensor_desc.dim, py_tensor_desc.format);
}

void slow_prestride(
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    tt::tt_PytorchTensorDesc& shuffled_tensor,
    const tt::tt_dram_io_desc& q_desc,
    aligned_vector<uint16_t>& shuffled_data_half,
    aligned_vector<uint32_t>& shuffled_data) {
    aligned_vector<uint16_t> padded_data_half = {};
    aligned_vector<uint32_t> padded_data = {};
    if(q_desc.s_descriptor.stride > 0) {
        // Need to perform convolution shuffling
        int stride = q_desc.s_descriptor.stride;
        uint32_t window_x, window_y, num_rows, input_face_shape, shuffled_row_size;
        tt_PytorchTensorDesc tensor_to_shuffle(py_tensor_desc);
        get_conv_shuffler_params(
            py_tensor_desc, q_desc, window_x, window_y, num_rows, input_face_shape, shuffled_row_size, tensor_to_shuffle);
        if (py_tensor_desc.format == tt::DataFormat::Float16_b || py_tensor_desc.format == tt::DataFormat::Float16) {
            if((py_tensor_desc.shape[3] % stride) || (py_tensor_desc.shape[2] % stride)) {
                padded_data_half = aligned_vector<uint16_t>(tensor_to_shuffle.shape[0] * tensor_to_shuffle.shape[1] * input_face_shape);
                pad_tensor<uint16_t>(py_tensor_desc, tensor_to_shuffle, padded_data_half);
            }
            shuffled_data_half =
                aligned_vector<std::uint16_t>(tensor_to_shuffle.shape[0] * shuffled_row_size * num_rows, 0);
            shuffled_tensor = tt::io::shuffle_for_conv_fp16(
                tensor_to_shuffle,
                stride,
                shuffled_data_half,
                window_x,
                window_y,
                num_rows,
                input_face_shape,
                shuffled_row_size);
        } else if (py_tensor_desc.format == tt::DataFormat::Float32) {
            if((py_tensor_desc.shape[3] % stride) || (py_tensor_desc.shape[2] % stride)) {
                padded_data = aligned_vector<uint32_t>(tensor_to_shuffle.shape[0] * tensor_to_shuffle.shape[1] * input_face_shape);
                pad_tensor<uint32_t>(py_tensor_desc, tensor_to_shuffle, padded_data);
            }
            shuffled_data = aligned_vector<std::uint32_t>(tensor_to_shuffle.shape[0] * shuffled_row_size * num_rows, 0);
            shuffled_tensor = tt::io::shuffle_for_conv_fp32(
                tensor_to_shuffle,
                stride,
                shuffled_data,
                window_x,
                window_y,
                num_rows,
                input_face_shape,
                shuffled_row_size);
        } else {
            log_fatal("Convolution Shuffling is only enabled for Fp32 and Fp16* host formats.");
        }
    }
}

DataFormat get_pytorch_tensor_format(DataFormat device_format) {
    switch (device_format) {
        case DataFormat::Float32:
            return DataFormat::Float32;
        case DataFormat::RawUInt32:
            return DataFormat::RawUInt32;
        case DataFormat::RawUInt16:
            return DataFormat::RawUInt16;
        case DataFormat::RawUInt8:
            return DataFormat::RawUInt8;
        case DataFormat::Int8:
            return DataFormat::Int8;
        case DataFormat::UInt8:
            return DataFormat::UInt8;
        case DataFormat::Int32:
            return DataFormat::Int32;
        case DataFormat::UInt16:
            return DataFormat::UInt16;
        case DataFormat::Float16_b:
        case DataFormat::Bfp8_b:
        case DataFormat::Bfp4_b:
        case DataFormat::Bfp2_b:
            return DataFormat::Float16_b;
        case DataFormat::Float16:
        case DataFormat::Bfp8:
        case DataFormat::Bfp4:
        case DataFormat::Bfp2:
            return DataFormat::Float16;
        default: log_fatal("get_pytorch_tensor_format doesn't support this format.."); break;
    }
    return DataFormat::Invalid;
}

void get_ublock_data(
    const tt_dram_io_desc &q_desc,
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
    int gr = buf_index / q_desc.bufq_grid_dim_c;
    int gc = buf_index % q_desc.bufq_grid_dim_c;
    for (uint32_t tr=0; tr<q_desc.ublock_rt; ++tr) {
        for (uint32_t tc=0; tc<q_desc.ublock_ct; ++tc) {
            int rt_index = gr*q_desc.ublock_rt*q_desc.mblock_m + ubr*q_desc.ublock_rt + tr;
            int ct_index = gc*q_desc.ublock_ct*q_desc.mblock_n + ubc*q_desc.ublock_ct + tc;
            tt_tile *tile = tensor.get_tile_ptr(rt_index, ct_index, zi, wi, q_desc.tile_height, q_desc.tile_width);
            log_assert(!tile->packed_data_present(), "expected packed data to be empty");
            uint32_t tile_size = tile->size_bytes(true);
            tile->packed_data.resize(tile_size);
            std::memcpy(tile->packed_data.data(), rv.data() + global_offset + buf_offset, tile_size);
            tile->packed_data_to_tile();
            buf_offset += tile_size / sizeof(uint32_t);
        }
    }
}

tt_tensor tilized_tensor_to_tt_tensor(const tt_TilizedTensorDesc& in, const tt_dram_io_desc& q_desc, Dim ublock_scan) {
    // Direct conversion from tt_TilizedTensorDesc to tt_tensor (useful for golden)
    log_assert(q_desc.layout == IO_LAYOUT::Tilized, "{} is only enabled for queues with tilized layout", __FUNCTION__);
    tt_tensor_metadata md;
    uint32_t block_size = tt::size::get_entry_size_in_bytes(q_desc.bufq_target_format, q_desc.layout == IO_LAYOUT::Tilized, 
                                    q_desc.ublock_ct, q_desc.ublock_rt, q_desc.mblock_m, q_desc.mblock_n, q_desc.t, q_desc.tile_height, q_desc.tile_width);
    md.shape.rt = q_desc.ublock_rt * q_desc.mblock_m * q_desc.bufq_grid_dim_r;
    md.shape.ct = q_desc.ublock_ct * q_desc.mblock_n * q_desc.bufq_grid_dim_c;
    md.shape.z  = q_desc.t;
    md.shape.w  = in.buf_size_bytes / block_size;
    md.shape.tile_height =  q_desc.tile_height;
    md.shape.tile_width = q_desc.tile_width;
    md.is_tilized = true;
    md.data_format = q_desc.bufq_target_format;
    tt_tensor tensor(md);
    tensor.reserve_tile_tensor();

    for(uint32_t alloc_idx = 0; alloc_idx < q_desc.bufq_grid_dim_r * q_desc.bufq_grid_dim_c; alloc_idx++) {
        uint32_t buf_offset = alloc_idx * in.buf_size_bytes / sizeof(uint32_t);
        uint32_t next_buf_start = buf_offset + in.buf_size_bytes / sizeof(uint32_t);
        vector<uint32_t> buf_data(reinterpret_cast<const uint32_t*>(in.ptr) + buf_offset, reinterpret_cast<const uint32_t*>(in.ptr) + next_buf_start);
        for(uint32_t w = 0; w < tensor.getw(); w++) {
            uint32_t offset = 0;
            for(uint32_t z = 0; z < tensor.getz(); z++) {
                if(ublock_scan == Dim::R) {
                    for(uint32_t ubr = 0; ubr < q_desc.mblock_m; ubr++) {
                        for(uint32_t ubc = 0; ubc < q_desc.mblock_n; ubc++) {
                            get_ublock_data(q_desc, tensor, buf_data, w * (block_size/sizeof(uint32_t)), alloc_idx, offset, w, z, ubr, ubc);
                        }
                    }
                } else if (ublock_scan == Dim::C) {
                    for(uint32_t ubc = 0; ubc < q_desc.mblock_n; ubc++) {
                        for(uint32_t ubr = 0; ubr < q_desc.mblock_m; ubr++) {
                            get_ublock_data(q_desc, tensor, buf_data, w * (block_size/sizeof(uint32_t)), alloc_idx, offset, w, z, ubr, ubc);
                        }
                    }
                } else {
                    log_fatal("Invalid ublock scan order");
                }
            }
        }
    }
    tensor.set_is_tilized(true); // tt_tensor is tilized
    return tensor;
}
}  // namespace tt::io
