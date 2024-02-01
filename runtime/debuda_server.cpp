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
                    start(port);
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

std::optional<uint32_t> tt_debuda_server::pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) {
    tt_cxy_pair target(chip_id, noc_x, noc_y);
    uint32_t size_in_bytes = 4;
    bool small_access = false;
    bool register_txn = true;
    std::vector<std::uint32_t> mem_vector;

    runtime->cluster->read_dram_vec(mem_vector, target, address, size_in_bytes, small_access, register_txn);
    log_trace(
        tt::LogDebuda, "pci_read4 from {}-{} 0x{:x} data: 0x{:x}", noc_x, noc_y, (uint32_t)address, mem_vector[0]);
    return mem_vector[0];
}
std::optional<uint32_t> tt_debuda_server::pci_write4(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) {
    tt_cxy_pair target(chip_id, noc_x, noc_y);
    std::vector<std::uint32_t> mem_vector;

    mem_vector.push_back(data);
    runtime->cluster->write_dram_vec(mem_vector, target, address);
    log_trace(tt::LogDebuda, "pci_write4 to {}-{} 0x{:x} data: 0x{:x}", noc_x, noc_y, (uint32_t)address, mem_vector[0]);
    return 4;
}
std::optional<std::vector<uint8_t>> tt_debuda_server::pci_read(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size) {
    tt_cxy_pair target(chip_id, noc_x, noc_y);
    bool small_access = false;  // TODO: This might be set to true for bigger reads :)
    bool register_txn = true;
    std::vector<std::uint32_t> mem_vector;

    runtime->cluster->read_dram_vec(mem_vector, target, address, size, small_access, register_txn);
    log_trace(tt::LogDebuda, "pci_read from {}-{} 0x{:x} data: 0x{:x}", noc_x, noc_y, (uint32_t)address, mem_vector[0]);

    std::vector<uint8_t> result(mem_vector.size() * sizeof(uint32_t));
    memcpy(&result[0], &mem_vector[0], result.size());
    return result;
}
std::optional<uint32_t> tt_debuda_server::pci_write(
    uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, const uint8_t* data, uint32_t size) {
    tt_cxy_pair target(chip_id, noc_x, noc_y);
    std::vector<std::uint32_t> mem_vector;

    if (size % 4 != 0)
        log_trace(tt::LogDebuda, "pci_write data truncated, data size ({}) needs to be rounded to 4 bytes", size);
    if (size >= 4) {
        mem_vector.resize(size / 4);
        memcpy(&mem_vector[0], data, size / 4 * 4);
        runtime->cluster->write_dram_vec(mem_vector, target, address);
        log_trace(
            tt::LogDebuda, "pci_write to {}-{} 0x{:x} data: 0x{:x}", noc_x, noc_y, (uint32_t)address, mem_vector[0]);
        return size / 4 * 4;
    } else {
        log_trace(tt::LogDebuda, "pci_write nothing to write as data size ({}) is less than 4 bytes", size);
        return 0;
    }
}
std::optional<uint32_t> tt_debuda_server::pci_read4_raw(uint8_t chip_id, uint64_t address) {
    // TODO: finish this
    DebudaIFC difc(runtime->cluster.get());
    if (difc.is_chip_mmio_capable(chip_id)) {
        uint32_t val = difc.bar_read32(chip_id, address);
        // log_debug(tt::LogDebuda, "pci_read4_raw from 0x{:x} data: 0x{:x}", transfer_req.addr, val);
        return val;
    } else {
        return {};
    }
}
std::optional<uint32_t> tt_debuda_server::pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data) {
    // TODO: finish this
    DebudaIFC difc(runtime->cluster.get());
    if (difc.is_chip_mmio_capable(chip_id)) {
        // log_debug(tt::LogDebuda, "pci_write4_raw to 0x{:x} data: 0x{:x}", transfer_req.addr, transfer_req.data);
        difc.bar_write32(chip_id, address, data);
        return 4;
    } else {
        return {};
    }
}
std::optional<uint32_t> tt_debuda_server::dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel) {
    std::uint32_t size_in_bytes = 4;
    std::vector<std::uint32_t> mem_vector;

    runtime->cluster->read_sysmem_vec(mem_vector, address, channel, size_in_bytes, chip_id);
    log_debug(
        tt::LogDebuda,
        "dma_buffer_read4 from 0x{:x} bytes: {} data: 0x{:x} ...",
        (uint32_t)address,
        size_in_bytes,
        mem_vector[0]);
    return mem_vector[0];
}

std::optional<std::string> tt_debuda_server::pci_read_tile(
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
std::optional<std::string> tt_debuda_server::get_runtime_data() { return get_runtime_data_yaml(runtime); }
std::optional<std::string> tt_debuda_server::get_cluster_description() { return get_cluster_desc_path(runtime); }
std::optional<std::string> tt_debuda_server::get_harvester_coordinate_translation(uint8_t chip_id) {
    DebudaIFC difc(runtime->cluster.get());

    return difc.get_harvested_coord_translation(chip_id);
}

void tt_debuda_server::wait_terminate() {
    // iF m_connection_addr is an empty string, we did not start a server, so we do not need to wait
    if (connection_address.empty()) {
        return;
    }
    log_info(tt::LogDebuda, "The debug server is running. Press ENTER to resume execution...");
    std::string user_input;
    std::cin >> user_input;
}
