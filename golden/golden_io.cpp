// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "golden_io.hpp"

#include "common/cache_lib.hpp"
#include "golden_utils.hpp"
#include "common/io_lib.hpp"
#include "common/data_binary_lib.hpp"

namespace tt::golden::io {

golden_workload_data *get_workload(const std::string tag) {
    golden_workload_data *workload;
    if (tt_object_cache<golden_workload_data>::exists(tag)) {
        workload = tt_object_cache<golden_workload_data>::get(tag);
    } else {
        workload = new golden_workload_data(tag);
        tt_object_cache<golden_workload_data>::set(tag, workload);
    }
    return workload;
}
tt_golden_config *get_config(const std::string tag) {
    tt_golden_config *config;
    if (tt_object_cache<tt_golden_config>::exists(tag)) {
        config = tt_object_cache<tt_golden_config>::get(tag);
    } else {
        config = new tt_golden_config();
        tt_object_cache<tt_golden_config>::set(tag, config);
    }
    return config;
}
void free_object_cache() {
    // it is safe to destroy an empty cache, hence a super set is used
    tt_object_cache<tt_golden_config>::destroy();
    tt_object_cache<golden_workload_data>::destroy();
}

void push_input(
    const tt::tt_dram_io_desc &q_desc, 
    const tt::tt_TilizedTensorDesc &tilized_tensor, 
    const int timeout_in_seconds, 
    const int ram_ptr) {
        log_assert(q_desc.layout == IO_LAYOUT::Tilized, "Golden supports tt_TilizedTensorDesc pushing only for queues with tilized layout");
        auto workload = tt::golden::io::get_workload(q_desc.netlist_path);
        tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
        tt_tensor tensor = tt::io::tilized_tensor_to_tt_tensor(tilized_tensor, q_desc, workload -> get_ublock_scan(q_info.name));
        vector<tt_tensor> input_tensors = tensor.wsplit(false);
        for (const auto& tensor_entry : input_tensors) {
            push_input(q_desc.netlist_path, q_info, std::make_shared<tt_tensor>(tensor_entry), ram_ptr);
        }
}
//! Interface Functions for queues
void push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool push_one,
    const int timeout_in_seconds,
    const int ptr) {
    // Initialize variables for convolution shuffler and perform perstriding if required
    tt_PytorchTensorDesc shuffled_tensor(py_tensor_desc);
    aligned_vector<uint16_t> shuffled_data_half = {};
    aligned_vector<uint32_t> shuffled_data = {};
    tt::io::slow_prestride(py_tensor_desc, shuffled_tensor, q_desc, shuffled_data_half, shuffled_data);
    // Construct the input float vector from py_tensor_desc
    log_assert(
        ((shuffled_tensor.format == DataFormat::Float32) || (shuffled_tensor.format == DataFormat::Float16) ||
         (shuffled_tensor.format == DataFormat::Float16_b) || (shuffled_tensor.format == DataFormat::RawUInt32) ||
         (shuffled_tensor.format == DataFormat::RawUInt16) || (shuffled_tensor.format == DataFormat::Int32) ||
         (shuffled_tensor.format == DataFormat::Int8) || (shuffled_tensor.format == DataFormat::UInt8)) || (shuffled_tensor.format == DataFormat::UInt16),
        "Input pytorch tensor can only be FP32/FP16/FP16_b/Raw32/Raw16/Int32/Int8/UInt8/UInt16");
    auto config = tt::golden::io::get_config(q_desc.netlist_path);
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    // int num_entries = push_one ? 1 : q_info.input_count;
    int num_entries = shuffled_tensor.shape.at(0);
    log_assert(
        !(num_entries % q_info.input_count) || num_entries == 1,
        "w_dim={} must be an integer multiple of q_info.input_count={} or be 1 if pushing one by one for queue={}",
        shuffled_tensor.shape.at(0),
        q_info.input_count,
        q_desc.queue_name);

    int py_tensor_num_elements =
        num_entries * shuffled_tensor.shape.at(1) * shuffled_tensor.shape.at(2) * shuffled_tensor.shape.at(3);
    uint32_t py_tensor_num_bytes = py_tensor_num_elements * shuffled_tensor.itemsize;
    const uint32_t *data = static_cast<const uint32_t *>(shuffled_tensor.ptr);
    uint32_t data_len = py_tensor_num_bytes / sizeof(uint32_t);
    std::vector<float> data_vector = tt::io::process_raw_untilized(data, data_len, shuffled_tensor.format);
    tt::data_binary::dump_file_on_env_flags(q_info.name, data_vector);
    // Checks
    auto md = get_tensor_metadata_from_tt_queue_info(q_info, !push_one);
    md.shape.w = num_entries;  // accounts for the case where we push all entries
    if (py_tensor_num_elements != num_entries * get_tensor_size_in_elements(q_info)) {
        log_error(
            "Queue and Pytorch Tensor number of elements mismatched -- py_tensor_num_elements={} "
            "get_tensor_size_in_elements(q_info)={}, num_entries={}",
            py_tensor_num_elements,
            get_tensor_size_in_elements(q_info),
            num_entries);
        log_error(
            "Queue and Pytorch Tensor number of elements mismatched -- q_desc Info {} and tt_PytorchTensorDesc "
            "shape [{},{},{},{}] -- dim={} mismatched",
            q_info,
            shuffled_tensor.shape.at(0),
            shuffled_tensor.shape.at(1),
            shuffled_tensor.shape.at(2),
            shuffled_tensor.shape.at(3),
            shuffled_tensor.dim);
        throw std::runtime_error("Queue and Pytorch Tensor number of elements mismatched");
    }
    // Convert to tilized tensor
    tt_tensor tensor_input(md);
    if (config->ignore_data_format_precision) {
        md.data_format = DataFormat::Float32;
    }
    tensor_input.set_is_tilized(false);
    tensor_input.flat_tensor_data = std::move(data_vector);
    tensor_input.tilize_inplace(true, false, q_desc.layout == IO_LAYOUT::Flat);
    if (q_desc.layout != IO_LAYOUT::Flat) {
        tensor_input.set_is_tilized(true);
    }

    if (config->ignore_data_format_precision) {
        md.data_format = q_info.data_format;
    } else {
        tensor_input.adjust_tensor_for_accuracy(false);
    }
    // Push to golden
    log_debug(tt::LogGolden, "Pushing Input Queue = {}", q_info);
    log_trace(tt::LogGolden, "Tensor Dump {}", tensor_input.get_string());
    if (tensor_input.getw() == 1) {
        tt::golden::io::push_input(q_desc.netlist_path, q_info, std::make_shared<tt_tensor>(std::move(tensor_input)), ptr);
    } else {
        vector<tt_tensor> input_tensors = tensor_input.wsplit(false);
        for (tt_tensor &input_tensor : input_tensors) {
            tt::golden::io::push_input(q_desc.netlist_path, q_info, std::make_shared<tt_tensor>(std::move(input_tensor)), ptr);
        }
    }
}

