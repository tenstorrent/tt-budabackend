// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <unistd.h>
#include <map>
#include <vector>
#include <string>
#include <tuple>
#include "env_lib.hpp"
#include <experimental/filesystem>
#include "device/tt_arch_types.h"

namespace fs = std::experimental::filesystem;
namespace tt {

//! Utility functions for various backend params
namespace param {
// Implemented by runtime to fill the param store
extern void populate_runtime_params(
    std::map<std::string, std::string> &params,
    const std::string &soc_desc_file_path,
    const std::string &eth_cluster_file_path,
    std::string runtime_params_ref_path = "",
    bool store = false);

// Implemented by runtime to fill system/device params on query
extern std::tuple<std::string, std::string> populate_runtime_system_params(
    std::map<std::string, std::string> &params,
    const std::string &soc_desc_file_path,
    const std::string &eth_cluster_file_path,
    std::string runtime_params_ref_path = "",
    bool store = false);

// Implemented by golden to fill the param store
// extern void populate_golden_params(std::unordered_map<std::string, std::string> &params);

// Implemented by coremodel to fill the param store
// extern void populate_model_params(std::unordered_map<std::string, std::string> &params);
std::vector<std::string> get_lookup_contexts(const std::string &key);

std::string get_lookup_key(const std::vector<std::string> &contexts);

// Device Description given to FE
struct DeviceDesc {
    tt::ARCH arch = tt::ARCH::Invalid;
    std::string soc_desc_yaml = "";
    bool mmio = false;
    std::uint32_t harvesting_mask = 0;
};

// Used to generate custom DeviceDesc objects (each contains a path to a SOC Descriptor) or DeviceDesc objects for connected devices
void create_soc_descriptor(const std::string& file_name, tt::ARCH arch, std::uint32_t harvesting_mask = 0, std::pair<int, int> grid_dim = {0, 0});
std::string get_device_yaml_based_on_config(tt::ARCH arch, std::uint32_t harvesting_mask = 0, std::pair<int, int> grid_dim = {0, 0}, const std::string& out_dir = "./tt_build");
std::tuple<std::string, std::uint32_t> get_device_yaml_at_index(std::size_t device_index, const std::string& out_dir = "./tt_build");
DeviceDesc get_custom_device_desc(tt::ARCH arch, bool mmio = false, std::uint32_t harvesting_mask = 0, std::pair<int, int> grid_dim = {0, 0}, const std::string& out_dir = "./tt_build");
std::vector<DeviceDesc> get_device_descs(const std::string& out_dir);
// Returns a path to a cluster Descriptor (used for WH)
std::string get_device_cluster_yaml(const std::string& out_dir);

// Backend param store
// It's a singleton that should be retrieved via get()
struct tt_backend_params {
    public:
    std::map<std::string, std::string> params;  // ordered for dumping

    std::string get_param(const std::string &key, std::string runtime_params_ref_path = "", bool save = false);
    int get_int_param(const std::string &key, std::string runtime_params_ref_path = "", bool save = false);
    std::string get_cluster_config_file();
    inline static std::map<std::string, std::string> runtime_system_params = {}; // cache system level parameters
    static std::map<std::string, tt_backend_params>& get_instances() {
        static std::map<std::string, tt_backend_params> instances;
        return instances;
    }

    static tt_backend_params& get(const std::string &soc_desc_file_path, const std::string &eth_cluster_file_path, std::string runtime_params_ref_path = "", bool save = false) {

        std::string key = soc_desc_file_path + eth_cluster_file_path + runtime_params_ref_path;

        auto &instances = get_instances();
        auto match = instances.find(key);
        if (instances.find(key) == instances.end() or save) {
            bool tmp;
            std::tie(match, tmp) = instances.emplace(
                key, tt_backend_params(soc_desc_file_path, eth_cluster_file_path, runtime_params_ref_path, save));
        }
        return match->second;
    }

    static void reset() {
        runtime_system_params.clear();
        get_instances().clear();
    }

    private:
    tt_backend_params(const std::string &soc_desc_file_path, const std::string &eth_cluster_file_path, std::string runtime_params_ref_path = "", bool save = false);
    std::string m_eth_cluster_file_path;
    std::string m_soc_desc_file_path;

    public:
    tt_backend_params(tt_backend_params&&)       = default;
    tt_backend_params(tt_backend_params const&)  = delete;
    void operator=(tt_backend_params const&)     = delete;
};

}  // namespace param
}  // namespace tt
