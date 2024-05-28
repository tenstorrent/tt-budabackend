// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace tt::dbd {

// When adding new request, please add it into category
// New requests should always be added at the end of request group
enum class request_type : uint8_t {
    // Basic requests
    invalid = 0,
    ping = 1,

    // Device requests
    pci_read4 = 10,
    pci_write4,
    pci_read,
    pci_write,
    pci_read4_raw,
    pci_write4_raw,
    dma_buffer_read4,

    // Runtime requests
    pci_read_tile = 100,
    get_runtime_data,
    get_cluster_description,
    get_harvester_coordinate_translation,
    get_device_ids,
    get_device_arch,
    get_device_soc_description,
};

// Structures for receiving requests
// If request doesn't need data, structure shouldn't be defined
// Structures are named as <name_of_request_type>_request.
// For example: request_type::pci_read4 has structure pci_read4_request.

struct request {
    request_type type;
} __attribute__((packed));

struct pci_read4_request : request {
    uint8_t chip_id;
    uint8_t noc_x;
    uint8_t noc_y;
    uint64_t address;
} __attribute__((packed));

struct pci_write4_request : request {
    uint8_t chip_id;
    uint8_t noc_x;
    uint8_t noc_y;
    uint64_t address;
    uint32_t data;
} __attribute__((packed));

struct pci_read_request : request {
    uint8_t chip_id;
    uint8_t noc_x;
    uint8_t noc_y;
    uint64_t address;
    uint32_t size;
} __attribute__((packed));

struct pci_write_request : request {
    uint8_t chip_id;
    uint8_t noc_x;
    uint8_t noc_y;
    uint64_t address;
    uint32_t size;
    uint8_t data[0];
} __attribute__((packed));

struct pci_read4_raw_request : request {
    uint8_t chip_id;
    uint32_t address;
} __attribute__((packed));

struct pci_write4_raw_request : request {
    uint8_t chip_id;
    uint32_t address;
    uint32_t data;
} __attribute__((packed));

struct dma_buffer_read4_request : request {
    uint8_t chip_id;
    uint64_t address;
    uint16_t channel;
} __attribute__((packed));

struct pci_read_tile_request : request {
    uint8_t chip_id;
    uint8_t noc_x;
    uint8_t noc_y;
    uint64_t address;
    uint32_t size;
    uint8_t data_format;
} __attribute__((packed));

struct get_harvester_coordinate_translation_request : request {
    uint8_t chip_id;
} __attribute__((packed));

struct get_device_arch_request : request {
    uint8_t chip_id;
} __attribute__((packed));

struct get_device_soc_description_request : request {
    uint8_t chip_id;
} __attribute__((packed));

}  // namespace tt::dbd
