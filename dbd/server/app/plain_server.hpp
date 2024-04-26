// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <dbdserver/server.h>

class plain_server {
   private:
    std::string connection_address;  // The zmq connection string. Keeping it around for logging purposes
    std::unique_ptr<tt::dbd::server> server;

   public:
    ~plain_server();

    bool start(int port);
};
