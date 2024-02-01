// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
namespace tt::io::utils {

// Populate an queue descriptor from a queue name
tt::tt_dram_io_desc get_queue_descriptor(const std::shared_ptr<tt_backend> &backend, const std::string &queue_name) {
    tt::tt_dram_io_desc queue_desc = backend->get_queue_descriptor(queue_name);
    // (optional) maps device address to a contiguous user-space address in tt::tt_dram_io_desc::bufq_mapping
    // - push_input will use this mapping for memcpy-based fast DMA if it exists
    // - push_input will use user-mode driver for DMA if mapping does not exist
    tt::backend::translate_addresses(queue_desc);
    return queue_desc;
}

// Populate a tensor descriptor with appropriate metadata derived from the model
// The returned tensor will be empty and needs to be filled with data (i.e. its ptr will need to point to valid memory)
tt::tt_PytorchTensorDesc to_tensor_descriptor(
    unsigned int w_dim,
    unsigned int z_dim,
    unsigned int r_dim,
    unsigned int c_dim,
    tt::DataFormat format,
    unsigned int dim = tt::PY_TENSOR_DIMS) {
    tt::tt_PytorchTensorDesc tensor_desc;
    tensor_desc.owner = tt::OWNERSHIP::Backend;
    tensor_desc.ptr = nullptr;
    tensor_desc.itemsize = tt::backend::sizeof_format(format);
    tensor_desc.format = format;
    tensor_desc.shape = {w_dim, z_dim, r_dim, c_dim};
    tensor_desc.dim = dim;

    tensor_desc.strides[3] = tt::backend::sizeof_format(format);
    tensor_desc.strides[2] = c_dim * tensor_desc.strides[3];
    tensor_desc.strides[1] = r_dim * tensor_desc.strides[2];
    tensor_desc.strides[0] = z_dim * tensor_desc.strides[1];
    return tensor_desc;
}
std::tuple<unsigned int, unsigned int, unsigned int, unsigned int> get_tensor_shape_from_model(
    const std::string& name,
    std::shared_ptr<tt::tt_device_image> model,
    const tt::tt_dram_io_desc& queue_desc) {
    unsigned int microbatch_size = queue_desc.input_count;
    unsigned int z, y, x;
    if (queue_desc.s_descriptor.stride > 0) {
        z = std::get<0>(model -> get_io_prestride_map().at(name));
        y = std::get<1>(model -> get_io_prestride_map().at(name));
        x = std::get<2>(model -> get_io_prestride_map().at(name));
    }
    else {
        z = queue_desc.t / (queue_desc.hstack_factor * queue_desc.vstack_factor);
        y = queue_desc.bufq_grid_dim_c * queue_desc.mblock_m * queue_desc.ublock_rt * queue_desc.vstack_factor * queue_desc.tile_height;
        x = queue_desc.bufq_grid_dim_r * queue_desc.mblock_n* queue_desc.ublock_ct * queue_desc.hstack_factor * queue_desc.tile_width;
    }
    return {microbatch_size, z, y, x};
}

// Converts a device format into a pytorch supported format that's lossless with the minimal storage requirements
tt::DataFormat get_pytorch_tensor_format_from_queue(const tt::tt_dram_io_desc& queue_desc) {
    switch (queue_desc.bufq_target_format) {
        case tt::DataFormat::Float32:
            return tt::DataFormat::Float32;
        case tt::DataFormat::RawUInt32:
            return tt::DataFormat::RawUInt32;
        case tt::DataFormat::RawUInt16:
            return tt::DataFormat::RawUInt16;
        case tt::DataFormat::RawUInt8:
            return tt::DataFormat::RawUInt8;
        case tt::DataFormat::Int8:
            return tt::DataFormat::Int8;
        case tt::DataFormat::UInt8:
            return tt::DataFormat::UInt8;
        case tt::DataFormat::Int32:
            return tt::DataFormat::Int32;
        case tt::DataFormat::Float16_b:
        case tt::DataFormat::Bfp8_b:
        case tt::DataFormat::Bfp4_b:
        case tt::DataFormat::Bfp2_b:
            return tt::DataFormat::Float16_b;
        case tt::DataFormat::Float16:
        case tt::DataFormat::Bfp8:
        case tt::DataFormat::Bfp4:
        case tt::DataFormat::Bfp2:
            return tt::DataFormat::Float16;
        default: throw std::string("Invalid Tensor data format"); break;
    }
    return tt::DataFormat::Invalid;
}

// Fill a tensor descriptor with data. We choose to populate it with data in persistent memory, allocated by backend.
// The user can choose to populate the descriptor with data from any source (ex: images/text tokens read from disk).
void fill_tensor_with_data(const std::string& name, tt::tt_PytorchTensorDesc& tensor) {
    uint32_t num_elements = tensor.shape.at(3) * tensor.shape.at(2) * tensor.shape.at(1) * tensor.shape.at(0);
    auto mem = tt::backend::allocate_memory_for_tensor(tensor.format, num_elements);
    // Populating tensor with garbage data. The user can choose to fill up this memory with actual inputs.
    tensor.ptr = mem -> data();
}

// Generate a Pytorch Tensor Descriptor Structure for an activation input, based on the model configuration
tt::tt_PytorchTensorDesc get_tensor_descriptor(const std::string &name, const std::shared_ptr<tt::tt_device_image> model, const tt::tt_dram_io_desc& queue_desc) {
    const auto tensor_shape = get_tensor_shape_from_model(name, model, queue_desc);
    const tt::DataFormat format = get_pytorch_tensor_format_from_queue(queue_desc);
    return to_tensor_descriptor(std::get<0>(tensor_shape), std::get<1>(tensor_shape), std::get<2>(tensor_shape), std::get<3>(tensor_shape), format);
}
}