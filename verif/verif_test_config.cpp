// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_test_config.hpp"

#include <cmath>

#include "verif_comparison.hpp"
#include "verif_stimulus.hpp"
#include "utils/logger.hpp"
#include "yaml-cpp/yaml.h"

namespace verif::test_config {

VerifTestConfig read_from_yaml_file(const std::string &filepath, const std::optional<std::string> &default_filepath) {
    VerifTestConfig config;
    try {
        auto config_yaml = YAML::LoadFile(filepath);
        if (config_yaml[c_test_config_attr_name]["test-args"]) {
            for (const auto &it : config_yaml[c_test_config_attr_name]["test-args"]) {
                config.test_args.insert({it.first.as<std::string>(), it.second.as<std::string>()});
            }
        }
        if (config_yaml[c_test_config_attr_name]["io-config"]) {
            for (const auto &it : config_yaml[c_test_config_attr_name]["io-config"]) {
                config.io_config.insert({it.first.as<std::string>(), it.second.as<std::vector<std::string>>()});
            }
        }
        if (default_filepath.has_value()) {
            config.comparison_configs = verif::comparison::read_configs_map_with_defaults(filepath, default_filepath.value());
            config.stimulus_configs = verif::stimulus::read_configs_map_with_defaults(filepath, default_filepath.value());
        } else {
            config.comparison_configs = verif::comparison::read_configs_map_from_yaml_file(filepath);
            config.stimulus_configs = verif::stimulus::read_configs_map_from_yaml_file(filepath);
        }
    } catch (const std::exception &e) {
        log_fatal("VerifTestConfig::read_from_yaml_file failure: {}", e.what());
    }
    return config;
}

}  // namespace verif::test_config