// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "communication.h"
#include "debuda_implementation.h"

namespace tt::dbd {

// Server class implements tt::dbd::communication to process requests and provides
// virtual functions for each request type. If function is not implemented, it should return {},
// which means that command is not supported by the server.
class server : public communication {
   public:
    server(std::unique_ptr<debuda_implementation> implementation) : implementation(std::move(implementation)) {}

   protected:
    void process(const request &request) override;

   private:
    // Helper functions that wrap optional into tt::dbd::communication::respond function calls.
    void respond(std::optional<std::string> response);
    void respond(std::optional<uint32_t> response);
    void respond(std::optional<std::vector<uint8_t>> response);
    void respond_not_supported();

    std::unique_ptr<debuda_implementation> implementation;
};

}  // namespace tt::dbd
