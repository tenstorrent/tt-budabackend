// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt {

using ParamMap = std::unordered_map<std::string, float>;

class OpModelParams {
   public:
    float get_param(
        const std::string &op_type,
        const std::uint32_t version,
        const std::string &param_type,
        const std::string &param_attr = "") const;

    explicit OpModelParams(const std::string &params_dir_path, const std::uint32_t total_versions);

   private:
    // Each op has its own map of params - ParamOp.
    // Each update of params of one or multiple ops triggers a new version of all op params.
    // This is needed to decouple op params updates from PyBuda updates of BudaBackend.
    //
    // m_params is a vector of op params - each element keeps op params for version i+1, where i is the index of the
    // vector. Versioning starts from 1, and no version can be skipped. Each version is loaded from a yaml file that is
    // located in params_dir_path passed to the constructor.
    std::vector<std::unordered_map<std::string, ParamMap>> m_params;
};

}  // namespace tt
