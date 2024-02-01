// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "communication.h"

namespace tt::dbd {

// Server class implements tt::dbd::communication to process requests and provides
// virtual functions for each request type. If function is not implemented, it should return {},
// which means that command is not supported by the server.
class server : public communication {
   public:
    server() = default;

   protected:
    void process(const request &request) override;

    // List of functions that should be implemented for debuda server interface.
    virtual std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) = 0;
    virtual std::optional<uint32_t> pci_write4(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) = 0;
    virtual std::optional<std::vector<uint8_t>> pci_read(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size) = 0;
    virtual std::optional<uint32_t> pci_write(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, const uint8_t *data, uint32_t size) = 0;
    virtual std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) = 0;
    virtual std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) = 0;
    virtual std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) = 0;

    virtual std::optional<std::string> pci_read_tile(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) = 0;
    virtual std::optional<std::string> get_runtime_data() = 0;
    virtual std::optional<std::string> get_cluster_description() = 0;
    virtual std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) = 0;

   private:
    // Helper functions that wrap optional into tt::dbd::communication::respond function calls.
    void respond(std::optional<std::string> response);
    void respond(std::optional<uint32_t> response);
    void respond(std::optional<std::vector<uint8_t>> response);
    void respond_not_supported();
};

}  // namespace tt::dbd
