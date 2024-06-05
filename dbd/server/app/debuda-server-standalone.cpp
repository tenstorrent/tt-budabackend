// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// The main purpose of this file is to create a Debuda-server (see loader/debuda_server.cpp) so that Debuda can connect
// to it.
#include <ctime>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>

#include "dbdserver/server.h"
#include "dbdserver/umd_with_open_implementation.h"
#include "utils/logger.hpp"

namespace fs = std::experimental::filesystem;

struct server_config {
   public:
    int port;
    std::string runtime_data_yaml_path;
    std::vector<uint8_t> wanted_devices;
};

// Make sure that the file exists, and that it is a regular file
void ensure_file(const std::string& filetype, const std::string& filename) {
    if (!fs::exists(filename)) {
        log_error("{} file '{}' does not exist. Exiting.", filetype, filename);
        exit(1);
    }
    if (!fs::is_regular_file(filename)) {
        log_error("{} file '{}' is not a regular file. Exiting.", filetype, filename);
        exit(1);
    }
}

int run_debuda_server(const server_config& config) {
    if (config.port > 1024 && config.port < 65536) {
        // Open wanted devices
        std::unique_ptr<tt::dbd::umd_with_open_implementation> implementation;
        // Try to open only wanted devices
        try {
            implementation =
                tt::dbd::umd_with_open_implementation::open({}, config.runtime_data_yaml_path, config.wanted_devices);
        } catch (std::runtime_error& error) {
            log_custom(tt::Logger::Level::Error, tt::LogDebuda, "Cannot open device: {}.", error.what());
            return 1;
        }

        auto connection_address = std::string("tcp://*:") + std::to_string(config.port);
        log_info(tt::LogDebuda, "Debug server starting on {}...", connection_address);

        // Spawn server
        std::unique_ptr<tt::dbd::server> server;
        try {
            server = std::make_unique<tt::dbd::server>(std::move(implementation));
            server->start(config.port);
            log_info(tt::LogDebuda, "Debug server started on {}.", connection_address);
        } catch (...) {
            log_custom(tt::Logger::Level::Error, tt::LogDebuda,
                       "Debug server cannot start on {}. An instance of debug server might already be running.",
                       connection_address);
            return 1;
        }

        // Wait terminal input to stop the server
        log_info(tt::LogDebuda, "The debug server is running. Press ENTER to stop execution...");
        std::cin.get();

        // Stop server in destructor
        log_info(tt::LogDebuda, "Debug server ended on {}", connection_address);
        return 0;
    } else {
        log_error("port should be between 1024 and 65535 (inclusive)");
        return 1;
    }
}

// This variable is used in tt_cluster.cpp to cache cluster descriptor path. We set it here to bypass generating it when
// we have it.
extern std::string cluster_desc_path;

server_config parse_args(int argc, char** argv) {
    server_config config = server_config();
    config.port = atoi(argv[1]);

    int i = 2;
    while (i < argc) {
        if (strcmp(argv[i], "-y") == 0) {
            config.runtime_data_yaml_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "-d") == 0) {
            i++;
            if (i >= argc) {
                log_error("Expected space-delimited list of integer ids after -d");
                return {};
            } else {
                while (i < argc) {
                    try {
                        int device_id = std::atoi(argv[i]);
                        if (device_id < 0 || device_id > 255) throw std::invalid_argument("Invalid device id");
                        config.wanted_devices.push_back(device_id);
                    } catch (std::invalid_argument& e) {
                        log_error("Invalid device id: {}", argv[i]);
                        return {};
                    }
                    i++;
                }
            }
        } else {
            log_error("Unknown argument: {}", argv[i]);
            return {};
        }
    }

    return std::move(config);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        log_error("Need arguments: <port> [-y path_to_yaml_file] [-d <device_id1> [<device_id2> ... <device_idN>]]");
        return 1;
    }

    server_config config = parse_args(argc, argv);
    log_info(tt::LogDebuda, "Starting debuda-server: {} {} {}", argv[0], argv[1], argc > 2 ? argv[2] : "");
    log_info(tt::LogDebuda, "Use environment variable TT_PCI_LOG_LEVEL to set the logging level (1 or 2)");

    if (argc == 2) {
        return run_debuda_server(config);
    }

    // Check if argv[2] is a valid filename for the runtime_data.yaml
    std::ifstream f(config.runtime_data_yaml_path);
    if (!f.good() && !config.runtime_data_yaml_path.empty()) {
        // f must be a file, not a directory
        if (fs::is_directory(argv[2])) {
            log_error("File {} is a directory. Exiting.", argv[2]);
            return 1;
        }
        log_error("File {} does not exist. Exiting.", argv[2]);
        return 1;
    }

    return run_debuda_server(config);
}
