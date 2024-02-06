// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "device/tt_soc_descriptor.h"

class buda_SocDescriptor : public tt_SocDescriptor {
   public:
    buda_SocDescriptor(const std::string& device_descriptor_path);
    buda_SocDescriptor(const tt_SocDescriptor& other);
    buda_SocDescriptor() = default;
    const std::vector<std::size_t>& get_trisc_sizes() const;
    const std::unordered_map<tt_xy_pair, std::vector<tt_xy_pair>>& get_perf_dram_bank_to_workers() const;

   private:
    void map_workers_to_dram_banks();
    std::vector<std::size_t> trisc_sizes;
    std::unordered_map<tt_xy_pair, std::vector<tt_xy_pair>> perf_dram_bank_to_workers;
};
std::unique_ptr<buda_SocDescriptor> load_soc_descriptor_from_yaml(const std::string& device_descriptor_file_path);