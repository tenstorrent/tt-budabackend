// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_overlay_config.h"

void tests::read_overlay_config_from_yaml(OverlayConfig &overlay_config, YAML::Node &test_descriptor) {
    // Graph name
    if (not test_descriptor["overlay-config"]) {
        throw std::runtime_error("overlay-config needs to be defined within the yaml file");
    }
    if (not test_descriptor["overlay-config"]["graph_name"]) {
        throw std::runtime_error("overlay-config graph_name needs to be defined within the yaml file");
    }
    overlay_config.graph_name = test_descriptor["overlay-config"]["graph_name"].as<std::string>();

    // Read inputs and set tile-sizes and num-msgs
    for (auto const &it : test_descriptor["test-config"]["tensor-config"]) {
        auto tensor_string = it.first.as<std::string>();
        auto tensor_dims_vector = it.second["dims"].as<std::vector<int>>();
        auto tensor_data_format = llk::get_data_format(it.second["data-format"].as<std::string>());

        llk::TensorDims tensor_dims(tensor_dims_vector);
        update_overlay_config_for_tensor(overlay_config, tensor_string, tensor_dims, tensor_data_format);
    }

    // overlay overrides from yaml
    std::map<std::string, std::string> overlay_config_override;
    if (test_descriptor["overlay-config"]) {
        overlay_config_override = test_descriptor["overlay-config"].as<std::map<std::string, std::string>>();
        for (const auto &it : overlay_config_override) {
            overlay_config.config[it.first] = it.second;
        }
    }
}

void tests::update_overlay_config_for_tensor(
    OverlayConfig &overlay_config, std::string tensor_key, llk::TensorDims tensor_dims, DataFormat data_format) {
    std::string tile_size_string = to_string(tensor_dims.tile_bytes_when_assembled(data_format));
    std::string num_tiles_string = to_string(tensor_dims.num_tiles());
    overlay_config.config["num_" + tensor_key + "_park_buf_msgs"] = num_tiles_string;
    overlay_config.config["num_" + tensor_key + "_data_buf_msgs"] = num_tiles_string;
    overlay_config.config[tensor_key + "_park_buf_tile_size"] = tile_size_string;
    overlay_config.config[tensor_key + "_data_buf_tile_size"] = tile_size_string;
}

void tests::update_overlay_config(OverlayConfig &overlay_config, std::string key, std::string value) {
    overlay_config.config[key] = value;
}

YAML::Emitter &operator<<(YAML::Emitter &out, const tests::OverlayConfig &overlay_config) {
    out << YAML::Key << "overlay-config";
    out << overlay_config.config;
    return out;
}