// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_tensor_data_format.h"

#include <iostream>

#include "tile.h"

std::string llk::to_str(const DataFormat format) {
    std::string result;
    switch (format) {
        case DataFormat::Bfp2: result = "Bfp2"; break;
        case DataFormat::Bfp2_b: result = "Bfp2_b"; break;
        case DataFormat::Bfp4: result = "Bfp4"; break;
        case DataFormat::Bfp4_b: result = "Bfp4_b"; break;
        case DataFormat::Bfp8: result = "Bfp8"; break;
        case DataFormat::Bfp8_b: result = "Bfp8_b"; break;
        case DataFormat::Float16: result = "Float16"; break;
        case DataFormat::Float16_b: result = "Float16_b"; break;
        case DataFormat::Float32: result = "Float32"; break;
        case DataFormat::Tf32: result = "Tf32"; break;
        default:
            std::cout << __FUNCTION__ << "::Unsupported: DataFormat=" << static_cast<int>(format) << std::endl;
            throw;
            break;
    }
    return result;
}

DataFormat llk::get_data_format(const std::string in_data_format) {
    DataFormat result;
    string data_format_string = in_data_format;
    transform(data_format_string.begin(), data_format_string.end(), data_format_string.begin(), ::toupper);
    
    if (data_format_string.compare("BFP2") == 0) {
        result = DataFormat::Bfp2;
    } else if (data_format_string.compare("BFP2_B") == 0) {
        result = DataFormat::Bfp2_b;
    } else if (data_format_string.compare("BFP4") == 0) {
        result = DataFormat::Bfp4;
    } else if (data_format_string.compare("BFP4_B") == 0) {
        result = DataFormat::Bfp4_b;
    } else if (data_format_string.compare("BFP8") == 0) {
        result = DataFormat::Bfp8;
    } else if (data_format_string.compare("BFP8_B") == 0) {
        result = DataFormat::Bfp8_b;
    } else if (data_format_string.compare("FLOAT16") == 0) {
        result = DataFormat::Float16;
    } else if (data_format_string.compare("FLOAT16_B") == 0) {
        result = DataFormat::Float16_b;
    } else if (data_format_string.compare("FLOAT32") == 0) {
        result = DataFormat::Float32;
    } else if (data_format_string.compare("TF32") == 0) {
        result = DataFormat::Tf32;
    }else {
        std::cout << __FUNCTION__ << "::Unsupported: data_format_string=" << data_format_string << std::endl;
        throw;
    }
    return result;
}
float llk::num_bytes(const DataFormat format) {
    float result;
    switch (format) {
        case DataFormat::Bfp2:
        case DataFormat::Bfp2_b: result = 0.25; break;
        case DataFormat::Bfp4:
        case DataFormat::Bfp4_b: result = 0.5; break;
        case DataFormat::Bfp8:
        case DataFormat::Bfp8_b: result = 1; break;
        case DataFormat::Float16:
        case DataFormat::Float16_b: result = 2; break;
        case DataFormat::Float32: result = 4; break;
        case DataFormat::Tf32: result = 4; break;
        default:
            std::cout << __FUNCTION__ << "::Unsupported: DataFormat=" << static_cast<int>(format) << std::endl;
            throw;
            break;
    }
    return result;
}

int llk::num_bytes_extra(const DataFormat format) {
    int result;
    switch (format) {
        case DataFormat::Bfp2:
        case DataFormat::Bfp2_b:
        case DataFormat::Bfp4:
        case DataFormat::Bfp4_b:
        case DataFormat::Bfp8:
        case DataFormat::Bfp8_b: result = 64 + 16; break;
        case DataFormat::Float16:
        case DataFormat::Float16_b: result = 16; break;
        case DataFormat::Float32: result = 16; break;
        case DataFormat::Tf32: result = 16; break;
        default:
            std::cout << __FUNCTION__ << "::Unsupported: DataFormat=" << static_cast<int>(format) << std::endl;
            throw;
            break;
    }
    return result;
}
