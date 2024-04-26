// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "llk_tensor_data_format.h"
#include "llk_tensor_dims.h"
#include "utils/hash.h"
#include "yaml-cpp/yaml.h"

namespace tests {

enum class FifoType { Input, Output };
FifoType fifo_type_from_string(std::string fifo_type_string);
std::string fifo_type_to_string(FifoType fifo_type);

struct SliceConfig {
    std::string buffer_hex_path = "";
    std::string header_buffer_hex_path = "";
    uint32_t size_in_bytes = 0;
    uint32_t tile_id = 0;
    bool compressed = false;
};

struct SectionConfig {
    uint32_t section_id = 0;
    uint32_t size_in_bytes = 0;
    uint32_t hdr_size_in_bytes = 0;
    bool section_repeat_count = false;
    bool compressed = false;
    DataFormat data_format = DataFormat::Float16;
    std::vector<SliceConfig> slice_configs = {};
};

struct StreamConfig {
    bool initialized = false;
    std::string name = "";
    FifoType type = FifoType::Input;
    bool has_previous_layer = false;
    bool is_result = false;
    bool stream = true;
    uint32_t stream_id = 0;
    uint32_t stream_phase = 1;
    uint32_t stream_buffer_address = 0;
    uint32_t stream_buffer_size = 0;
    uint32_t stream_msg_buffer_address = 0;
    uint32_t stream_msg_buffer_size = 0;
    uint32_t max_tiles_for_buffer = UINT32_MAX;
    std::vector<SectionConfig> section_configs = {};
    YAML::Node user_set_yaml;
};

void initialize_stream_config(StreamConfig &stream_config);
void add_tile_to_stream_config(
    StreamConfig &stream_config,
    std::string buffer_hex_path,
    std::string header_hex_path,
    uint32_t size_in_bytes,
    DataFormat data_format,
    uint32_t tile_id);
void read_slice_config_from_yaml(SliceConfig &slice_config, const YAML::Node &slice_config_yaml);
void read_section_config_from_yaml(SectionConfig &section_config, const YAML::Node &section_config_yaml);
void read_stream_config_from_yaml(StreamConfig &stream_config, const YAML::Node &stream_config_yaml);
std::map<std::string, std::string> get_map_from_slice_config(const SliceConfig &slice_config);
std::map<std::string, std::string> get_map_from_section_config(const SectionConfig &section_config);
std::map<std::string, std::string> get_map_from_stream_config(const StreamConfig &stream_config);
void get_testdef_yaml_from_stream_configs(YAML::Node &testdef_yaml, std::vector<StreamConfig> &stream_configs);

}  // namespace tests

YAML::Emitter &operator<<(YAML::Emitter &out, const tests::StreamConfig &stream_config);

namespace std {
template <>
struct hash<tests::StreamConfig> {
    inline size_t operator()(const tests::StreamConfig &stream_config) const {
        // size_t value = your hash computations over x
        size_t current_hash = stream_config.stream_id;
        ::hash_combine(current_hash, stream_config.stream_id);
        ::hash_combine(current_hash, stream_config.stream_phase);
        ::hash_combine(current_hash, stream_config.stream_buffer_address);
        ::hash_combine(current_hash, stream_config.stream_buffer_size);
        ::hash_combine(current_hash, stream_config.stream_msg_buffer_address);
        ::hash_combine(current_hash, stream_config.stream_msg_buffer_size);
        ::hash_combine(current_hash, stream_config.initialized);
        ::hash_combine(current_hash, stream_config.name);
        return current_hash;
    }
};
}  // namespace std