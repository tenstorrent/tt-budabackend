// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_kernel_params.h"

#include <fstream>
#include <iostream>

void tests::del_param_mapping(KernelParams &kernel_params, std::string key) { kernel_params.param_mappings.erase(key); }
void tests::add_param_mapping(KernelParams &kernel_params, std::string key, std::string val) {
    kernel_params.param_mappings[key] = val;
}
void tests::add_param_mapping(KernelParams &kernel_params, std::map<std::string, std::string> &override_params) {
    for (const auto &it : override_params) {
        kernel_params.param_mappings[it.first] = it.second;
    }
}
void tests::clear_param_mapping(KernelParams &kernel_params) { kernel_params.param_mappings.clear(); }
void tests::debug(KernelParams &kernel_params) {
    std::cout << "---------------" << std::endl;
    for (const auto &it : kernel_params.param_mappings) {
        std::cout << "Param[" << it.first << "]: " << it.second << std::endl;
    }
    std::cout << "---------------" << std::endl;
}
