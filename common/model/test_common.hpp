// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <algorithm>
#include "common/buda_soc_descriptor.h"
#include <filesystem>
// Needed for TargetDevice enum
#include "common/base.hpp"

inline tt::ARCH arch_name_from_string(const std::string &arch_name_string) {
    if (arch_name_string.compare("none") == 0) {
        return tt::ARCH::Invalid;
    } else if (arch_name_string.compare("grayskull") == 0) {
        return tt::ARCH::GRAYSKULL;

    } else if (arch_name_string.compare("wormhole") == 0) {
        return tt::ARCH::WORMHOLE;

    } else if (arch_name_string.compare("wormhole_b0") == 0) {
        return tt::ARCH::WORMHOLE_B0;

    } else {
        throw std::runtime_error("Unknown arch_name_string" + arch_name_string);
    }
}

inline std::string get_machine_description_file(const tt::ARCH &arch) {
    // Some old code in model still use this function. We can remove this completely when model/ is deleted
    log_fatal("Machine description file is no longer used in backend");

    string buda_home;
    if (getenv("BUDA_HOME")) {
        buda_home = getenv("BUDA_HOME");
    } else { 
        buda_home = "./";
    }
    if (buda_home.back() != '/') {
        buda_home += "/";
    }
    switch (arch) {
        case tt::ARCH::Invalid: return buda_home+ "machine_desc_max_avail.txt"; // will be overwritten in tt_global_state constructor
        case tt::ARCH::JAWBRIDGE: throw std::runtime_error("JAWBRIDGE arch not supported");
        case tt::ARCH::GRAYSKULL: return buda_home+ "machine_desc_max_avail.txt";
        case tt::ARCH::WORMHOLE: return buda_home+ "machine_desc_wormhole.txt";
        case tt::ARCH::WORMHOLE_B0: return buda_home+ "machine_desc_wormhole_b0.txt";
        default: throw std::runtime_error("Unsupported device arch");
    };
    return "";
}
inline std::string get_soc_description_file(string build_dir){
    if(std::filesystem::exists(build_dir + "/device_descs/"))
        return build_dir + "/device_descs/";
    return build_dir + "/device_desc.yaml";
}

inline std::string get_soc_description_file(const tt::ARCH &arch, tt::TargetDevice target_device, string output_dir = "", bool harvested = false, tt_xy_pair grid_size = tt_xy_pair(0, 0)) {

    // Ability to skip this runtime opt, since trimmed SOC desc limits which DRAM channels are available.
    bool use_full_soc_desc = getenv("FORCE_FULL_SOC_DESC");
    string buda_home;
    if (getenv("BUDA_HOME")) {
        buda_home = getenv("BUDA_HOME");
    } else { 
        buda_home = "./";
    }
    if (buda_home.back() != '/') {
        buda_home += "/";
    }
    if (target_device == tt::TargetDevice::Versim && !use_full_soc_desc) {
        log_assert(output_dir != "", "Output directory path is not set. In versim, soc-descriptor must get generated and copied to output-dir.");
        return output_dir + "/device_desc.yaml";
    } 
    else {
        tt_xy_pair grid_size_to_use;
        if(grid_size.x > 0 && grid_size.y > 0) {
            grid_size_to_use = grid_size;
        }
        else {
            std::unordered_map<tt::ARCH, tt_xy_pair> arch_default_grid_size = {{tt::ARCH::GRAYSKULL, {10, 12}}, {tt::ARCH::WORMHOLE, {8, 10}}, {tt::ARCH::WORMHOLE_B0, {8, 10}}};
            grid_size_to_use = arch_default_grid_size.at(arch);
        }
        std::string soc_desc_file = buda_home + "device/" + get_string_lowercase(arch) + "_" + std::to_string(grid_size_to_use.x) + "x" + std::to_string(grid_size_to_use.y);
        if((arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) and harvested) {
            soc_desc_file += "_harvested";
        }
        soc_desc_file += ".yaml";
        log_assert(std::filesystem::exists(soc_desc_file), "{} does not exist for arch {} with dimensions {}x{}", soc_desc_file, arch, grid_size.x, grid_size.y);
        return soc_desc_file;
    }
    return "";
}
