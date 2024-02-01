// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "op_model.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"

namespace tt {

float OpModelParams::get_param(
    const std::string &op_type,
    const std::uint32_t version,
    const std::string &param_type,
    const std::string &param_attr) const {
    if (version == 0) {
        log_error("Param versioning starts from 1. Returning 0 for param {}.", param_type);
        return 0;
    }
    if (version > m_params.size()) {
        log_error("Version {} of param {} not found. Returning 0.", version, param_type);
        return 0;
    }
    const int version_index = version - 1;

    std::string fallback_op = "default", fallback_param = "default";
    std::string op_type_with_fallback = op_type;
    std::string param_type_with_fallback = param_type + "_" + param_attr;

    // Look up op type -> fallback to default op type
    if (m_params[version_index].find(op_type) == m_params[version_index].end()) {
        op_type_with_fallback = fallback_op;
    }
    if (m_params[version_index].find(op_type_with_fallback) == m_params[version_index].end()) {
        log_error(
            "OpType '{}' not found in OpModelParams, and fallback option '{}' isn't defined", op_type, fallback_op);
        return 0;
    }

    const ParamMap &param_map = m_params[version_index].at(op_type_with_fallback);

    // Look up param with attr -> fallback to param without attr -> fallback to default param
    if (param_map.find(param_type_with_fallback) == param_map.end()) {
        param_type_with_fallback = param_type;
    }
    if (param_map.find(param_type_with_fallback) == param_map.end()) {
        param_type_with_fallback = fallback_param;
    }
    if (param_map.find(param_type_with_fallback) == param_map.end()) {
        log_error(
            "ParamType '{}' not found in OpModelParams for OpType '{}', and fallback option '{}' isn't defined",
            param_type,
            op_type,
            fallback_param);
        return 0;
    }
    if (op_type_with_fallback == fallback_op or param_type_with_fallback == fallback_param) {
        log_warning(
            tt::LogModel,
            "OpType::ParamType look up '{}::{}' not found in OpModelParams, using fallback option '{}::{}'",
            op_type,
            param_type,
            op_type_with_fallback,
            param_type_with_fallback);
    }
    return param_map.at(param_type_with_fallback);
}

OpModelParams::OpModelParams(const std::string &params_dir_path, const std::uint32_t total_versions) {
    if (!std::filesystem::is_directory(params_dir_path)) {
        log_fatal("Op params directory {} doesn't exist!", params_dir_path);
    }

    // Each version of params is stored in a separate file named params_v{version}.yaml
    for (std::uint32_t version = 1; version <= total_versions; version++) {
        std::string params_file_name = params_dir_path + "/params_v" + std::to_string(version) + ".yaml";
        std::ifstream params_file(params_file_name);
        if (!params_file.is_open()) {
            log_fatal("Failed to open op params file {} for version {}.", params_file_name, version);
        }
        const std::uint32_t version_index = version - 1;

        m_params.push_back(std::unordered_map<std::string, ParamMap>());

        try {
            YAML::Node yaml_data = YAML::Load(params_file);

            for (const auto &entry : yaml_data) {
                const std::string &op_type = entry.first.as<std::string>();
                const YAML::Node &params = entry.second;

                m_params[version_index].insert({op_type, ParamMap()});
                ParamMap &param_map = m_params[version_index].at(op_type);

                for (const auto &param_entry : params) {
                    const std::string &param_type = param_entry.first.as<std::string>();
                    const float &param_value = param_entry.second.as<float>();
                    param_map[param_type] = param_value;
                }
            }
        } catch (const YAML::Exception &e) {
            std::ostringstream error_stream;
            error_stream << "Error while parsing YAML: " << e.what();
            log_fatal("{}", error_stream.str());
        }
    }
}

}  // namespace tt
