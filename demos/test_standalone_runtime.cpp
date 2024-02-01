// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <memory>
#include <vector>
#include <experimental/filesystem>

#include "tt_backend.hpp"
#include "tt_backend_api.hpp"
#include "tt_backend_api_types.hpp"
#include "io_utils.h"

namespace fs = std::experimental::filesystem; 

int main(int argc, char **argv) {

    if (argc <= 1) {
        throw std::runtime_error("TTI path not specified on the command line");
    }
    else if (argc > 3) {
        throw std::runtime_error("Incorrect number of arguments specified to inference harness. Supported args: TTI_PATH NUM_INFERENCE_LOOPS");
    }

    // Define path to pre-compiled model and output artifacts
    std::string output_path = "tt_build/test_standalone_runtime";
    fs::create_directories(output_path);
    uint32_t inference_loops = 1;
    std::string model_path = argv[1];  // eg. "/home_mnt/software/spatial2/backend/binaries/CI_TTI_TEST_BINARIES_WH/bert.tti"

    if (argc == 3) {
        inference_loops = std::stoi(argv[2]);
    }

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
    for (uint32_t i = 0; i < inference_loops; i++) {
        // <io process> - Push a microbatch of inputs to device
        for (const std::string &name : model->get_graph_input_names()) {
            tt::tt_dram_io_desc io_desc = tt::io::utils::get_queue_descriptor(backend, name);
            tt::tt_PytorchTensorDesc tensor_desc = tt::io::utils::get_tensor_descriptor(name, model, io_desc);
            // Fill the tensor descriptor with data. We choose to allocate dummy memory using the TT backend for this tensor.
            // The user is free to use previously allocated memory, or use the backend to allocate memory that is then filled with actual data.
            tt::io::utils::fill_tensor_with_data(name, tensor_desc);
            // DMA the input tensor from host to device
            assert(tt::backend::push_input(io_desc, tensor_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);
            // Optional: Host memory management
            // - free releases storage on host (tensor data freed), since host is done with pushing data for this activation
            // - The user can choose not to free this memory and use it even after the data is in device DRAM 
            std::cout << "Pushed Input tensor " << name << " data ptr: " << tensor_desc.ptr << std::endl;
            assert(tt::backend::free_tensor(tensor_desc) == tt::DEVICE_STATUS_CODE::Success);
        }

        // <runtime process> - Run inference program, p_loop_count is the number of microbatches executed
        std::map<std::string, std::string> program_parameters = {{"$p_loop_count", "1"}};
        for (const auto& prog_name : backend -> get_programs()) {
            assert(backend->run_program(prog_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success);
        }
        
        // <io process> - Pop a microbatch of outputs from device
        for (const std::string &name : model->get_graph_output_names()) {
            tt::tt_dram_io_desc io_desc = tt::io::utils::get_queue_descriptor(backend, name);
            tt::tt_PytorchTensorDesc tensor_desc = {};  // passed into get_tensor below to be populated

            // DMA the output tensor from device to host
            assert(tt::backend::get_output(io_desc, tensor_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);

            // Device memory management
            // - pop releases storage on device (queue entries popped), device can push more outputs to queue
            assert(tt::backend::pop_output(io_desc, false, 1) == tt::DEVICE_STATUS_CODE::Success);

            // Host memory management
            // - free releases storage on host (tensor data freed), host is done with the output data
            // - The user can choose not to free this memory and use it for downstream tasks 
            std::cout << "Got Output tensor " << name << " data ptr: " << tensor_desc.ptr << std::endl;
            assert(tt::backend::free_tensor(tensor_desc) == tt::DEVICE_STATUS_CODE::Success);
        }
    }
    // <runtime process> - Teardown the backend
    if (backend->finish() != tt::DEVICE_STATUS_CODE::Success) {
        throw std::runtime_error("Failed to shutdown device");
    }
    return 0;
}