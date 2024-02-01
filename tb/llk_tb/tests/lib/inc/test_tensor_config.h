// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "llk_tensor_data_format.h"
#include "llk_tensor_dims.h"
#include "test_stream_config.h"
#include "utils/hash.h"
#include "yaml-cpp/yaml.h"

using namespace std;

namespace tests {

struct TensorConfig {
    std::string name = "";
    uint32_t address = 0;
    llk::TensorDims dims = {};
    DataFormat data_format;
    map<string, string> stimulus_config;
    StreamConfig stream_config;

    TensorConfig();
    TensorConfig(llk::TensorDims dims, DataFormat data_format);
    TensorConfig(llk::TensorDims dims, DataFormat data_format, string name);
};

void read_stimulus_config_from_yaml(TensorConfig &tensor_config, const YAML::Node &stimulus_config);
void read_stream_config_from_yaml(TensorConfig &tensor_config, const YAML::Node &stream_config);

}  // namespace tests

namespace std {
template <>
struct hash<tests::TensorConfig> {
    inline size_t operator()(const tests::TensorConfig &tensor_config) const {
        // size_t value = your hash computations over x
        size_t current_hash = tensor_config.dims.x;
        ::hash_combine(current_hash, tensor_config.dims.y);
        ::hash_combine(current_hash, tensor_config.dims.w);
        ::hash_combine(current_hash, tensor_config.dims.z);
        ::hash_combine(current_hash, tensor_config.name);
        ::hash_combine(current_hash, static_cast<uint>(tensor_config.data_format));
        for (auto &it : tensor_config.stimulus_config) {
            ::hash_combine(current_hash, it.first);
            ::hash_combine(current_hash, it.second);
        }
        ::hash_combine(current_hash, tensor_config.stream_config);
        return current_hash;
    }
};
}  // namespace std