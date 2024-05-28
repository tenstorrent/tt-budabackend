// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tt::dbd {

// Interface that should be implemented for debuda server to process requests.
class debuda_implementation {
   public:
    virtual ~debuda_implementation() = default;

    virtual std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) {
        return {};
    }
    virtual std::optional<uint32_t> pci_write4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                               uint32_t data) {
        return {};
    }
    virtual std::optional<std::vector<uint8_t>> pci_read(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y,
                                                         uint64_t address, uint32_t size) {
        return {};
    }
    virtual std::optional<uint32_t> pci_write(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                              const uint8_t *data, uint32_t size) {
        return {};
    }
    virtual std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) { return {}; }
    virtual std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) { return {}; }
    virtual std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) { return {}; }

    virtual std::optional<std::string> pci_read_tile(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                                     uint32_t size, uint8_t data_format) {
        return {};
    }
    virtual std::optional<std::string> get_runtime_data() { return {}; }
    virtual std::optional<std::string> get_cluster_description() { return {}; }
    virtual std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) { return {}; }
    virtual std::optional<std::vector<uint8_t>> get_device_ids() { return {}; }
    virtual std::optional<std::string> get_device_arch(uint8_t chip_id) { return {}; }
    virtual std::optional<std::string> get_device_soc_description(uint8_t chip_id) { return {}; }
};

}  // namespace tt::dbd
