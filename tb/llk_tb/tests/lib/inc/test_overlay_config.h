// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

#include "llk_types.h"
#include "yaml-cpp/yaml.h"

namespace tests {

struct OverlayConfig {
    std::string graph_name;
    std::map<std::string, std::string> config;
};

void read_overlay_config_from_yaml(OverlayConfig &overlay_config, YAML::Node &test_descriptor);
void update_overlay_config_for_tensor(
    OverlayConfig &overlay_config, std::string tensor_key, llk::TensorDims tensor_dims, DataFormat data_format);
void update_overlay_config(OverlayConfig &overlay_config, std::string key, std::string value);

}  // namespace tests

YAML::Emitter &operator<<(YAML::Emitter &out, const tests::OverlayConfig &overlay_config);