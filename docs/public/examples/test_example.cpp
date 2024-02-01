// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <memory>
#include <vector>

#include "tt_backend.hpp"
#include "tt_backend_api.hpp"
#include "tt_backend_api_types.hpp"

// Populate a queue descriptor from a queue name
tt::tt_dram_io_desc get_queue_descriptor(const std::shared_ptr<tt_backend> &backend, const std::string &queue_name);

// Populate a tensor descriptor from a queue name
tt::tt_PytorchTensorDesc get_tensor_descriptor(const std::string &name);

int main(int argc, char **argv) {
    // Define path to pre-compiled model and output artifacts
    std::string output_path = "tt_build/base_encoders";
    std::string model_path = "base_encoders.tti";

    // Create a pre-compiled model object and a backend object from it using default config
    std::shared_ptr<tt::tt_device_image> model = std::make_shared<tt::tt_device_image>(model_path, output_path);
    std::shared_ptr<tt_backend> backend = tt_backend::create(model, tt::tt_backend_config{});

    // The following code are organized into <runtime process> and <io process> sections
    // where the two processes can be running on different user spaces (e.g. host and soc)

    // <runtime process> - Initialize the backend
    if (backend->initialize() != tt::DEVICE_STATUS_CODE::Success) {
        throw std::runtime_error("Failed to initialize device");
    }

    // The following code must execute between initialize() and finish()
    {
        // <io process> - Push a microbatch of inputs to device
        for (const std::string &name : model->get_graph_input_names()) {
            tt::tt_dram_io_desc io_desc = get_queue_descriptor(backend, name);
            tt::tt_PytorchTensorDesc tensor_desc = get_tensor_descriptor(name);

            // DMA the input tensor from host to device
            assert(tt::backend::push_input(io_desc, tensor_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);
        }

        // <runtime process> - Run inference program, p_loop_count is the number of microbatches executed
        std::map<std::string, std::string> program_parameters = {{"$p_loop_count", "1"}};
        backend->run_program("run_fwd_0", program_parameters);

        // <io process> - Pop a microbatch of outputs from device
        for (const std::string &name : model->get_graph_output_names()) {
            tt::tt_dram_io_desc io_desc = get_queue_descriptor(backend, name);
            tt::tt_PytorchTensorDesc tensor_desc = {};  // passed into get_tensor below to be populated

            // DMA the output tensor from device to host
            assert(tt::backend::get_output(io_desc, tensor_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);

            // Device memory management
            // - pop releases storage on device (queue entries popped), device can push more outputs to queue
            assert(tt::backend::pop_output(io_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);

            // Host memory management
            // - free releases storage on host (tensor data freed), host is done with the output data
            std::cout << "Output tensor " << name << " data ptr: " << tensor_desc.ptr << std::endl;
            assert(tt::backend::free_tensor(tensor_desc) == tt::DEVICE_STATUS_CODE::Success);
        }
    }

    // <runtime process> - Teardown the backend
    if (backend->finish() != tt::DEVICE_STATUS_CODE::Success) {
        throw std::runtime_error("Failed to shutdown device");
    }
    return 0;
}

// Populate an queue descriptor from a queue name
tt::tt_dram_io_desc get_queue_descriptor(const std::shared_ptr<tt_backend> &backend, const std::string &queue_name) {
    tt::tt_dram_io_desc queue_desc = backend->get_queue_descriptor(queue_name);
    // (optional) maps device address to a contiguous user-space address in tt::tt_dram_io_desc::bufq_mapping
    // - push_input will use this mapping for memcpy-based fast DMA if it exists
    // - push_input will use user-mode driver for DMA if mapping does not exist
    tt::backend::translate_addresses(queue_desc);
    return queue_desc;
}

// Populate a tensor descriptor from raw data + metadata
template <class T>
tt::tt_PytorchTensorDesc to_tensor_descptor(
    const T *array,
    unsigned int w_dim,
    unsigned int z_dim,
    unsigned int r_dim,
    unsigned int c_dim,
    tt::DataFormat format,
    unsigned int dim = tt::PY_TENSOR_DIMS) {
    tt::tt_PytorchTensorDesc tensor_desc;
    tensor_desc.owner = tt::OWNERSHIP::Backend;
    tensor_desc.ptr = array;
    tensor_desc.itemsize = sizeof(T);
    tensor_desc.format = format;
    tensor_desc.shape = {w_dim, z_dim, r_dim, c_dim};
    tensor_desc.dim = dim;

    tensor_desc.strides[3] = sizeof(T);
    tensor_desc.strides[2] = c_dim * tensor_desc.strides[3];
    tensor_desc.strides[1] = r_dim * tensor_desc.strides[2];
    tensor_desc.strides[0] = z_dim * tensor_desc.strides[1];
    return tensor_desc;
}

// Populate a tensor descriptor from a queue name
tt::tt_PytorchTensorDesc get_tensor_descriptor(const std::string &name) {
    // The following code is an example for BERT base encoder input:
    // - activation: [microbatch, channels = 1, height = 128, width = 768]
    // - atten_mask: [microbatch, channels = 1, height = 32, width = 128]
    if (name == "input_1") {
        static std::vector<uint16_t> tensor_data(128 * 1 * 128 * 768, 0);
        return to_tensor_descptor<uint16_t>(tensor_data.data(), 128, 1, 128, 768, tt::DataFormat::Float16_b);
    } else if (name == "attention_mask") {
        static std::vector<uint16_t> tensor_data(128 * 1 * 32 * 128, 0);
        return to_tensor_descptor<uint16_t>(tensor_data.data(), 128, 1, 32, 128, tt::DataFormat::Float16_b);
    }
    throw std::runtime_error("Tensor is not a valid input");
}
