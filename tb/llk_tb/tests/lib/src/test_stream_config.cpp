// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_stream_config.h"
#include "stream_io_map.h"

#include <glog/logging.h>

#include <iostream>

using namespace tests;
using namespace std;

FifoType tests::fifo_type_from_string(std::string fifo_type_string) {
    if (fifo_type_string == "input") {
        return FifoType::Input;
    } else if (fifo_type_string == "output") {
        return FifoType::Output;
    } else {
        throw std::runtime_error("Incorrect fifo type");
    }
}
std::string tests::fifo_type_to_string(FifoType fifo_type) {
    if (fifo_type == FifoType::Input) {
        return "src_fifo";
    } else if (fifo_type == FifoType::Output) {
        return "streambuffer";
    } else {
        throw std::runtime_error("Incorrect fifo type");
    }
}
void tests::read_slice_config_from_yaml(SliceConfig &slice_config, const YAML::Node &slice_config_yaml) {
    if (slice_config_yaml["buffer-hex-path"]) {
        slice_config.buffer_hex_path = slice_config_yaml["buffer-hex-path"].as<string>();
    }
    if (slice_config_yaml["header-buffer-hex-path"]) {
        slice_config.header_buffer_hex_path = slice_config_yaml["header-buffer-hex-path"].as<string>();
    }
    if (slice_config_yaml["tile-id"]) {
        slice_config.tile_id = slice_config_yaml["tile-id"].as<int>();
    }
    if (slice_config_yaml["size-in-bytes"]) {
        slice_config.size_in_bytes = slice_config_yaml["size-in-bytes"].as<int>();
    }
    if (slice_config_yaml["compressed"]) {
        slice_config.compressed = slice_config_yaml["compressed"].as<bool>();
    }
}
void tests::read_section_config_from_yaml(SectionConfig &section_config, const YAML::Node &section_config_yaml) {
    if (section_config_yaml["size-in-bytes"]) {
        section_config.size_in_bytes = section_config_yaml["size-in-bytes"].as<int>();
    }
    if (section_config_yaml["data-format"]) {
        section_config.data_format = static_cast<DataFormat>(section_config_yaml["data-format"].as<int>());
    }
    if (section_config_yaml["hdr-size-in-bytes"]) {
        section_config.hdr_size_in_bytes = section_config_yaml["hdr-size-in-bytes"].as<int>();
    }
    if (section_config_yaml["section-repeat-count"]) {
        section_config.section_repeat_count = section_config_yaml["section-repeat-count"].as<int>();
    }
    if (section_config_yaml["compressed"]) {
        section_config.compressed = section_config_yaml["compressed"].as<bool>();
    }
    for (auto &slice_yaml : section_config_yaml["slices"]) {
        SliceConfig slice_config;
        read_slice_config_from_yaml(slice_config, slice_yaml);
        section_config.slice_configs.push_back(slice_config);
    }
}
void tests::read_stream_config_from_yaml(StreamConfig &stream_config, const YAML::Node &stream_config_yaml) {
    stream_config.user_set_yaml = stream_config_yaml;
    if (stream_config_yaml["src-name"]) {
        stream_config.name = stream_config_yaml["src-name"].as<string>();
    }
    if (stream_config_yaml["type"]) {
        stream_config.type = fifo_type_from_string(stream_config_yaml["type"].as<string>());
    }
    if (stream_config_yaml["is-result"]) {
        stream_config.is_result = stream_config_yaml["is-result"].as<bool>();
    }
    if (stream_config_yaml["has-previous-layer"]) {
        stream_config.has_previous_layer = stream_config_yaml["has-previous-layer"].as<bool>();
    }
    if (stream_config_yaml["stream"]) {
        stream_config.stream = stream_config_yaml["stream"].as<bool>();
    }
    if (stream_config_yaml["stream-id"]) {
        stream_config.stream_id = old_stream_id_to_new_stream_id(stream_config_yaml["stream-id"].as<uint32_t>());
    }
    if (stream_config_yaml["stream-phase"]) {
        stream_config.stream_phase = stream_config_yaml["stream-phase"].as<uint32_t>();
    }
    if (stream_config_yaml["stream-buffer-address"]) {
        stream_config.stream_buffer_address = stream_config_yaml["stream-buffer-address"].as<uint32_t>();
    }
    if (stream_config_yaml["stream-buffer-size"]) {
        stream_config.stream_buffer_size = stream_config_yaml["stream-buffer-size"].as<uint32_t>();
    }
    if (stream_config_yaml["stream-msg-buffer-address"]) {
        stream_config.stream_msg_buffer_address = stream_config_yaml["stream-msg-buffer-address"].as<uint32_t>();
    }
    if (stream_config_yaml["stream-msg-buffer-size"]) {
        stream_config.stream_msg_buffer_size = stream_config_yaml["stream-msg-buffer-size"].as<uint32_t>();
    }
    for (auto &section_yaml : stream_config_yaml["sections"]) {
        SectionConfig section_config;
        read_section_config_from_yaml(section_config, section_yaml);
        stream_config.section_configs.push_back(section_config);
    }
    stream_config.initialized = true;
}
void tests::initialize_stream_config(StreamConfig &stream_config) {
    stream_config.stream_msg_buffer_size = 16;
    stream_config.stream_buffer_size = 0;
    stream_config.section_configs.clear();
}
void tests::add_tile_to_stream_config(
    StreamConfig &stream_config,
    std::string buffer_hex_path,
    std::string header_hex_path,
    uint32_t size_in_bytes,
    DataFormat data_format,
    uint32_t tile_id) {
    int tile_size = size_in_bytes;
    if (tile_size % 16) {
        tile_size = tile_size + 16;
    }
    // Update Section Config based off tile
    assert(tile_size > 16);
    uint32_t tile_size_without_header = tile_size - 16;

    // Create a new section if the section config is empty, or if we have max tiles in the section.
    if (stream_config.section_configs.empty() ||
        (stream_config.section_configs.back().slice_configs.size() >= stream_config.max_tiles_for_buffer)) {
        stream_config.section_configs.push_back(SectionConfig(
            {.hdr_size_in_bytes = 16,
             .data_format = data_format,
             .section_id = static_cast<uint32_t>(stream_config.section_configs.size())}));
    }
    SectionConfig &section_config = stream_config.section_configs.back();
    assert(data_format == section_config.data_format && "Format of tile being added doesn't match section");

    // Update Slice Config based off tile added.
    section_config.slice_configs.push_back(SliceConfig({
        .buffer_hex_path = buffer_hex_path,
        .header_buffer_hex_path = header_hex_path,
        .size_in_bytes = tile_size_without_header,  // Doesn't include header size
        .tile_id = tile_id,
    }));
    section_config.size_in_bytes += tile_size;

    // Update Stream Config based off max section size
    stream_config.stream_buffer_size = std::max(section_config.size_in_bytes, stream_config.stream_buffer_size);
    stream_config.stream_msg_buffer_address = stream_config.stream_buffer_address + stream_config.stream_buffer_size;
}
std::map<string, string> tests::get_map_from_slice_config(const SliceConfig &slice_config) {
    std::map<string, string> config_map;
    config_map["file"] = slice_config.buffer_hex_path;
    config_map["header"] = slice_config.header_buffer_hex_path;
    config_map["size_in_bytes"] = to_string(slice_config.size_in_bytes);
    config_map["tile_id"] = to_string(slice_config.tile_id);
    config_map["compressed"] = slice_config.compressed ? "true" : "false";
    config_map["rep_id"] = "1";
    config_map["zplane_mask"] = "0";
    config_map["x"] = "0x100";
    config_map["y"] = "0x01";
    config_map["z"] = "0x04";
    config_map["w"] = "0x01";

    return config_map;
}
std::map<string, string> tests::get_map_from_section_config(const SectionConfig &section_config) {
    std::map<string, string> config_map;
    config_map["data_format"] = to_string(static_cast<int>(section_config.data_format));
    config_map["compressed"] = section_config.compressed ? "true" : "false";
    config_map["size_in_bytes"] = to_string(section_config.size_in_bytes);
    config_map["hdr_size_in_bytes"] = to_string(section_config.hdr_size_in_bytes);
    config_map["section_repeat_count"] = to_string(section_config.section_repeat_count);
    config_map["tile_table_size_in_bytes"] = "16";
    config_map["fifo_pos"] = "0";
    config_map["section_id"] = to_string(section_config.section_id);
    return config_map;
}
std::map<string, string> tests::get_map_from_stream_config(const StreamConfig &stream_config) {
    std::map<string, string> config_map;
    config_map["src_name"] = stream_config.name;
    config_map["type"] = fifo_type_to_string(stream_config.type);
    config_map["is_result"] = stream_config.is_result ? "true" : "false";
    config_map["has_previous_layer"] = stream_config.has_previous_layer ? "true" : "false";
    config_map["stream"] = stream_config.stream ? "true" : "false";
    config_map["stream_id"] = to_string(stream_config.stream_id);
    config_map["stream_start_phase"] = to_string(stream_config.stream_phase);
    config_map["stream_buffer_base"] = to_string(stream_config.stream_buffer_address);
    config_map["stream_buffer_size"] = to_string(stream_config.stream_buffer_size);
    config_map["stream_msg_buffer_base"] = to_string(stream_config.stream_msg_buffer_address);
    config_map["stream_msg_buffer_size"] = to_string(stream_config.stream_msg_buffer_size);
    config_map["fifo_base_addr"] = "0x00000000";
    config_map["fifo_size"] = "0x00000000";
    config_map["interactive-mode"] = "true";
    return config_map;
}

