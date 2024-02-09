// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tt::data_binary {
bool does_file_exists(const std::string& root, const std::string& q_name, int entry_index);
std::string get_filename(const std::string& root, const std::string& q_name, int entry_index);
void dump_file_on_env_flags(const std::string& q_name, const std::vector<float>& data_vector);
template<class T>
void dump_file(std::filesystem::path filepath, const std::vector<T>& data_vector);
template<class T>
void dump_file(std::filesystem::path filepath, const T* data_vector_start, uint32_t data_vector_size_bytes);
template<class T>
void read_file(std::filesystem::path filepath, std::vector<T>& data_vector);

}  // namespace tt::data_binary