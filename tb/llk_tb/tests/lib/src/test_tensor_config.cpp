// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_tensor_config.h"

#include <string>

tests::TensorConfig::TensorConfig(llk::TensorDims dims, DataFormat data_format) :
    dims(dims), data_format(data_format){};
tests::TensorConfig::TensorConfig(llk::TensorDims dims, DataFormat data_format, string name) :
    dims(dims), data_format(data_format), name(name){};

tests::TensorConfig::TensorConfig() {
    dims = llk::TensorDims(0, 0, 0, 0);
    data_format = DataFormat::Bfp8;
};

void tests::read_stimulus_config_from_yaml(TensorConfig &tensor_config, const YAML::Node &stimulus_config) {
    for (auto const &it : stimulus_config) {
        auto key_string = it.first.as<std::string>();
        auto val_string = it.second.as<std::string>();
        tensor_config.stimulus_config[key_string] = val_string;
    }
}

void tests::read_stream_config_from_yaml(TensorConfig &tensor_config, const YAML::Node &stream_config) {
    read_stream_config_from_yaml(tensor_config.stream_config, stream_config);
}
