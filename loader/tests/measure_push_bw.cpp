// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <cstdlib>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

// --netlist --num-loops --host-data-format

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z = queue_info.dim.t;
    md.shape.w = batched ? queue_info.input_count : 1;
    md.is_tilized = true;

    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    tt_tensor tensor(md);
    tensor.init_to_xyz_values();
    return tensor;
}

int get_queue_size(tt_queue_info &queue_info, bool is_tilized, uint32_t tile_height = 32, uint32_t tile_width = 32) {
    int num_buffers = queue_info.grid_size.r * queue_info.grid_size.c;
    int buffer_size = tt::backend::get_io_size_in_bytes(
        queue_info.data_format,
        !is_tilized,
        queue_info.dim.ublock_ct,
        queue_info.dim.ublock_rt,
        queue_info.dim.mblock_m,
        queue_info.dim.mblock_n,
        queue_info.dim.t,
        queue_info.entries,
        tile_height,
        tile_width);
    return buffer_size * num_buffers;
}

int main(int argc, char **argv) {
    std::vector<std::string> args(argv, argv + argc);
    std::string netlist_path;
    int num_loops;
    int stride;
    std::vector<int> input_shape;
    std::string host_format_;
    std::string arch_name;
    std::string cmd_line_args;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "");
    std::tie(num_loops, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-loops", 1);
    std::tie(stride, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--stride", 0);
    std::tie(cmd_line_args, args) = verif_args::get_command_option_and_remaining_args(args, "--input-tensor-shape", "");
    verif_args::split_string_into_vector(input_shape, cmd_line_args, ",");
    std::tie(host_format_, args) = verif_args::get_command_option_and_remaining_args(args, "--host-data-format", "QueueFormat");
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");

    tt_backend_config config = {
        .type = tt::DEVICE::Silicon,
        .arch = arch_name == "grayskull" ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
        .optimization_level = 0,
        .output_dir = output_dir,
    };

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);

    auto runtime = std::dynamic_pointer_cast<tt_runtime>(backend_api);
    float average_push_bw = 0;

    vector<tt_dram_io_desc> input_io_desc;
    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);
    cluster -> start_device(runtime -> config.device_params);

    for (const auto &it : workload.queues) {
        if (it.second.my_queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            input_io_desc.push_back(io_desc);
        }
    }

    auto& io_desc = input_io_desc[0];

    if(stride > 0) {
        io_desc.s_descriptor.stride  = stride;
        for(int x = 0; x < stride; x++) {
            for(int y = 0; y < stride; y++){
                io_desc.s_descriptor.xy_offsets.push_back({x, y});
            }
        }
    }

    tt_queue_info &queue_info = workload.queues[io_desc.queue_name].my_queue_info;
    int push_size = queue_info.input_count;
    DataFormat host_format = host_format_ == "QueueFormat" ? queue_info.data_format : verif::STRING_TO_DATA_FORMAT.at(host_format_);

    bool is_tilized = workload.has_tilized_data(queue_info.name);
    int queue_size = get_queue_size(queue_info, is_tilized);  // uses pybuda api
    int tensor_size = get_tensor_size_in_bytes(queue_info, is_tilized);
    int header_size = queue_info.grid_size.r * queue_info.grid_size.c * tt::io::QUEUE_HEADER_SIZE_BYTES;
    log_assert(tensor_size * queue_info.entries + header_size == queue_size, "Tensor anf queue size mismatch");  // check size consistency manual vs. pybuda api

    tt_PytorchTensorDesc pytorch_tensor;
    aligned_vector<float> fp32_data;
    aligned_vector<uint16_t> fp16_data;
    aligned_vector<int8_t> int8_data;

    if(stride > 0) {
        uint32_t itemsize = 0;
        if(host_format == DataFormat::Float32) {
            fp32_data = aligned_vector<float>(input_shape[0] * input_shape[1] * input_shape[2] * input_shape[3], 0);
            for(int i = 0; i < fp32_data.size(); i++) {
                fp32_data[i] = i;
            }
            pytorch_tensor.ptr = fp32_data.data();
            itemsize = sizeof(float);
        }
        else if(host_format == DataFormat::Float16 || host_format == DataFormat::Float16_b) {
            fp16_data = aligned_vector<uint16_t>(input_shape[0] * input_shape[1] * input_shape[2] * input_shape[3], 0);
            for(int i = 0; i < fp16_data.size(); i++) {
                fp16_data[i] = i;
            }
            pytorch_tensor.ptr = fp16_data.data();
            itemsize = sizeof(uint16_t);
        }
        else {
            log_fatal("Convolution Shuffling is only supported for FP32/FP16/Fp16_b host formats!");
        }
        tt::io::set_py_tensor_metadata(pytorch_tensor, {static_cast<uint32_t>(input_shape[0]), static_cast<uint32_t>(input_shape[1]), static_cast<uint32_t>(input_shape[2]), static_cast<uint32_t>(input_shape[3])}, itemsize, tt::PY_TENSOR_DIMS, host_format);
    }
    else {
        tt_tensor input = get_tilized_tensor(queue_info, 0, true, host_format);

        if(input.get_data_format() == DataFormat::Float32) {
            pytorch_tensor = tt::io::get_pytorch_tensor_desc<float>(input, fp32_data);
        } else if(input.get_data_format() == DataFormat::Int8) {
            pytorch_tensor = tt::io::get_pytorch_tensor_desc<int8_t>(input, int8_data);
        } else {
            pytorch_tensor = tt::io::get_pytorch_tensor_desc<uint16_t>(input, fp16_data);
        }
    }
    pytorch_tensor.owner = tt::OWNERSHIP::Frontend;
    int64_t push_bytes = push_size * tensor_size;

    for (int loop = 0; loop < num_loops; loop++) {
        runtime -> loader -> create_and_allocate_io_queues(workload.queues);
        if(loop > 0) {
            if(tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::exists(io_desc.queue_name)) {
                tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::get(io_desc.queue_name) -> overwrite_wptr(0);
            }
            else if(tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(io_desc.queue_name)) {
                tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::get(io_desc.queue_name) -> overwrite_wptr(0);
            }
            else {
                log_fatal("Not using MMIO Push or Device Push!");
            }
        }
        int64_t push_duration = 0;
        auto push_start = std::chrono::high_resolution_clock::now();
        auto push_status = tt::backend::push_input(io_desc, pytorch_tensor, false, 10);
        log_assert(push_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on this push");
        auto push_end = std::chrono::high_resolution_clock::now();
        push_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(push_end - push_start).count();
        float push_bandwidth = ((float)push_bytes / (1024 * 1024 * 1024)) / ((float)push_duration / 1.0e9);
        // log_info(tt::LogTest, "Push Size:         \t{} MB", push_bytes / (1024 * 1024));
        // log_info(tt::LogTest, "Push Elapsed Time: \t{} us", push_duration);
        // log_info(tt::LogTest, "Push Bandwidth:    \t{:3.3f} GB/s", push_bandwidth);
        // log_info(tt::LogTest, "Batch Size:        \t{}", push_size);
        log_info(tt::LogTest, "Push Samples/sec:  \t{:3.0f}", ((float)push_size) / ((float)push_duration / 1.0e9));

        if(loop) average_push_bw += push_bandwidth;
    }
    std::cout << "Average Push BW: " << float(average_push_bw / (num_loops - 1));
    return 0;
}