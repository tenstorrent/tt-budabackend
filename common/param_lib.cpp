// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>

#include "param_lib.hpp"
#include "utils/logger.hpp"

namespace tt {

namespace param {
std::vector<std::string> get_lookup_contexts(const std::string &key) {
    std::string delimiter = "-";
    std::string tmp = key;
    std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
    std::vector<std::string> lookup;
    if (!tmp.empty()) {
        size_t current_pos = tmp.find(delimiter);
        while (current_pos != std::string::npos) {
            lookup.push_back(tmp.substr(0, current_pos));
            tmp.erase(0, current_pos + 1);
            current_pos = tmp.find(delimiter);
        }
        if (!tmp.empty()) {
            lookup.push_back(tmp);
        }
        log_info(tt::LogBackend, "Lookup contexts -- arch:{} scope:{} name:{}", lookup.at(0), lookup.at(1), lookup.at(2));
    }
    log_assert(lookup.size() == 3,"Lookup key does not follow Arch-Scope-Name triple format!");
    return lookup;
}

std::string get_lookup_key(const std::vector<std::string> &contexts) {
    log_assert(contexts.size() == 3, "Lookup context does not follow Arch-Scope-Name triplet format!");
    std::string tmp = contexts.at(0) + "-" + contexts.at(1) + "-" + contexts.at(2);
    std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
    return tmp;
}

tt_backend_params::tt_backend_params(const std::string &soc_desc_file_path, const std::string &eth_cluster_file_path, std::string runtime_params_ref_path, bool save) {
    populate_runtime_params(params, soc_desc_file_path, eth_cluster_file_path, runtime_params_ref_path, save);
    m_soc_desc_file_path = soc_desc_file_path;
    m_eth_cluster_file_path = eth_cluster_file_path;
}

// Get param if it exists, some are populated on demand.
std::string tt_backend_params::get_param(const std::string &key, std::string runtime_params_ref_path, bool save) {
    std::string tmp = key;
    std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
    if (params.find(tmp) != params.end()) {
        return params.at(tmp);
    } else if (get_lookup_contexts(key).at(0) == "system") {
        // Populate system level parameters only if system level query is made
        if(!runtime_system_params.size()) {
            std::tie(m_soc_desc_file_path, m_eth_cluster_file_path) = populate_runtime_system_params(
                runtime_system_params, m_soc_desc_file_path, m_eth_cluster_file_path, runtime_params_ref_path, save);
        }
        if (runtime_system_params.find(tmp) != runtime_system_params.end()) {
            return runtime_system_params.at(tmp);
        }
    }
    log_fatal("Lookup key = '{}' is invalid!", key);
    return "";
}

int tt_backend_params::get_int_param(const std::string &key, std::string runtime_params_ref_path, bool save) {
    return std::stoi(get_param(key, runtime_params_ref_path, save));
}

}  // namespace param
}  // namespace tt
