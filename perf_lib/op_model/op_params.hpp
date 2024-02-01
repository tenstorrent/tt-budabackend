// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tt {

using ParamMap = std::unordered_map<std::string, float>;

class OpModelParams {
   public:
    float get_param(
        const std::string &op_type, const std::string &param_type, const std::string &param_attr = "") const;

    explicit OpModelParams(const std::string &yaml_file_path);

   private:
    std::unordered_map<std::string, ParamMap> m_params;
};

}  // namespace tt
