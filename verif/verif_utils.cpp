// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_utils.hpp"

#include <filesystem>
#include <utility>

#include "common/io_lib.hpp"
#include "tt_backend_api.hpp"
#include "utils/logger.hpp"
#include "runtime/runtime_io.hpp"
#include "common/tensor_lib.hpp"
ostream& operator<<(ostream& os, const tt_tensor_metadata& t) {
    os << "tt_tensor_metadata{";
    os << " .shape = " << t.shape << ",";
    os << " .data_format = " << t.data_format << ",";
    os << " .gradient_data_format = " << t.gradient_data_format << ",";
    os << " .optimizer_data_format = " << t.optimizer_data_format << ",";
    os << " .is_tilized = " << t.is_tilized << ",";
    os << " .requires_grad = " << t.requires_grad << ",";
    os << " .block_shape = " << t.block_shape << ",";
    os << " .grid_shape = " << t.grid_shape;
    os << "}";
    return os;
}

set<verif::ExternalBinaryMode> verif::cmdline_to_external_binary_mode(string all_modes) {
    vector<string> each_mode;
    set<verif::ExternalBinaryMode> each_mode_external;
    verif_args::split_string_into_vector(each_mode, all_modes, ",");
    for (string mode: each_mode) {
        if (mode == "") {
            return {};
        } else if (mode == "all" || mode == "constants") {
            each_mode_external.insert(verif::ExternalBinaryMode::Constants);
        } else if (mode == "all" || mode == "all_inputs") {
            each_mode_external.insert(verif::ExternalBinaryMode::AllInputs);
        } else if (mode == "all" || mode == "all_outputs") {
            each_mode_external.insert(verif::ExternalBinaryMode::AllOutputs);
        } else {
            log_fatal("Invalid ExternalBinaryMode");
        }
    }
    return each_mode_external;
}

void verif::read_tensor_from_binary(
    string yaml_root, tt_queue_info queue_info, int entry_index, tt_tensor& tensor, const bool& init_rams, const uint &replicate_entries) {
    string queue_name = queue_info.name;
    tensor.metadata.is_tilized = false;  // When reading from flat yaml inputs, we assume not tilized
    string data_filepath = tt::data_binary::get_filename(yaml_root, queue_name, entry_index);
    string init_rams_filepath = yaml_root + queue_name + ".init." + to_string(entry_index) + ".bin";

    std::vector<float> data_vector = {};
    if (init_rams and (queue_info.type == IO_TYPE::RandomAccess) and (queue_info.input != "HOST") and
        std::filesystem::exists(init_rams_filepath)) {
        // Need to add init_rams
        tt::data_binary::read_file(init_rams_filepath, data_vector);
    } else {
        tt::data_binary::read_file(data_filepath, data_vector);
    }
    
    if (replicate_entries > 0) {
        auto original_size = data_vector.size();
        data_vector.resize((replicate_entries + 1) * original_size);
        for (int i = 0; i < replicate_entries; i++) {
            std::copy_n(data_vector.begin(), original_size, data_vector.begin() + i * original_size);
        }
    }

    log_debug(tt::LogVerif, "Reading tensor {} entry {} from yaml", queue_name, entry_index);
    log_assert(tensor.metadata.shape.z > 0, "tensor_metadata.shape.z must be greater than 0");
    log_debug(
        tt::LogVerif,
        "tensor.volume.in_elements={}, data_vector_elements: {}",
        tensor.total_tiles() * 32 * 32,
        data_vector.size());
    tensor.fill_with_data(data_vector);
    if (queue_info.data_format == DataFormat::RawUInt8 or queue_info.data_format == DataFormat::RawUInt16 or
         queue_info.data_format == DataFormat::RawUInt32) {
        tensor.metadata.data_format = queue_info.data_format;
    } else {
        tensor.metadata.data_format = DataFormat::Float32;
    }
    tensor.tilize_inplace(true, false, queue_info.layout == IO_LAYOUT::Flat);
    tensor.set_is_tilized(true);
}

