// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Debuda server is an IP socket server that responds to requests from Debuda. See REQ_TYPE enum
// below for the list of requests. The main connection to the backend is through tt_runtime object.
#pragma once
#include <string>

class tt_runtime;

class tt_debuda_server {
    // This is what Debuda needs access to
    tt_runtime *m_runtime;

    // The zmq connection string. Keeping it around for logging purposes
    std::string m_connection_addr;

    public:
        tt_debuda_server (tt_runtime *runtime);
        virtual ~tt_debuda_server ();

        // Allows user to continue debugging even when the tt_runtime exectution is over
        void wait_terminate();
};