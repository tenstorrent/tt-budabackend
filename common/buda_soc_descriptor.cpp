// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "buda_soc_descriptor.h"

#include "utils/logger.hpp"
#include "l1_address_map.h"

void buda_SocDescriptor::map_workers_to_dram_banks() {
    // This functionality should be copied over from pipegen2 :: perf_info_manager_internal
    // Plus it doesnt look optimized on WH, nor BH. 
    // On WH: dram ch2 5-0 and ch3 5-2 are mapped to 4 cores only
    // On BH: algorithm doesnt map any workers to dram ch0 0-0 and ch4 9-0

    log_assert(dram_cores.size() > 0, "No DRAM cores found");

    std::vector<std::vector<tt_xy_pair>> dram_cores_per_channel; // take first core of each channel
    for (std::vector<tt_xy_pair>& channel_cores : dram_cores) {
        dram_cores_per_channel.push_back(std::vector<tt_xy_pair>());
        dram_cores_per_channel.back().push_back(channel_cores[0]);
    }

    for (tt_xy_pair worker : workers) {
        // Initialize target dram core to the first dram.
        tt_xy_pair target_dram_bank = dram_cores.at(0).at(0);
        for (const auto& dram_cores : dram_cores_per_channel) {
            for (tt_xy_pair dram : dram_cores) {
                int diff_x = worker.x - dram.x;
                int diff_y = worker.y - dram.y;
                // Represents a dram core that comes "before" this worker.
                if (diff_x >= 0 && diff_y >= 0) {
                    int diff_dram_x = worker.x - target_dram_bank.x;
                    int diff_dram_y = worker.y - target_dram_bank.y;
                    // If initial dram core comes after the worker, swap it with this dram.
                    if (diff_dram_x < 0 || diff_dram_y < 0) {
                        target_dram_bank = dram;
                        // If both target dram core and current dram core come before the worker, choose the one that's
                        // closer.
                    } else if (diff_x + diff_y < diff_dram_x + diff_dram_y) {
                        target_dram_bank = dram;
                    }
                }
            }
        }
        if (perf_dram_bank_to_workers.find(target_dram_bank) == perf_dram_bank_to_workers.end()) {
            perf_dram_bank_to_workers.insert(
                std::pair<tt_xy_pair, std::vector<tt_xy_pair>>(target_dram_bank, {worker}));
        } else {
            perf_dram_bank_to_workers[target_dram_bank].push_back(worker);
        }
    }
}

buda_SocDescriptor::buda_SocDescriptor(const std::string& device_descriptor_path) :
    tt_SocDescriptor(device_descriptor_path) {
    trisc_sizes = {
        l1_mem::address_map::TRISC0_SIZE,
        l1_mem::address_map::TRISC1_SIZE,
        l1_mem::address_map::TRISC2_SIZE};  // TODO: Read trisc size from yaml
    map_workers_to_dram_banks();
}

buda_SocDescriptor::buda_SocDescriptor(const tt_SocDescriptor& other) : tt_SocDescriptor(other) {
    trisc_sizes = {
        l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE};
    map_workers_to_dram_banks();
}

const std::unordered_map<tt_xy_pair, std::vector<tt_xy_pair>>& buda_SocDescriptor::get_perf_dram_bank_to_workers()
    const {
    return perf_dram_bank_to_workers;
}

const std::vector<std::size_t>& buda_SocDescriptor::get_trisc_sizes() const { return trisc_sizes; }

std::unique_ptr<buda_SocDescriptor> load_soc_descriptor_from_yaml(const std::string& device_descriptor_file_path) {
    std::unique_ptr<buda_SocDescriptor> soc_desc = std::make_unique<buda_SocDescriptor>(device_descriptor_file_path);
    return soc_desc;
}