void verif::write_tensor_to_binary(string yaml_root, const std::string& q_name, int entry_index, tt_tensor &tensor) {

    size_t numel = tensor.get_shape().volume_full();
    vector<float> data_tensor(numel);

    for (int iw = 0; iw < tensor.getw(); iw++) {
        for (int iz = 0; iz < tensor.getz(); iz++) {
            for (int irt = 0; irt < tensor.getrt(); irt++) {
                for (int ict = 0; ict < tensor.getct(); ict++) {
                    const tt_tile &tile = tensor.tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    for (int i_tile_row = 0; i_tile_row < tensor.get_shape().tile_height; i_tile_row++) {
                        size_t i_dest_w = iw * tensor.getz() * tensor.getrfull() * tensor.getcfull();
                        size_t i_dest_z = iz * tensor.getrfull() * tensor.getcfull();
                        size_t ir = irt * tensor.get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * tensor.getcfull();
                        size_t i_dest_c = (ict * tensor.get_shape().tile_width);

                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        for (size_t i_tile_col = 0; i_tile_col < tensor.get_shape().tile_width; i_tile_col++) {
                            data_tensor[dest_row_index + i_tile_col] = *((float*)(&(tile.t[i_tile_row][i_tile_col])));
                        }
                    }
                }
            }
        }
    }
    tt::data_binary::dump_file(tt::data_binary::get_filename(yaml_root, q_name, entry_index), data_tensor);
}