void pop_output(const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds) {
    std::string netlist_path = q_desc.netlist_path;
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    // Pop from golden
    log_debug(tt::LogGolden, "Popping Output Queue = {}", q_info);
    int num_entries = pop_one ? 1 : q_info.input_count;
    for (int i = 0; i < num_entries; i++) {
        tt::golden::io::pop_output(netlist_path, q_info);
    }
}
void get_output(
    const tt::tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool get_one,
    const int timeout_in_seconds,
    const int ptr) {
    std::string netlist_path = q_desc.netlist_path;
    auto workload = tt::golden::io::get_workload(netlist_path);
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    // Get from golden
    log_debug(tt::LogGolden, "Popping Output Queue = {}", q_info);
    int num_entries = get_one ? 1 : q_info.input_count;
    vector<tt_tensor> output_tensors;
    if (q_info.type == IO_TYPE::Queue) {
        for (int i = 0; i < num_entries; i++) {
            int rd_ptr = 0;
            rd_ptr = workload->queues[q_info.name].my_io->get_global_rd_ptr() + i;
            auto output_tensor = tt::golden::io::get_output(netlist_path, q_info, rd_ptr);
            output_tensors.push_back(output_tensor->copy(false));
        }
    } else if (q_info.type == IO_TYPE::RandomAccess) {
        auto output_tensor = tt::golden::io::get_output(netlist_path, q_info, ptr);
        output_tensors.push_back(output_tensor->copy(false));
    } else {
        log_fatal("get_output IO_TYPE is invalid");
    }
    // w-slices stacking oepration
    tt_tensor result = tt_tensor::wstack(output_tensors);
    // t-slices stacking operation
    if (q_desc.hstack_factor > 1 or q_desc.vstack_factor > 1) {
        log_assert(q_desc.t % q_desc.hstack_factor == 0,  "hstack factor must divide t evenly!");
        log_assert(q_desc.t % q_desc.vstack_factor == 0,  "vstack factor must divide t evenly!");
        if (q_desc.stack_row_major) {
            result =
                result.reshape_z_dim_into_c_dim(q_desc.hstack_factor).reshape_z_dim_into_r_dim(q_desc.vstack_factor);
        } else {
            result =
                result.reshape_z_dim_into_r_dim(q_desc.vstack_factor).reshape_z_dim_into_c_dim(q_desc.hstack_factor);
        }
    }
    auto output_tensor = std::make_shared<tt_tensor>(result);
    log_trace(tt::LogGolden, "Golden Tensor Dump {}", output_tensor->get_string());
    // Convert the tensor to untilized format and construct pytorch tensor desc
    tt::golden::io::convert_tt_tensor_to_py_tensor_desc(netlist_path, output_tensor, q_desc, py_tensor_desc, !get_one);
}

