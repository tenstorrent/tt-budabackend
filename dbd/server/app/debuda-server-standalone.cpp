// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// The main purpose of this file is to create a Debuda-server (see loader/debuda_server.cpp) so that Debuda can connect to it.
#include <fstream>
#include <chrono>
#include <ctime>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "utils/logger.hpp"

// Make sure that the file exists, and that it is a regular file
void ensure_file (const std::string& filetype, const std::string& filename) {
    if (!fs::exists(filename)) {
        log_error("{} file '{}' does not exist. Exiting.", filetype, filename);
        exit(1);
    }
    if (!fs::is_regular_file(filename)) {
        log_error("{} file '{}' is not a regular file. Exiting.", filetype, filename);
        exit(1);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        log_error("Need arguments: <port> <runtime-data-yaml-path> ");
        return 1;
    }

    std::vector<std::string> input_args(argv, argv + argc);
    log_info(tt::LogDebuda, "Starting debuda-server: {} {} {}", argv[0], argv[1], argv[2]);
    log_info(tt::LogDebuda, "Use environment variable TT_PCI_LOG_LEVEL to set the logging level (1 or 2)");

    int port = atoi (argv[1]);

    // Set the environament variable for the port, so that tt_runtime can pick it up
    if (getenv("TT_DEBUG_SERVER_PORT") == NULL) {
        setenv("TT_DEBUG_SERVER_PORT", std::to_string(port).c_str(), 1);
    }

    // Check if argv[2] is a valid filename for the runtime_data.yaml
    std::ifstream f(argv[2]);
    if (!f.good()) {
        // f must be a file, not a directory
        if (fs::is_directory(argv[2])) {
            log_error("File {} is a directory. Exiting.", argv[2]);
            return 1;
        }
        log_error("File {} does not exist. Exiting.", argv[2]);
        return 1;
    }

    // Load all the runtime data to pass to tt_runtime
    auto runtime_data = YAML::LoadFile(argv[2]);
    auto arch_name = runtime_data["arch_name"].as<std::string>();
    auto netlist_path = runtime_data["netlist_path"].as<std::string>();
    ARCH arch = get_arch_from_string (arch_name);
    DEVICE backend_type = get_device_from_string(runtime_data["backend_type"].as<std::string>());
    auto cluster_descriptor_path = runtime_data["cluster_descriptor_path"].as<std::string>();

    log_info(tt::LogDebuda, "Port: {}, netlist: {}, arch: {}, cluster-desc: {}", port, netlist_path, arch, cluster_descriptor_path);

    ensure_file ("Runtime data", argv[2]);
    ensure_file ("Netlist", netlist_path);
    printf ("Arch name: %s\n", arch_name.c_str());
    if (arch_name != "GRAYSKULL") {
        ensure_file ("Cluster descriptor", cluster_descriptor_path);
    }

    // Make a temporary directory for the backend to use
    const char* DBD_SERVER_TMP_DIR = "./debuda-server-tmp";

    // Set the minimal configuration to start the runtime
    tt_backend_config target_config = {
        .type = backend_type,
        .arch = arch,
        .output_dir = DBD_SERVER_TMP_DIR, // Hacky: backend will create this directory, which will not be used really.
        .cluster_descriptor_path = cluster_descriptor_path
    };
    log_info(tt::LogDebuda, "Setting temporary directory to: {}", DBD_SERVER_TMP_DIR);

    // Create backend
    std::shared_ptr<tt_backend> target_backend;
    target_backend = tt_backend::create(netlist_path, target_config);
    tt_runtime* silicon_runtime = dynamic_cast<tt_runtime *>(target_backend.get());

    // Wait until termination
    log_info(tt::LogDebuda, "debuda-server-standalone started: press ctrl-C to exit");
    std::this_thread::sleep_until(std::chrono::time_point<std::chrono::system_clock>::max());

    // Cleanup
    try {
        log_assert(target_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target device to close successfully");
    } catch (...) {
        // FIX: perf reporter throws an error; Don't just swallow it.
    }

    // if DBD_SERVER_TMP_DIR directory exists, delete it
    if (fs::exists(DBD_SERVER_TMP_DIR)) {
        log_info(tt::LogDebuda, "Removing temporary directory: {}", DBD_SERVER_TMP_DIR);
        fs::remove_all(DBD_SERVER_TMP_DIR);
    }
    return 0;
}

