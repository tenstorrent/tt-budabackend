// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

tt_VersimDevice::tt_VersimDevice(std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip_, std::string ndesc_path) : tt_device(soc_descriptor_per_chip_) {
  throw std::runtime_error("tt_VersimDevice() -- VERSIM is not supported in this build\n");
}

tt_VersimDevice::~tt_VersimDevice () {}

int tt_VersimDevice::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_VersimDevice::get_all_chips_in_cluster() { return {}; }
int tt_VersimDevice::detect_number_of_chips() { return 0; }

void tt_VersimDevice::start_device(const tt_device_params &device_params) {}
void tt_VersimDevice::close_device() {}
void tt_VersimDevice::write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd) {}
void tt_VersimDevice::read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {}
void tt_VersimDevice::rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {}
void tt_VersimDevice::start(
    std::vector<std::string> plusargs,
    std::vector<std::string> dump_cores,
    bool no_checkers,
    bool /*init_device*/,
    bool /*skip_driver_allocs*/
) {}

void tt_VersimDevice::deassert_risc_reset(int target_device) {}
void tt_VersimDevice::deassert_risc_reset_at_core(int target_device, tt_xy_pair core) {}
void tt_VersimDevice::assert_risc_reset(int target_device) {}
void tt_VersimDevice::assert_risc_reset_at_core(int target_device, tt_xy_pair core) {}

void tt_VersimDevice::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {};
// void tt_VersimDevice::dump_wall_clock_mailbox(std::string output_path, int device_id) {}

std::set<chip_id_t> tt_VersimDevice::get_target_mmio_device_ids() {return {};}
std::set<chip_id_t> tt_VersimDevice::get_target_remote_device_ids() {return {};}

bool versim_check_dram_core_exists(
    const std::vector<std::vector<tt_xy_pair>> &dram_core_channels, tt_xy_pair target_core) {
  return false;
}

bool tt_VersimDevice::stop() { return true; }

void tt_VersimDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {}

std::map<int,int> tt_VersimDevice::get_clocks() {return std::map<int,int>();}

tt_ClusterDescriptor* tt_VersimDevice::get_cluster_description() {return ndesc.get();}

