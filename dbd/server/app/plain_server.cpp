// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "plain_server.hpp"

#include <unistd.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "dbdserver/umd_with_open_implementation.h"
#include "device/tt_device.h"
#include "utils/logger.hpp"

bool plain_server::start(int port) {
    connection_address = std::string("tcp://*:") + std::to_string(port);
    log_info(tt::LogDebuda, "Debug server starting on {}...", connection_address);

    std::unique_ptr<tt::dbd::umd_with_open_implementation> implementation;

    try {
        implementation = tt::dbd::umd_with_open_implementation::open();
        if (!implementation) {
            return false;
        }
    } catch (std::runtime_error& error) {
        log_custom(tt::Logger::Level::Error, tt::LogDebuda, "Cannot open device: {}.", error.what());
        return false;
    }

    try {
        server = std::make_unique<tt::dbd::server>(std::move(implementation));
        server->start(port);
        log_info(tt::LogDebuda, "Debug server started on {}.", connection_address);
        return true;
    } catch (...) {
        log_custom(tt::Logger::Level::Error, tt::LogDebuda,
                   "Debug server cannot start on {}. An instance of debug server might already be running.",
                   connection_address);
        return false;
    }
}

plain_server::~plain_server() { log_info(tt::LogDebuda, "Debug server ended on {}", connection_address); }