void convert_tt_tensor_to_py_tensor_desc(
    const std::string netlist_path,
    std::shared_ptr<tt_tensor> tensor,
    const tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool batched) {
    const tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    log_assert(
        tt::io::get_pytorch_tensor_format(q_info.data_format) != DataFormat::Invalid,
        "Must be a supported device to pytorch format conversion");
    log_assert(q_desc.t % q_desc.hstack_factor == 0, "hstack factor must divide t evenly!");
    log_assert(q_desc.t % q_desc.vstack_factor == 0, "vstack factor must divide t evenly!");

    auto workload = tt::golden::io::get_workload(netlist_path);
    auto config = tt::golden::io::get_config(netlist_path);
    auto md = get_tensor_metadata_from_tt_queue_info(q_info, batched);
    int num_batched_entries = (batched ? q_info.input_count : 1);
    int num_elements_tensor = num_batched_entries * get_tensor_size_in_elements(q_info);
    auto allocated_queue = workload->allocate_untilized_memory(q_info.name, num_batched_entries);
    // Convert the tensor to untilized format and construct pytorch tensor desc
    DataFormat queue_tensor_format = q_info.data_format;
    if (config->ignore_data_format_precision) {
        queue_tensor_format = DataFormat::Float32;
    }
    tensor->metadata.data_format = queue_tensor_format;  // Force output_queue same format as specified in netlist
    if (queue_tensor_format == DataFormat::Float32) {
        vector<float> float_vector;
        tensor->untilize_to_flat_tensor_data(true, false, q_desc.layout == IO_LAYOUT::Flat, float_vector);
        std::memcpy(allocated_queue->data(), float_vector.data(), num_elements_tensor * 4);
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<float>(
            reinterpret_cast<const float *>(allocated_queue->data()),
            md.shape.w,
            md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor),
            md.shape.rt * q_desc.vstack_factor,
            md.shape.ct * q_desc.hstack_factor,
            queue_tensor_format,
            py_tensor_desc.dim,
            md.shape.tile_height,
            md.shape.tile_width);
    } else if (tensor -> metadata.data_format == DataFormat::Int8) {
        vector<int8_t> byte_vector;
        tensor->untilize_to_flat_tensor_data_byte(true, false, byte_vector);
        std::memcpy(allocated_queue->data(), byte_vector.data(), num_elements_tensor);
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int8_t>(
            reinterpret_cast<const int8_t *>(allocated_queue->data()),
            md.shape.w,
            md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor),
            md.shape.rt * q_desc.vstack_factor,
            md.shape.ct * q_desc.hstack_factor,
            queue_tensor_format,
            py_tensor_desc.dim,
            md.shape.tile_height,
            md.shape.tile_width);
    } else if (tensor -> metadata.data_format == DataFormat::UInt8) {
        vector<uint8_t> byte_vector;
        tensor->untilize_to_flat_tensor_data_unsigned_byte(true, false, byte_vector);
        std::memcpy(allocated_queue->data(), byte_vector.data(), num_elements_tensor);
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint8_t>(
            reinterpret_cast<const uint8_t *>(allocated_queue->data()),
            md.shape.w,
            md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor),
            md.shape.rt * q_desc.vstack_factor,
            md.shape.ct * q_desc.hstack_factor,
            queue_tensor_format,
            py_tensor_desc.dim,
            md.shape.tile_height,
            md.shape.tile_width);
    } else if (tensor -> metadata.data_format == DataFormat::Int32) {
        vector<int32_t> dword_vector;
        tensor->untilize_to_flat_tensor_data_dword(true, false, dword_vector);
        std::memcpy(allocated_queue->data(), dword_vector.data(), num_elements_tensor * 4);
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<int32_t>(
            reinterpret_cast<const int32_t *>(allocated_queue->data()),
            md.shape.w,
            md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor),
            md.shape.rt * q_desc.vstack_factor,
            md.shape.ct * q_desc.hstack_factor,
            queue_tensor_format,
            py_tensor_desc.dim,
            md.shape.tile_height,
            md.shape.tile_width);
    } else {
        vector<uint16_t> half_float_vector;
        tensor->untilize_to_flat_tensor_data_half(true, false, half_float_vector);
        std::memcpy(allocated_queue->data(), half_float_vector.data(), num_elements_tensor * 2);
        py_tensor_desc = tt::io::get_pytorch_tensor_desc_from_array<uint16_t>(
            reinterpret_cast<const uint16_t *>(allocated_queue->data()),
            md.shape.w,
            md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor),
            md.shape.rt * q_desc.vstack_factor,
            md.shape.ct * q_desc.hstack_factor,
            queue_tensor_format,
            py_tensor_desc.dim,
            md.shape.tile_height,
            md.shape.tile_width);
    }
    if (config->ignore_data_format_precision) {
        tensor->metadata.data_format = q_info.data_format;
    }

    if (std::getenv("TT_BACKEND_CAPTURE_TENSORS") != nullptr) {
        vector<float> float_vector;
        tensor->untilize_to_flat_tensor_data(true, false, q_desc.layout == IO_LAYOUT::Flat, float_vector);
        tt::data_binary::dump_file_on_env_flags(q_info.name, float_vector);
    }
    [[maybe_unused]] int py_tensor_num_elements = py_tensor_desc.shape.at(1) * py_tensor_desc.shape.at(2) * py_tensor_desc.shape.at(3);
    log_debug(
        tt::LogGolden,
        "py_tensor_num_elements={} num_batched_entries={}, get_tensor_size_in_elements(q_info)={}",
        py_tensor_num_elements,
        num_batched_entries,
        get_tensor_size_in_elements(q_info));
    log_debug(
        tt::LogGolden,
        "q_desc Info {} and tt_PytorchTensorDesc shape [{},{},{},{}], dim={}",
        q_info,
        py_tensor_desc.shape.at(0),
        py_tensor_desc.shape.at(1),
        py_tensor_desc.shape.at(2),
        py_tensor_desc.shape.at(3),
        py_tensor_desc.dim);
}

