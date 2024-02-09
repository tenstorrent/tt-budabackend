// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "sparse_matmul_params.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

#include "utils/logger.hpp"

namespace tt {

SparseMatmulParams::SparseMatmulParams(const std::string& yaml_file_path) : name(yaml_file_path) {
    std::ifstream file(yaml_file_path);
    if (!file.is_open()) {
        log_fatal("Failed to open {}", yaml_file_path);
        return;
    }

    try {
        YAML::Node yaml_data = YAML::Load(file);

        // Load sparse matmul parameters into a map;
        // Structure of the map can be seen in the header file and in the yaml files being loaded
        // (params/wormhole_b0_sparse_params_<fidelity>.yaml)
        for (const auto& ublock_rt_entry : yaml_data) {
            // Extract integer dimension from the key
            auto ublock_rt_key = ublock_rt_entry.first.as<std::string>();
            uint32_t ublock_rt = atoi(&ublock_rt_key.back());

            params_by_dimensions.emplace(
                ublock_rt, std::unordered_map<uint32_t, std::unordered_map<uint32_t, SparseMatmulStageCycles>>());

            for (const auto& ublock_ct_entry : ublock_rt_entry.second) {
                auto ublock_ct_key = ublock_ct_entry.first.as<std::string>();
                uint32_t ublock_ct = atoi(&ublock_ct_key.back());

                params_by_dimensions[ublock_rt].emplace(
                    ublock_ct, std::unordered_map<uint32_t, SparseMatmulStageCycles>());

                for (const auto& ublock_kt_entry : ublock_ct_entry.second) {
                    auto ublock_kt_key = ublock_kt_entry.first.as<std::string>();
                    uint32_t ublock_kt = atoi(&ublock_kt_key.back());

                    params_by_dimensions[ublock_rt][ublock_ct].emplace(ublock_kt, SparseMatmulStageCycles());

                    for (const auto& stage_cycles : ublock_kt_entry.second) {
                        params_by_dimensions[ublock_rt][ublock_ct][ublock_kt].emplace(
                            stage_cycles.first.as<std::string>(), stage_cycles.second.as<uint32_t>());
                    }
                }
            }
        }
    } catch (const YAML::Exception& e) {
        std::ostringstream error_stream;
        error_stream << "Error while parsing YAML: " << e.what();
        log_fatal("{}", error_stream.str());
    }
}

const std::unordered_map<std::string, uint32_t>& SparseMatmulParams::get_params(
    uint32_t ublock_rt, uint32_t ublock_ct, uint32_t ublock_kt) {
    // If we don't have the exact dimension in the cycles_by_dimensions map,
    // fallback to the next smaller one as the worst case;
    //
    // For example, if we had ublock [3, 2] and u_kt = 5, we would use cycles for ublock [2, 2] and u_kt = 4,
    // since we don't have exact cycles for ublock_rt = 3 and u_kt = 5

    log_assert(
        params_by_dimensions.find(1) != params_by_dimensions.end() &&
            params_by_dimensions.at(1).find(1) != params_by_dimensions.at(1).end() &&
            params_by_dimensions.at(1).at(1).find(1) != params_by_dimensions.at(1).at(1).end(),
        "Worst case scenario (ub_r: 1, ub_c: 1, u_kt: 1) not present in the map.");

    uint32_t ublock_rt_key = ublock_rt;
    while (params_by_dimensions.find(ublock_rt_key) == params_by_dimensions.end()) ublock_rt_key--;

    // Insert the obtained value into the map for future use in case there was a fallback to the lower dimension
    if (ublock_rt_key != ublock_rt) {
        params_by_dimensions[ublock_rt] = params_by_dimensions.at(ublock_rt_key);
    }

    uint32_t ublock_ct_key = ublock_ct;
    while (params_by_dimensions.at(ublock_rt_key).find(ublock_ct_key) == params_by_dimensions.at(ublock_rt_key).end())
        ublock_ct_key--;

    if (ublock_ct_key != ublock_ct) {
        params_by_dimensions[ublock_rt][ublock_ct] = params_by_dimensions.at(ublock_rt).at(ublock_ct_key);
    }

    uint32_t ublock_kt_key = ublock_kt;
    while (params_by_dimensions.at(ublock_rt_key).at(ublock_ct_key).find(ublock_kt_key) ==
           params_by_dimensions.at(ublock_rt_key).at(ublock_ct_key).end())
        ublock_kt_key--;

    if (ublock_kt_key != ublock_kt) {
        params_by_dimensions[ublock_rt][ublock_ct][ublock_kt] = params_by_dimensions.at(ublock_rt).at(ublock_ct).at(ublock_kt_key);
    }

    return params_by_dimensions.at(ublock_rt).at(ublock_ct).at(ublock_kt);
}

}  // namespace tt