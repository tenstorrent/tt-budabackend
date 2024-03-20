// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_binary_lib.hpp"

#include <cstring>
#include <fstream>

#include "utils/logger.hpp"

namespace tt::data_binary {
std::string get_filename(const std::string& root, const std::string& q_name, int entry_index) {
    std::filesystem::path root_path(root);
    std::string filename = q_name + "." + std::to_string(entry_index) + ".bin";
    std::filesystem::path final_path = root_path / filename;
    return final_path.string();
}

bool does_file_exists(const std::string& root, const std::string& q_name, int entry_index) {
    return std::filesystem::exists(get_filename(root, q_name, entry_index));
}

void dump_file_on_env_flags(const std::string& q_name, const std::vector<float>& data_vector) {
    if (std::getenv("TT_BACKEND_CAPTURE_TENSORS") != nullptr) {
        std::string output_dir = "./";
        if (std::getenv("TT_BACKEND_CAPTURE_DIR") != nullptr) {
            output_dir = std::getenv("TT_BACKEND_CAPTURE_DIR");
            if (not std::filesystem::exists(output_dir)) {
                std::filesystem::create_directories(output_dir);
            }
        }
        int index = 0;
        while (does_file_exists(output_dir, q_name, index)) {
            index++;
        }
        
        dump_file(get_filename(output_dir, q_name, index), data_vector);
    }
}
template<class T>
void dump_file(std::filesystem::path filepath, const std::vector<T>& data_vector) {
    try {
        std::ofstream outfile(filepath, std::ios::binary | std::ios::out);
        const T* address = &data_vector[0];
        outfile.write(reinterpret_cast<const char*>(address), data_vector.size() * sizeof(T));
        outfile.close();
    } catch (...) {
        log_error("Error writing data file {}", filepath);
        exit(1);
    }
}

template<class T>
void dump_file(std::filesystem::path filepath, const T* data_vector_start, uint32_t data_vector_size_bytes) {
    try {
        std::ofstream outfile(filepath, std::ios::binary | std::ios::out);
        outfile.write(reinterpret_cast<const char*>(data_vector_start), data_vector_size_bytes);
        outfile.close();
    } catch (...) {
        log_error("Error writing data file {}", filepath);
        exit(1);
    }
}

template<class T>
void read_file(std::filesystem::path filepath, std::vector<T>& data_vector) {
    try {
        std::ifstream infile(filepath, std::ifstream::binary | std::ios::ate);

        std::ifstream::pos_type pos = infile.tellg();
        if (pos == 0) {
            data_vector = std::vector<T>{};
        } else {
            unsigned long num_bytes = pos;
            unsigned long num_floats = pos / sizeof(T);
            std::vector<char> byte_data(num_bytes);
            data_vector = std::vector<T>(num_floats);
            infile.seekg(0, std::ios::beg);
            infile.read(&byte_data[0], pos);
            infile.close();
            std::memcpy(&data_vector[0], &byte_data[0], byte_data.size());
        }
    } catch (...) {
        log_error("Error reading data file {}", filepath);
        exit(1);
    }
}
}  // namespace tt::data_binary

template void tt::data_binary::dump_file<float>(std::filesystem::path, const std::vector<float>&);
template void tt::data_binary::read_file<float>(std::filesystem::path, std::vector<float>&);
template void tt::data_binary::dump_file<uint32_t>(std::filesystem::path, const std::vector<uint32_t>&);
template void tt::data_binary::read_file<uint32_t>(std::filesystem::path, std::vector<uint32_t>&); 
template void tt::data_binary::dump_file<uint32_t>(std::filesystem::path, const uint32_t*, uint32_t);
template void tt::data_binary::dump_file<float>(std::filesystem::path, const float*, uint32_t);