void push_input(
    const std::string netlist_path, const tt_queue_info &q_info, std::shared_ptr<tt_tensor> input, const int ptr) {
    auto workload = tt::golden::io::get_workload(netlist_path);
    if (q_info.type == IO_TYPE::Queue) {
        workload->queues[q_info.name].my_io->push(input);
    } else if (q_info.type == IO_TYPE::RandomAccess) {
        log_assert(ptr >= 0, "wr_ptr for ram={} must be set when pushing using pybuda API", q_info.name);
        int saved_global_wr_ptr = workload->queues[q_info.name].my_io->get_global_wr_ptr();
        workload->queues[q_info.name].my_io->set_global_wr_ptr(ptr);
        workload->queues[q_info.name].my_io->push(input);
        workload->queues[q_info.name].my_io->set_global_wr_ptr(saved_global_wr_ptr);
    } else {
        log_fatal("push_input IO_TYPE is invalid");
    }
}
std::shared_ptr<tt_tensor> pop_output(const std::string netlist_path, const tt_queue_info &q_info) {
    auto workload = tt::golden::io::get_workload(netlist_path);
    std::shared_ptr<tt_tensor> result;
    if (q_info.type == IO_TYPE::Queue) {
        workload->queues[q_info.name].my_io->set_local_rd_ptr(workload->queues[q_info.name].my_io->get_global_rd_ptr());
        result = workload->queues[q_info.name].my_io->get();
        workload->queues[q_info.name].my_io->pop();
    } else if (q_info.type == IO_TYPE::RandomAccess) {
        log_fatal("ram cannot be popped -- pop called on ram={}", q_info.name);
    } else {
        log_fatal("pop_output IO_TYPE is invalid");
    }
    return result;
}
std::shared_ptr<tt_tensor> get_output(const std::string netlist_path, const tt_queue_info &q_info, const int ptr) {
    auto workload = tt::golden::io::get_workload(netlist_path);
    std::shared_ptr<tt_tensor> result;
    if (q_info.type == IO_TYPE::Queue) {
        int saved_local_rd_ptr = workload->queues[q_info.name].my_io->get_local_rd_ptr();
        workload->queues[q_info.name].my_io->set_local_rd_ptr(ptr);
        result = workload->queues[q_info.name].my_io->get();
        workload->queues[q_info.name].my_io->set_local_rd_ptr(saved_local_rd_ptr);
    } else if (q_info.type == IO_TYPE::RandomAccess) {
        log_assert(ptr >= 0, "rd_ptr for ram={} must be set when getting output using pybuda API", q_info.name);
        int saved_global_rd_ptr = workload->queues[q_info.name].my_io->get_global_rd_ptr();
        workload->queues[q_info.name].my_io->set_global_rd_ptr(ptr);
        result = workload->queues[q_info.name].my_io->get();
        workload->queues[q_info.name].my_io->set_global_rd_ptr(saved_global_rd_ptr);
    } else {
        log_fatal("get_output IO_TYPE is invalid");
    }
    result -> metadata.shape.tile_height = get_tile_dim_y(q_info);
    result -> metadata.shape.tile_width = get_tile_dim_x(q_info);
    return result;
}
}  // namespace tt::golden::io
