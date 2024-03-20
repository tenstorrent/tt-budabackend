// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Debuda server is an IP socket server that responds to requests from Debuda. See REQ_TYPE enum
// below for the list of requests. The main connection to the backend is through tt_runtime object.
#pragma once
#include <dbdserver/server.h>

#include <string>

class tt_runtime;

class tt_debuda_server : public tt::dbd::server {
   private:
    // This is what Debuda needs access to
    tt_runtime *runtime;

    // The zmq connection string. Keeping it around for logging purposes
    std::string connection_address;

   protected:
    std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) override;
    std::optional<uint32_t> pci_write4(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) override;
    std::optional<std::vector<uint8_t>> pci_read(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size) override;
    std::optional<uint32_t> pci_write(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, const uint8_t *data, uint32_t size) override;
    std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address) override;
    std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) override;
    std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) override;

    std::optional<std::string> pci_read_tile(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) override;
    std::optional<std::string> get_runtime_data() override;
    std::optional<std::string> get_cluster_description() override;
    std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id) override;

   public:
    tt_debuda_server(tt_runtime *runtime);
    virtual ~tt_debuda_server();

    // Allows user to continue debugging even when the tt_runtime exectution is over
    void wait_terminate();
};