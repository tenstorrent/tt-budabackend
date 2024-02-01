// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

#include "op_model.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"

namespace tt {

float OpModelParams::get_param(
    const std::string &op_type, const std::string &param_type, const std::string &param_attr) const {
    std::string fallback_op = "default", fallback_param = "default";
    std::string op_type_with_fallback = op_type;
    std::string param_type_with_fallback = param_type + "_" + param_attr;

    // Look up op type -> fallback to default op type
    if (m_params.find(op_type) == m_params.end()) {
        op_type_with_fallback = fallback_op;
    }
    if (m_params.find(op_type_with_fallback) == m_params.end()) {
        log_error(
            "OpType '{}' not found in OpModelParams, and fallback option '{}' isn't defined",
            op_type,
            fallback_op);
        return 0;
    }

    const ParamMap &param_map = m_params.at(op_type_with_fallback);

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

OpModelParams::OpModelParams(const std::string &yaml_file_path) {
    std::ifstream file(yaml_file_path);
    if (!file.is_open()) {
        log_fatal("Failed to open {}", yaml_file_path);
        return;
    }
    try {
        YAML::Node yaml_data = YAML::Load(file);

        for (const auto &entry : yaml_data) {
            const std::string &op_type = entry.first.as<std::string>();
            const YAML::Node &params = entry.second;

            m_params.insert({op_type, ParamMap()});
            ParamMap &param_map = m_params.at(op_type);

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

}  // namespace tt
