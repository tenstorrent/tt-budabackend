// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <thread>
#include <zmq.hpp>

#include "requests.h"

namespace tt::dbd {

// Communication class that implements 0MQ server and parses messages into tt::dbd::request.
// Needs to have implemented function void process(const request&) for processing requests.
// Current production implementation of it is tt::dbd::server.
class communication {
   public:
    communication();
    virtual ~communication();

    int get_port() const { return port; }

    // Starts zmq server and throws if start fails
    void start(int port);
    void stop();
    bool is_connected() const;

   protected:
    // Override this function to process requests coming from client.
    virtual void process(const request& request) = 0;

    // After processing request, use these functions to send respond to the client.
    void respond(const std::string& message);
    void respond(const void* data, size_t size);

   private:
    int port;
    bool should_stop;
    zmq::context_t zmq_context;
    zmq::socket_t zmq_socket;
    std::unique_ptr<std::thread> background_thread;

    communication(const communication&) = delete;

    friend int communication_loop(tt::dbd::communication* communication);
    friend class yaml_not_implemented_server;
    void request_loop();
};

}  // namespace tt::dbd
