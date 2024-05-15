// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <regex>
#include <string>
#include <unordered_map>

#include "model/tensor.hpp"
#include "verif_args.hpp"
#include "verif_test_config_types.hpp"
#include "yaml-cpp/yaml.h"

namespace verif::test_config {

static const string c_test_config_attr_name = "test-config";
static const string c_default_config_attr_name = "default";
static const string c_config_overrides_attr_name = "overrides";

//! Will read only the default config from the yaml file
VerifTestConfig read_from_yaml_file(
    const std::string &filepath, const std::optional<std::string> &default_filepath = std::nullopt);

template <class T>
std::unordered_map<std::string, T> read_configs_map_from_yaml_file(const std::string &filepath, const std::string& configs_name,
    std::function<T(const YAML::Node &node)> node_reader)
{
    std::unordered_map<std::string, T> configs;
    try {
        auto config_yaml = YAML::LoadFile(filepath);
        if (config_yaml[c_test_config_attr_name][configs_name]) {
            configs.insert({c_default_config_attr_name, node_reader(config_yaml[c_test_config_attr_name][configs_name])});
            if (config_yaml[c_test_config_attr_name][configs_name][c_config_overrides_attr_name]) {
                if (not config_yaml[c_test_config_attr_name][configs_name][c_config_overrides_attr_name].IsMap()) {
                    log_fatal("Test configuration overrides have to be a map with overrides: { key: config_map }");
                }
                for (const auto &it : config_yaml[c_test_config_attr_name][configs_name][c_config_overrides_attr_name]) {
                    configs.insert({it.first.as<std::string>(), node_reader(it.second)});
                }
            }
        }
    } catch (const std::exception &e) {
        log_fatal("verif_test_config::read_configs_map_from_yaml_file failure{}", e.what());
    } catch (...) {
        log_fatal("verif_test_config::read_configs_map_from_yaml_file failure -- Probably missing config in yaml");
    }
    return configs;
}

template <class T>
std::unordered_map<std::string, T> read_configs_map_with_defaults(const std::string &filepath, const std::string &default_filepath,
    const std::string& configs_name, std::function<T(const YAML::Node &node)> node_reader)
{
    std::unordered_map<std::string, T> base_configs = read_configs_map_from_yaml_file(filepath, configs_name, node_reader);
    std::unordered_map<std::string, T> default_configs = read_configs_map_from_yaml_file(default_filepath, configs_name, node_reader);
    // insert gives priority to the target map, ie. base_configs will not be overwritten
    base_configs.insert(default_configs.begin(), default_configs.end());
    return base_configs;
}

template <class T>
T get_config_from_map(const std::string key, const std::unordered_map<std::string, T> &configs)
{
    if (configs.find(key) != configs.end())
    {
        return configs.at(key);
    }

    // Checking for wildcard overrides.
    for (auto config_it = configs.begin(); config_it != configs.end(); ++config_it)
    {
        if (config_it->first == c_default_config_attr_name)
        {
            continue;
        }
        else if (std::regex_match(key.begin(), key.end(), std::regex(config_it->first)))
        {
            return config_it->second;
        }
    }

    auto default_config_it = configs.find(c_default_config_attr_name);
    if (default_config_it != configs.end())
    {
        return default_config_it->second;
    }

    log_fatal(
        "verif_test_config::get_config_from_map cannot find default or specific override - Missing default config");

    return T();
}

template <class T>
T read_config_from_yaml_file(const std::string &filepath, const std::string& config_name,
    std::function<T(const YAML::Node &node)> node_reader)
{
    T config;

    try
    {
        auto config_yaml = YAML::LoadFile(filepath);
        if (config_yaml[c_test_config_attr_name][config_name])
        {
            config = node_reader(config_yaml[c_test_config_attr_name][config_name]);
        }
    }
    catch (const std::exception &e)
    {
        log_fatal("verif_test_config::read_config_from_yaml_file failure {}", e.what());
    }
    catch (...)
    {
        log_fatal("verif_test_config::read_config_from_yaml_file failure -- Probably missing config in yaml");
    }

    return config;
}

template <class T>
T get(
    const std::unordered_map<std::string, std::string> &test_args,
    const std::string &option,
    const std::optional<T> &default_value = std::nullopt) {
    if (test_args.find(option) != test_args.end()) {
        return verif_args::parse<T>(test_args.at(option));
    }
    if (not default_value.has_value()) {
        throw std::runtime_error("Test arg not found!");
    }
    return default_value.value();
}

}  // namespace verif::test_config