void tests::get_testdef_yaml_from_stream_configs(YAML::Node &testdef_yaml, std::vector<StreamConfig> &stream_configs) {
    testdef_yaml["check"] = {};
    std::map<std::string, std::string> core_node_map = {{"x", "0"}, {"y", "0"}};
    YAML::Node core_node = YAML::Node(core_node_map);
    for (auto &stream_config : stream_configs) {
        YAML::Node stream_config_yaml = YAML::Node(get_map_from_stream_config(stream_config));
        stream_config_yaml["node"] = core_node;
        stream_config_yaml["tvm_checker"] = "true";
        int section_index = 0;
        for (auto &section_config : stream_config.section_configs) {
            stream_config_yaml["sections"].push_back(YAML::Node(get_map_from_section_config(section_config)));
            for (auto &slice_config : section_config.slice_configs) {
                stream_config_yaml["sections"][section_index]["slices"].push_back(
                    YAML::Node(get_map_from_slice_config(slice_config)));
            }
            section_index++;
        }
        testdef_yaml["check"].push_back(stream_config_yaml);
    }

    VLOG(2) << testdef_yaml;
    google::FlushLogFiles(google::INFO);
}

YAML::Emitter &operator<<(YAML::Emitter &out, const tests::StreamConfig &stream_config) {
    out << stream_config.user_set_yaml;
    return out;
}