// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_backend_api_types.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <tuple>

#include "common/data_binary_lib.hpp"
#include "utils/logger.hpp"
namespace {
    std::string to_lower(const std::string& str) {
        std::string lower_str = str;
        std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), [](unsigned char c) { return std::tolower(c); });
        return lower_str;
    }
    std::string to_upper(const std::string &str) {
        std::string upper_str = str;
        std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), [](unsigned char c) { return std::toupper(c); });
        return upper_str;
    }
}

namespace tt {

std::string get_string(TileDim tile_dim) {
    switch(tile_dim) {
        case TileDim::Dim32x32:
            return "32x32";
        case TileDim::Dim16x32:
            return "16x32";
        case TileDim::Dim32x16:
            return "32x16";
        case TileDim::Dim8x32:
            return "8x32";
        case TileDim::Dim4x32:
            return "4x32";
        case TileDim::Dim2x32:
            return "2x32";
        case TileDim::Dim1x32:
            return "1x32";
        default:
            return "Invalid";
    }
}

TileDim get_tile_dim_from_array(const std::vector<int>& tile_dim_arr) {
    std::string tile_dim_str = std::to_string(tile_dim_arr[0]) + "x" + std::to_string(tile_dim_arr[1]);
    return get_tile_dim_from_string(tile_dim_str);
}

TileDim get_tile_dim_from_string(const std::string& tile_dim_str) {
    if (tile_dim_str == "32x32") {
        return TileDim::Dim32x32;
    } else if (tile_dim_str == "16x32") {
        return TileDim::Dim16x32;
    } else if(tile_dim_str == "8x32") {
        return TileDim::Dim8x32;
    } else if(tile_dim_str == "4x32") {
        return TileDim::Dim4x32;
    } else if(tile_dim_str == "2x32") {
        return TileDim::Dim2x32;
    } else if(tile_dim_str == "1x32") {
        return TileDim::Dim1x32;
    } else if (tile_dim_str == "32x16") {
        return TileDim::Dim32x16;
    } else {
        return TileDim::Invalid;
    }
}

TileDim transpose_tile_dim(TileDim input_tile_dim) {
    switch (input_tile_dim) {
    case TileDim::Dim16x32:
        return TileDim::Dim32x16;
    case TileDim::Dim32x16:
        return TileDim::Dim16x32;
    case TileDim::Dim32x32:
        return TileDim::Dim32x32;    
    default:
        return TileDim::Invalid;
    }
}

std::vector<int> tile_dim_to_array(TileDim tile_dim) {
    std::vector<int> tile_dim_array = {32, 32};
    switch (tile_dim) {
        case TileDim::Dim16x32:
            tile_dim_array[0] = 16;
            break;
        case TileDim::Dim32x16:
            tile_dim_array[1] = 16;
            break;
        case TileDim::Dim8x32:
            tile_dim_array[0] = 8;
            break;
        case TileDim::Dim4x32:
            tile_dim_array[0] = 4;
            break;
        case TileDim::Dim2x32:
            tile_dim_array[0] = 2;
            break;
        case TileDim::Dim1x32:
            tile_dim_array[0] = 1;
            break;
        case TileDim::Dim32x32:
            break;
        case TileDim::Invalid:
            log_fatal("Invalid tile dim."); 
    }

    return tile_dim_array;
}

DataFormat get_format_from_string(const std::string &format_str) {
    if (format_str == "Bfp2") {
        return DataFormat::Bfp2;
    } else if (format_str == "Bfp2_b") {
        return DataFormat::Bfp2_b;
    } else if (format_str == "Bfp4") {
        return DataFormat::Bfp4;
    } else if (format_str == "Bfp4_b") {
        return DataFormat::Bfp4_b;
    } else if (format_str == "Bfp8") {
        return DataFormat::Bfp8;
    } else if (format_str == "Bfp8_b") {
        return DataFormat::Bfp8_b;
    } else if (format_str == "Float16") {
        return DataFormat::Float16;
    } else if (format_str == "Float16_b") {
        return DataFormat::Float16_b;
    } else if (format_str == "Float32") {
        return DataFormat::Float32;
    } else if (format_str == "Tf32") {
        return DataFormat::Tf32;
    } else if (format_str == "Int8") {
        return DataFormat::Int8;
    } else if (format_str == "Lf8") {
        return DataFormat::Lf8;
    } else if (format_str == "UInt16") {
        return DataFormat::UInt16;
    } else if (format_str == "RawUInt8") {
        return DataFormat::RawUInt8;
    } else if (format_str == "RawUInt16") {
        return DataFormat::RawUInt16;
    } else if (format_str == "RawUInt32") {
        return DataFormat::RawUInt32;
    } else if (format_str == "Int32") {
        return DataFormat::Int32;
    } else {
        return DataFormat::Invalid;
    }
}

SfpuVectorMode get_sfpu_vector_mode_from_string(const std::string &vector_mode_string) {
    if (vector_mode_string == "rc") {
        return SfpuVectorMode::RC;
    } else if (vector_mode_string == "r") {
        return SfpuVectorMode::R;
    } else if (vector_mode_string == "c") {
        return SfpuVectorMode::C;
    } else {
        return SfpuVectorMode::Invalid;
    }
}

std::string get_string(DEVICE device) {
    switch (device) {
        case DEVICE::Model: return "Model"; break;
        case DEVICE::Versim: return "Versim"; break;
        case DEVICE::Silicon: return "Silicon"; break;
        case DEVICE::Golden: return "Golden"; break;
        case DEVICE::StaticAnalyzer: return "StaticAnalyzer"; break;
        case DEVICE::Invalid: return "Invalid"; break;
        default: return "Invalid"; break;
    }
}

DEVICE get_device_from_string(const std::string &device_string) {
    if (device_string == "Model") {
        return DEVICE::Model;
    } else if (device_string == "Versim") {
        return DEVICE::Versim;
    } else if (device_string == "Silicon") {
        return DEVICE::Silicon;
    } else if (device_string == "Golden") {
        return DEVICE::Golden;
    }  else if (device_string == "StaticAnalyzer") {
        return DEVICE::StaticAnalyzer;
    } else {
        return DEVICE::Invalid;
    }
}

std::string get_string(ARCH arch) {
    switch (arch) {
        case ARCH::JAWBRIDGE: return "JAWBRIDGE"; break;
        case ARCH::GRAYSKULL: return "GRAYSKULL"; break;
        case ARCH::WORMHOLE: return "WORMHOLE"; break;
        case ARCH::WORMHOLE_B0: return "WORMHOLE_B0"; break;
        case ARCH::Invalid: return "Invalid"; break;
        default: return "Invalid"; break;
    }
}

std::string get_string_lowercase(ARCH arch) {
    return to_lower(get_string(arch));
}

ARCH get_arch_from_string(const std::string &arch_str) {
    ARCH arch;
    std::string str = to_upper(arch_str);
    if (str == "JAWBRIDGE") {
        arch = ARCH::JAWBRIDGE;
    } else if (str == "GRAYSKULL") {
        arch = ARCH::GRAYSKULL;
    } else if (str == "WORMHOLE") {
        arch = ARCH::WORMHOLE;
    } else if (str == "WORMHOLE_B0") {
        arch = ARCH::WORMHOLE_B0;
    } else if (str == "INVALID") {
        arch = ARCH::Invalid;
    } else {
        throw std::runtime_error(arch_str + " is not recognized as tt::ARCH.");
    }

    return arch;
}

std::string get_string(DEVICE_MODE device_mode) {
    switch (device_mode) {
        case DEVICE_MODE::CompileAndRun: return "CompileAndRun"; break;
        case DEVICE_MODE::CompileOnly: return "CompileOnly"; break;
        case DEVICE_MODE::RunOnly: return "RunOnly"; break;
        default: return "CompileAndRun"; break;
    }
}
DEVICE_MODE get_device_mode_from_string(const std::string& device_mode_str) {
    DEVICE_MODE device_mode;

    if (device_mode_str == "CompileAndRun") {
        device_mode = DEVICE_MODE::CompileAndRun;
    } else if (device_mode_str == "CompileOnly") {
        device_mode = DEVICE_MODE::CompileOnly;
    } else if (device_mode_str == "RunOnly") {
        device_mode = DEVICE_MODE::RunOnly;
    } else {
        throw std::runtime_error(device_mode_str + " is not recognized as tt::DEVICE_MODE.");
    }
    return device_mode;
}

std::string get_string(COMPILE_FAILURE compile_failure) {
    switch (compile_failure) {
        case COMPILE_FAILURE::BriscCompile: return "BriscCompile"; break;
        case COMPILE_FAILURE::EriscCompile: return "EriscCompile"; break;
        case COMPILE_FAILURE::NriscCompile: return "NriscCompile"; break;
        case COMPILE_FAILURE::Net2Pipe: return "Net2Pipe"; break;
        case COMPILE_FAILURE::PipeGen: return "PipeGen"; break;
        case COMPILE_FAILURE::BlobGen: return "BlobGen"; break;
        case COMPILE_FAILURE::L1Size: return "L1Size"; break;
        case COMPILE_FAILURE::OverlaySize: return "OverlaySize"; break;
        default: return "undefined"; break;
    }
}

std::string get_string(const tt_compile_result& result) {
    std::string out = result.success ? "SUCCESS" : "FAILURE";
    if (!result.success) {
        out += ", failure_type: " + get_string(result.failure_type);
        out += ", device_id: " + std::to_string(result.device_id);
        out += ", temporal_epoch_id: " + std::to_string(result.temporal_epoch_id);
        out += ", graph: " + result.graph_name;
        out += ", logical_core_x: " + std::to_string(result.logical_core_x);
        out += ", logical_core_y: " + std::to_string(result.logical_core_y);
        out += ", extra_size_bytes: " + std::to_string(result.extra_size_bytes);
        out += ", failure_target: \n" + result.failure_target + "\n";
    }
    return out;
}

std::string get_string(const IO_TYPE &io_type) {
    switch(io_type) {
        case IO_TYPE::Queue:
            return "queue";
        case IO_TYPE::RandomAccess:
            return "ram";
        case IO_TYPE::Invalid:
            return "invalid";
        default:
            log_fatal("Invalid io_type");
            return "invalid";
    }
}

bool tensor_meta_to_tensor_desc(const tt_device_image::tensor_meta &meta, std::vector<float> &tensor_data, tt_PytorchTensorDesc& pytorch_tensor, tt_TilizedTensorDesc& tilized_tensor, DataFormat buf_target_format) {
    bool tilized;
    std::string tensor_bin = std::get<0>(meta);
    tt::data_binary::read_file(tensor_bin, tensor_data);
    tt::DataFormat tensor_df = get_format_from_string(std::get<2>(meta));
    // Ambiguity between Int32/Int8 and RawUint32/RawUInt8 when tensor metadata is generated 
    // Temprorarily resolve this by inferring the tensor format from the queue descriptor target format, 
    // until RawUInt* formats are added to Pytorch
    if (tensor_df == DataFormat::Int32) {
        if (buf_target_format == DataFormat::RawUInt32) {
            tensor_df = DataFormat::RawUInt32;
        }
    }
    else if (tensor_df == DataFormat::Int8) {
        if (buf_target_format == DataFormat::RawUInt8) {
            tensor_df = DataFormat::RawUInt8;
        }
    }

    if(tensor_bin.find(".tbin") == std::string::npos) {
        
        pytorch_tensor = tt_PytorchTensorDesc(tensor_data.data(), std::get<1>(meta), tensor_df, {}, {}, std::get<5>(meta));
        for (int i = 0; i < PY_TENSOR_DIMS; i++) {
            pytorch_tensor.shape[i] = std::get<3>(meta)[i];
            pytorch_tensor.strides[i] = std::get<4>(meta)[i];
        }
        tilized = false;
    }
    else {
        // Expects the following order: {bin_name, num_bufs, format, empty, empty, buf_size_bytes}
        tilized_tensor = tt_TilizedTensorDesc(tensor_data.data(), std::get<1>(meta), std::get<5>(meta), tensor_df);
        tilized = true;
    }
    
    return tilized;
}

}  // namespace tt
