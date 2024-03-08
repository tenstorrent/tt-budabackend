// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Debuda server is an IP socket server that responds to requests from Debuda. See REQ_TYPE enum
// below for the list of requests. The main connection to the backend is through tt_runtime object.
#pragma once

#include <dbdserver/server.h>
#include <dbdserver/umd_implementation.h>

#include <memory>
#include <string>

class tt_runtime;

class tt_debuda_server_implementation : public tt::dbd::umd_implementation {
   private:
    // This is what Debuda needs access to
    tt_runtime *runtime;

   public:
    tt_debuda_server_implementation(tt_runtime *runtime);

    std::optional<std::string> pci_read_tile(
        uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) override;
    std::optional<std::string> get_runtime_data() override;
    std::optional<std::string> get_cluster_description() override;
    std::optional<std::vector<uint8_t>> get_device_ids() override;
    std::optional<std::string> get_device_soc_description(uint8_t chip_id) override;
};

class tt_debuda_server {
   private:
    // This is what Debuda needs access to
    tt_runtime *runtime;

    // The zmq connection string. Keeping it around for logging purposes
    std::string connection_address;

    // The zmq server for debuda.
    std::unique_ptr<tt::dbd::server> server;

   public:
    tt_debuda_server(tt_runtime *runtime);
    virtual ~tt_debuda_server();

    // Allows user to continue debugging even when the tt_runtime exectution is over
    void wait_terminate();
};