//! This function takes a tt_tensor (which is tilized), and converts it to a raw untilized tensor before pushing into
//! the tt_backend using the pybuda api
void verif::push_tensor(
    tt_backend& backend, const string& q_name, tt_tensor* tensor, const int ptr, const int timeout) {
    tt::tt_dram_io_desc q_desc = backend.get_queue_descriptor(q_name);
    // Create flat vector..
    tensor->set_is_tilized(true);
    tt::tt_PytorchTensorDesc py_tensor_desc;
    aligned_vector<float> float_vector = aligned_vector<float>{};
    aligned_vector<uint16_t> half_float_vector = aligned_vector<uint16_t>{};
    aligned_vector<int8_t> byte_vector = aligned_vector<int8_t>{};
    aligned_vector<uint8_t> unsigned_byte_vector = aligned_vector<uint8_t>{};
    aligned_vector<int32_t> dword_vector = aligned_vector<int32_t>{};
    // Convert the tensor to untilized format and construct pytorch tensor desc
    if (tensor->metadata.data_format == DataFormat::Float32 || tensor->metadata.data_format == DataFormat::RawUInt32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<float>(*tensor, float_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::Int8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int8_t>(*tensor, byte_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::UInt8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint8_t>(*tensor, unsigned_byte_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::Int32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int32_t>(*tensor, dword_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint16_t>(*tensor, half_float_vector, q_desc.layout == IO_LAYOUT::Flat);
    }
    py_tensor_desc.owner = tt::OWNERSHIP::Frontend;  // host pushed tensor, pretending to be FE owned
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::translate_addresses for queue = {} failed", q_name);
    }
    if (tt::backend::push_input(q_desc, py_tensor_desc, true, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::push_input for queue = {} failed", q_name);
    }
}

void verif::push_pytorch_tensor(tt_backend& backend, const string& q_name, tt::tt_PytorchTensorDesc tensor, const bool is_activation, const int ptr, const int timeout, tt::tt_dram_io_desc q_desc_override) {
    tt::tt_dram_io_desc q_desc;

    if(q_desc_override.s_descriptor.stride > 0) {
        q_desc = q_desc_override;
        q_desc.backend_type = backend.get_queue_descriptor(q_name).backend_type;
    } else {
        q_desc = backend.get_queue_descriptor(q_name);
    }
    
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::translate_addresses for queue = {} failed", q_name);
    }
    
    if (tt::backend::push_input(q_desc, tensor, !is_activation, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::push_input for queue = {} failed", q_name);
    }
}

void verif::push_batched_tensor(tt_backend& backend, const string& q_name, tt_tensor* tensor, const bool is_activation, const int ptr, const int timeout, tt::tt_dram_io_desc q_desc_override, bool untilize_as_flat_as_tt_tensor) {
    tt::tt_dram_io_desc q_desc;

    if(q_desc_override.s_descriptor.stride > 0 || q_desc_override.layout != backend.get_queue_descriptor(q_name).layout) {
        q_desc = q_desc_override;
        q_desc.backend_type = backend.get_queue_descriptor(q_name).backend_type;
    } else {
        q_desc = backend.get_queue_descriptor(q_name);
    }
    
    // Create flat vector..
    tensor->set_is_tilized(true);
    tt::tt_PytorchTensorDesc py_tensor_desc;
    aligned_vector<float> float_vector = aligned_vector<float>{};
    aligned_vector<uint16_t> half_float_vector = aligned_vector<uint16_t>{};
    aligned_vector<int8_t> byte_vector = aligned_vector<int8_t>{};
    aligned_vector<uint8_t> unsigned_byte_vector = aligned_vector<uint8_t>{};
    aligned_vector<int32_t> dword_vector = aligned_vector<int32_t>{};
    // Convert the tensor to untilized format and construct pytorch tensor desc
    if (tensor->metadata.data_format == DataFormat::Float32 || tensor->metadata.data_format == DataFormat::RawUInt32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<float>(*tensor, float_vector, (!untilize_as_flat_as_tt_tensor) && q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::Int8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int8_t>(*tensor, byte_vector, (!untilize_as_flat_as_tt_tensor) && q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::UInt8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint8_t>(*tensor, unsigned_byte_vector, (!untilize_as_flat_as_tt_tensor) && q_desc.layout == IO_LAYOUT::Flat);
    } else if (tensor -> metadata.data_format == DataFormat::Int32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int32_t>(*tensor, dword_vector, (!untilize_as_flat_as_tt_tensor) && q_desc.layout == IO_LAYOUT::Flat);
    }    
    else {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint16_t>(*tensor, half_float_vector, (!untilize_as_flat_as_tt_tensor) && q_desc.layout == IO_LAYOUT::Flat);
    }
    py_tensor_desc.owner = tt::OWNERSHIP::Frontend;  // host pushed tensor, pretending to be FE owned
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::translate_addresses for queue = {} failed", q_name);
    }
    if (tt::backend::push_input(q_desc, py_tensor_desc, !is_activation, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::push_input for queue = {} failed", q_name);
    }
}

void verif::push_tensors(tt_backend& backend, const string& q_name, std::vector<tt_tensor>& tensors, const int ptr, const int timeout) {
    tt::tt_dram_io_desc q_desc = backend.get_queue_descriptor(q_name);
    tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    md.shape.w = q_info.input_count;
    tt_tensor temp_tensor(md);
    temp_tensor = tt_tensor::wstack(tensors);
    // Create flat vector..

    temp_tensor.set_is_tilized(true);
    tt::tt_PytorchTensorDesc py_tensor_desc;
    aligned_vector<float> float_vector = aligned_vector<float>{};
    aligned_vector<uint16_t> half_float_vector = aligned_vector<uint16_t>{};
    aligned_vector<int8_t> byte_vector = aligned_vector<int8_t>{};
    aligned_vector<uint8_t> unsigned_byte_vector = aligned_vector<uint8_t>{};
    aligned_vector<int32_t> dword_vector = aligned_vector<int32_t>{};
    // Convert the tensor to untilized format and construct pytorch tensor desc
    if (temp_tensor.metadata.data_format == DataFormat::Float32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<float>(temp_tensor, float_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (temp_tensor.metadata.data_format == DataFormat::Int8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int8_t>(temp_tensor, byte_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (temp_tensor.metadata.data_format == DataFormat::UInt8) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint8_t>(temp_tensor, unsigned_byte_vector, q_desc.layout == IO_LAYOUT::Flat);
    } else if (temp_tensor.metadata.data_format == DataFormat::Int32) {
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<int32_t>(temp_tensor, dword_vector, q_desc.layout == IO_LAYOUT::Flat);
    }
    else {  
        py_tensor_desc = tt::io::get_pytorch_tensor_desc<uint16_t>(temp_tensor, half_float_vector, q_desc.layout == IO_LAYOUT::Flat);
    }
    py_tensor_desc.owner = tt::OWNERSHIP::Frontend;  // host pushed tensor, pretending to be FE owned
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::translate_addresses for queue = {} failed", q_name);
    }
    if (tt::backend::push_input(q_desc, py_tensor_desc, false, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("push_tensor::push_input for queue = {} failed", q_name);
    }
}

//! This function takes a raw untilized tensor output from tt_backend, and converts it to a tt_tensor (which is tilized)
//! before return to test/host side
void verif::get_and_pop_tensor(
    tt_backend& backend, const string& q_name, tt_tensor* tensor, const int ptr, const int timeout) {
    tt::tt_dram_io_desc q_desc = backend.get_queue_descriptor(q_name);
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
    tensor->metadata = md;
    tt::tt_PytorchTensorDesc py_tensor_desc;
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::translate_addresses for queue = {} failed", q_name);
    }
    if (tt::backend::get_output(q_desc, py_tensor_desc, true, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::get_output for queue = {} failed", q_name);
    }

    std::vector<float> data_vector = {};
    // Convert the whole mem vector from py_tensor_desc to tensor
    log_assert(tt::io::get_pytorch_tensor_format(q_info.data_format) != DataFormat::Invalid,
        "Must be a supported device to pytorch format conversion");
    const uint32_t* data = static_cast<const uint32_t*>(py_tensor_desc.ptr);
    uint32_t data_len = get_tensor_size_in_elements(q_info) * py_tensor_desc.itemsize / sizeof(uint32_t);
    data_vector = tt::io::process_raw_untilized(data, data_len, py_tensor_desc.format);

    if (q_desc.io_type == IO_TYPE::Queue) {
        if (tt::backend::pop_output(q_desc, true, timeout) != tt::DEVICE_STATUS_CODE::Success) {
            log_fatal("get_and_pop_tensor::pop_output for queue = {} failed", q_name);
        }
    }

    tensor->set_is_tilized(false);
    tensor->flat_tensor_data = std::move(data_vector);
    tensor->tilize_inplace(true, false, q_desc.layout == IO_LAYOUT::Flat);
    if (tt::backend::free_tensor(py_tensor_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::free_tensor for queue = {} failed", q_name);
    }
}
void verif::get_and_pop_tensors(
    tt_backend& backend, const string& q_name, std::vector<tt_tensor>& tensors, const int ptr, const int timeout, bool pop_tensor) {
    tt::tt_dram_io_desc q_desc = backend.get_queue_descriptor(q_name);
    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
    md.shape.w = q_info.input_count;
    tt_tensor temp_tensor(md);
    tt::tt_PytorchTensorDesc py_tensor_desc;
    if (tt::backend::translate_addresses(q_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::translate_addresses for queue = {} failed", q_name);
    }
    if (tt::backend::get_output(q_desc, py_tensor_desc, false, timeout, ptr) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::get_output for queue = {} failed", q_name);
    }

    std::vector<float> data_vector = {};
    const uint32_t* data = static_cast<const uint32_t*>(py_tensor_desc.ptr);
    uint32_t data_len = q_info.input_count * get_tensor_size_in_elements(q_info) * py_tensor_desc.itemsize  / sizeof(uint32_t);
    data_vector = tt::io::process_raw_untilized(data, data_len, py_tensor_desc.format);

    if (q_desc.io_type == IO_TYPE::Queue and pop_tensor) {
        if (tt::backend::pop_output(q_desc, false, timeout) != tt::DEVICE_STATUS_CODE::Success) {
            log_fatal("get_and_pop_tensor::pop_output for queue = {} failed", q_name);
        }
    }
    
    temp_tensor.set_is_tilized(false);
    temp_tensor.fill_with_data(data_vector);
    temp_tensor.tilize_inplace(true, false, q_desc.layout == IO_LAYOUT::Flat);
    tensors = tensor_lib::split_merge_ops::wsplit(temp_tensor, false);
    if (tt::backend::free_tensor(py_tensor_desc) != tt::DEVICE_STATUS_CODE::Success) {
        log_fatal("get_and_pop_tensor::free_tensor for queue = {} failed", q_name);
    }
}

void verif::get_and_pop_flat_tensors(tt_backend& backend, const string& q_name, std::vector<float>& data_vector, const int ptr, const int timeout, std::uint32_t hstack_factor, std::uint32_t vstack_factor, bool stack_row_major){
    tt::tt_dram_io_desc q_desc = backend.get_queue_descriptor(q_name);
    q_desc.hstack_factor = hstack_factor;
    q_desc.vstack_factor = vstack_factor;
    q_desc.stack_row_major = stack_row_major;

    tt_queue_info q_info = get_tt_queue_info_from_tt_dram_io_desc(q_desc);
    tt::tt_PytorchTensorDesc py_tensor_desc;
    
    log_assert(tt::backend::translate_addresses(q_desc) == tt::DEVICE_STATUS_CODE::Success, "get_and_pop_flat_tensors::translate_addresses for queue = {} failed", q_name);
    log_assert(tt::backend::get_output(q_desc, py_tensor_desc, false, timeout, ptr) == tt::DEVICE_STATUS_CODE::Success, "get_and_pop_flat_tensors::get_output for queue = {} failed", q_name);

    const uint32_t* data = static_cast<const uint32_t*>(py_tensor_desc.ptr);
    uint32_t data_len = q_info.input_count * get_tensor_size_in_elements(q_info) * py_tensor_desc.itemsize  / sizeof(uint32_t);
    data_vector = tt::io::process_raw_untilized(data, data_len, py_tensor_desc.format);

    if (q_desc.io_type == IO_TYPE::Queue) {
        log_assert(tt::backend::pop_output(q_desc, false, timeout) == tt::DEVICE_STATUS_CODE::Success, "get_and_pop_flat_tensors::pop_output for queue = {} failed", q_name);
    }

    log_assert(tt::backend::free_tensor(py_tensor_desc) == tt::DEVICE_STATUS_CODE::Success, "get_and_pop_flat_tensors::free_tensor for queue = {} failed", q_name);
}

int get_buffer_end_address(int buffer_start_addr, YAML::Node queue_info){
    bool tilized_data = true;
    if(queue_info["layout"]) {
        tilized_data = !(queue_info["layout"].as<string>() == "flat");
    }
    int tile_size = tt::size::get_tile_size_in_bytes(verif::STRING_TO_DATA_FORMAT.at(queue_info["df"].as<string>()), tilized_data);
    return buffer_start_addr + tile_size * queue_info["entries"].as<int>() * queue_info["t"].as<int>() * queue_info["mblock"][0].as<int>() * queue_info["mblock"][1].as<int>() * queue_info["ublock"][0].as<int>() * queue_info["ublock"][1].as<int>() + tt::io::io_queue_header_size_bytes;
}

void verif::shift_addresses_by_offset(std::string input_filepath, std::string output_filepath, int offset) {
    YAML::Node netlist = YAML::LoadFile(input_filepath);
    YAML::Emitter emmiter;
    std::vector<verif::buffer_info> updated_queue_buffers;
    std::vector<std::string> arch_vector;
    std::string arch_str = "";

    try {
        arch_vector = netlist["devices"]["arch"].as<std::vector<std::string>>();
    }
    catch (YAML::BadConversion& e) {
        arch_vector = {};
        arch_str = netlist["devices"]["arch"].as<std::string>();
    }

    for(auto queue: netlist["queues"]) {
        if(queue.second["loc"].as<std::string>() == "dram"){
            for(auto buffer: queue.second["dram"]){
                if(!(buffer[0].as<int>() == 0 and buffer[1].as<int>() >= 0x30000000 and buffer[1].as<int>() <= 0x40000000)){
                    int start_addr = buffer[1].as<int>();
                    std::stringstream hex_addr;
                    hex_addr << "0x" <<  std::hex << start_addr + offset;
                    buffer[1] = hex_addr.str();
                }
                updated_queue_buffers.push_back({queue.first.as<std::string>(), buffer[0].as<int>(), buffer[1].as<int>(), get_buffer_end_address(buffer[1].as<int>(), queue.second), queue.second["target_device"].as<int>()});
            }
        }
    }

    for(std::uint32_t i = 0; i < updated_queue_buffers.size(); i++) {
        if(arch_str == "grayskull" or std::find(arch_vector.begin(), arch_vector.end(), "grayskull") != arch_vector.end()) {
            if(updated_queue_buffers[i].start_addr > 0x40000000) log_warning(tt::LogVerif, "The starting address for {} is greater than 0x40000000. Cannot run on Grayskull.", updated_queue_buffers[i].queue_name);
            if(updated_queue_buffers[i].end_addr > 0x40000000) log_warning(tt::LogVerif, "The end address for {} is greater than 0x40000000. Cannot run on Grayskull.", updated_queue_buffers[i].queue_name);
        }
        
        if(arch_str == "wormhole" or std::find(arch_vector.begin(), arch_vector.end(), "wormhole") != arch_vector.end()) {
            if(updated_queue_buffers[i].start_addr > 0x80000000)log_warning(tt::LogVerif, "The start address for {} is greater than 0x80000000. Cannot run on Wormhole.", updated_queue_buffers[i].queue_name);
            if(updated_queue_buffers[i].end_addr > 0x80000000) log_warning(tt::LogVerif, "The end address for {} is greater than 0x80000000. Cannot run on Wormhole.", updated_queue_buffers[i].queue_name);
        }
        for(std::uint32_t j = i + 1; j < updated_queue_buffers.size(); j++){
            if(updated_queue_buffers[i].device_id == updated_queue_buffers[j].device_id and  updated_queue_buffers[i].channel == updated_queue_buffers[j].channel and updated_queue_buffers[i].start_addr < updated_queue_buffers[j].end_addr and updated_queue_buffers[i].end_addr > updated_queue_buffers[j].start_addr){
                log_warning(tt::LogVerif, "{} and {} have overlapping buffers", updated_queue_buffers[i].queue_name, updated_queue_buffers[j].queue_name);
            }
        }
    }
    
    emmiter << netlist;
    std::ofstream fout(output_filepath);
    fout << emmiter.c_str();
}

void verif::get_input_output_queue_names(const string &netlist_path, vector<string> &input_queues, vector<string> &output_queues) {
    log_assert(fs::exists(netlist_path),  "Netlist path {} does not exist", netlist_path);
    std::ifstream ifs(netlist_path);
    string line;
    bool found_queue_section = false;
    bool found_output_section = false;
    bool found_input_section = false;
    while (getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        int white_space = 0;
        while (line.at(white_space) == ' ') {
            white_space++;
        }
        line = line.substr(white_space, line.size() - white_space);
        if (line == "# parameter" || line == "# constant" || line == "# epoch_to_epoch" || line == "graphs") {
            break;
        }
        if (line != "queues:" && !found_queue_section) {
            continue;
        }
        if (line == "queues:") {
            found_queue_section = true;
            continue;
        }
        if (line == "# input") {
            found_input_section = true;
            found_output_section = false;
            continue;
        }
        if (line == "# output") {
            found_output_section = true;
            found_input_section = false;
            continue;
        }
        if (found_queue_section && (found_input_section || found_output_section)) {
            auto colon_pos = line.find(":");
            log_assert(colon_pos != std::string::npos, "Each queue must have a : immediately after the queue name");
            string queue_name = line.substr(0, colon_pos);
            if (found_input_section) {
                input_queues.push_back(queue_name);
            } else {
                output_queues.push_back(queue_name);
            }
        }
    }
    if (input_queues.empty() || output_queues.empty()) {
        log_fatal("No Input/Output queues were identified from the netlist under {}", netlist_path);
    }

}

std::unordered_map<int, std::vector<uint32_t>> verif::parse_harvesting_masks(const std::string& harv_mask_str) {
    std::unordered_map<int, std::vector<uint32_t>> harvesting_masks = {};
    std::string row = "";
    std::vector<uint32_t> rows_per_chip = {};
    int i = 0;
    
    for(char c : harv_mask_str) {
        if(c == ',' or c == '.') {
            if(!row.empty()) {;
                rows_per_chip.push_back(std::stoi(row));
                row.clear();
            }
            if(c == '.') {
                harvesting_masks[i++] = rows_per_chip;
                rows_per_chip.clear();
            }
        }
        else {
            row += c;
        }
    }
    if (!row.empty()) {
        rows_per_chip.push_back(std::stoi(row));
    }
    if(!rows_per_chip.empty()) {
        harvesting_masks[i] = rows_per_chip;
    }
    return harvesting_masks;
}