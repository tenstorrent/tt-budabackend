// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Main command loop:   command_server_loop
// Commands (requests): REQ_TYPE
// Use LOGGER_LEVEL=Trace environment variable to see the detailed logs
#include "debuda_server.hpp"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

#include <memory>
#include <stdexcept>

#include "loader/tt_cluster.hpp"
#include "model/tile.hpp"
#include "runtime/runtime.hpp"
#include "tt_cluster.hpp"
#include "utils/logger.hpp"

template <typename... Args>
std::string string_format(const std::string& format, Args... args) {
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;  // Extra space for '\0'
    if (size_s <= 0) {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);  // We don't want the '\0' inside
}

constexpr tt::DataFormat to_data_format(uint8_t i) {
    constexpr std::array<tt::DataFormat, 12> supported_formats = {
        tt::DataFormat::Float32,
        tt::DataFormat::Float16,
        tt::DataFormat::Bfp8,
        tt::DataFormat::Bfp4,
        tt::DataFormat::Tf32,
        tt::DataFormat::Float16_b,
        tt::DataFormat::Bfp8_b,
        tt::DataFormat::Bfp4_b,
        tt::DataFormat::Lf8,
        tt::DataFormat::Bfp2,
        tt::DataFormat::UInt16,
        tt::DataFormat::Int8,
    };

    for (tt::DataFormat format : supported_formats)
        if (format == (tt::DataFormat)i)
            return format;
    return tt::DataFormat::Invalid;
}

// Converts tile in packed format into string
std::string dump_tile(const std::vector<uint32_t>& mem_vector, tt::DataFormat df) {
    tt::tt_tile tile;
    tile.packed_data = mem_vector;
    tile.set_data_format(df);
    tile.packed_data_to_tile();
    return tile.get_string();
}

std::string get_runtime_data_yaml(tt_runtime* runtime) {
    tt_cluster* cluster = runtime->cluster.get();
    tt_cluster_description* desc = cluster->get_cluster_desc();

    std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio = desc->get_chips_with_mmio();
    std::unordered_set<chip_id_t> all_chips = desc->get_all_chips();

    std::string s_all;
    for (const auto& elem : all_chips) {
        s_all += string_format("{},", elem);
    }

    std::string s_mmio;
    for (const auto& pair : chips_with_mmio) {
        s_mmio += string_format("{},", pair.first);
    }

    return string_format(
        "all_chips: [%s]\nchips_with_mmio: [%s]\n%s",
        s_all.c_str(),
        s_mmio.c_str(),
        runtime->runtime_data_to_yaml().c_str());
}

std::string get_cluster_desc_path(tt_runtime* runtime) {
    // TODO: Instead of returning cluster description path, we can be returning content of the file
    tt_cluster* cluster = runtime->cluster.get();
    tt_cluster_description* desc = cluster->get_cluster_desc();
    return cluster->get_cluster_desc_path(tt::buda_home());
}

tt_debuda_server::tt_debuda_server(tt_runtime* runtime) : runtime(runtime) {
    auto port_str = std::getenv("TT_DEBUG_SERVER_PORT");
    if (port_str != nullptr) {
        int port = atoi(port_str);
        if (port != 0) {
            if (port > 1024 && port < 65536) {
                connection_address = std::string("tcp://*:") + std::to_string(port);
                log_info(tt::LogDebuda, "Debug server starting on {}...", connection_address);

                try {
                    server =
                        std::make_unique<tt::dbd::server>(std::make_unique<tt_debuda_server_implementation>(runtime));
                    server->start(port);
                    log_info(tt::LogDebuda, "Debug server started on {}.", connection_address);
                } catch (...) {
                    log_info(
                        tt::LogDebuda,
                        "Debug server cannot start on {}. An instance of debug server might already be running.",
                        connection_address);
                }
            } else {
                log_error("TT_DEBUG_SERVER_PORT should be between 1024 and 65535 (inclusive)");
            }
        }
    }
}

tt_debuda_server::~tt_debuda_server() { log_info(tt::LogDebuda, "Debug server ended on {}", connection_address); }

void tt_debuda_server::wait_terminate() {
    // If connection_address is an empty string, we did not start a server, so we do not need to wait
    if (connection_address.empty()) {
        return;
    }
    log_info(tt::LogDebuda, "The debug server is running. Press ENTER to resume execution...");
    std::cin.get();

    server->stop();
}

bool tt_debuda_server::started_server() const {
    // iF m_connection_addr is an empty string, we did not start a server
    return !connection_address.empty();
}

tt_debuda_server_implementation::tt_debuda_server_implementation(tt_runtime* runtime) :
    tt::dbd::umd_implementation(DebudaIFC(runtime->cluster.get()).get_casted_device()), runtime(runtime) {}

std::optional<std::string> tt_debuda_server_implementation::pci_read_tile(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format) {
    tt::DataFormat df = to_data_format(data_format);
    std::vector<std::uint32_t> mem_vector;

    if (df != tt::DataFormat::Invalid) {
        tt_cxy_pair target(chip_id, noc_x, noc_y);
        bool small_access = false;
        bool register_txn = true;  // FIX: This should not be register access, actually

        runtime->cluster->read_dram_vec(mem_vector, target, address, size, small_access, register_txn);
        return dump_tile(mem_vector, df);
    } else {
        return {};
    }
}

std::optional<std::string> tt_debuda_server_implementation::get_runtime_data() {
    return get_runtime_data_yaml(runtime);
}

std::optional<std::string> tt_debuda_server_implementation::get_cluster_description() {
    return get_cluster_desc_path(runtime);
}

std::optional<std::vector<uint8_t>> tt_debuda_server_implementation::get_device_ids() {
    std::vector<uint8_t> device_ids;

    for (auto i : DebudaIFC(runtime->cluster.get()).get_target_device_ids()) {
        device_ids.push_back(i);
    }
    return device_ids;
}

std::optional<std::string> tt_debuda_server_implementation::get_device_soc_description(uint8_t chip_id) { return {}; }
