// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "server.h"

class tt_SiliconDevice;

namespace tt::dbd {

class umd_server : public server {
   public:
    void set_device(tt_SiliconDevice* device);

   protected:
    std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) override;
    std::optional<uint32_t> pci_write4(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) override;
    std::optional<std::vector<uint8_t>> pci_read(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size) override;
    std::optional<uint32_t> pci_write(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, const uint8_t* data, uint32_t size) override;
    std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) override;
    std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) override;
    std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) override;

    std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) override;
    std::optional<std::string> get_device_arch(uint8_t chip_id) override;

   private:
    bool is_chip_mmio_capable(uint8_t chip_id);

    tt_SiliconDevice* device = nullptr;
    std::string cluster_descriptor_path;
};

}  // namespace tt::dbd
