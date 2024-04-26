// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

namespace tests {

struct KernelParams {
    std::map<std::string, std::string> param_mappings = {};
    std::string templateFile = "";
    std::string outputFile = "";
};

void clear_param_mapping(KernelParams &kernel_params);
void del_param_mapping(KernelParams &kernel_params, std::string key);
void add_param_mapping(KernelParams &kernel_params, std::string key, std::string val);
void add_param_mapping(KernelParams &kernel_params, std::map<std::string, std::string> &override_params);
void debug(KernelParams &kernel_params);

}  // namespace tests