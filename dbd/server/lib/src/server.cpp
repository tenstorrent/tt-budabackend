// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "dbdserver/server.h"

#include "dbdserver/communication.h"

void tt::dbd::server::process(const tt::dbd::request& base_request) {
    switch (base_request.type) {
        case tt::dbd::request_type::invalid:
            respond_not_supported();
            break;
        case tt::dbd::request_type::ping:
            respond("PONG");
            break;

        case tt::dbd::request_type::pci_read4: {
            auto& request = static_cast<const tt::dbd::pci_read4_request&>(base_request);
            respond(implementation->pci_read4(request.chip_id, request.noc_x, request.noc_y, request.address));
            break;
        }
        case tt::dbd::request_type::pci_write4: {
            auto& request = static_cast<const tt::dbd::pci_write4_request&>(base_request);
            respond(implementation->pci_write4(request.chip_id, request.noc_x, request.noc_y, request.address,
                                               request.data));
            break;
        }
        case tt::dbd::request_type::pci_read: {
            auto& request = static_cast<const tt::dbd::pci_read_request&>(base_request);
            respond(
                implementation->pci_read(request.chip_id, request.noc_x, request.noc_y, request.address, request.size));
            break;
        }
        case tt::dbd::request_type::pci_write: {
            auto& request = static_cast<const tt::dbd::pci_write_request&>(base_request);
            respond(implementation->pci_write(request.chip_id, request.noc_x, request.noc_y, request.address,
                                              request.data, request.size));
            break;
        }
        case tt::dbd::request_type::pci_read4_raw: {
            auto& request = static_cast<const tt::dbd::pci_read4_raw_request&>(base_request);
            respond(implementation->pci_read4_raw(request.chip_id, request.address));
            break;
        }
        case tt::dbd::request_type::pci_write4_raw: {
            auto& request = static_cast<const tt::dbd::pci_write4_raw_request&>(base_request);
            respond(implementation->pci_write4_raw(request.chip_id, request.address, request.data));
            break;
        }
        case tt::dbd::request_type::dma_buffer_read4: {
            auto& request = static_cast<const tt::dbd::dma_buffer_read4_request&>(base_request);
            respond(implementation->dma_buffer_read4(request.chip_id, request.address, request.channel));
            break;
        }

        case tt::dbd::request_type::pci_read_tile: {
            auto& request = static_cast<const tt::dbd::pci_read_tile_request&>(base_request);
            respond(implementation->pci_read_tile(request.chip_id, request.noc_x, request.noc_y, request.address,
                                                  request.size, request.data_format));
            break;
        }
        case tt::dbd::request_type::get_runtime_data:
            respond(implementation->get_runtime_data());
            break;
        case tt::dbd::request_type::get_cluster_description:
            respond(implementation->get_cluster_description());
            break;
        case tt::dbd::request_type::get_harvester_coordinate_translation: {
            auto& request = static_cast<const tt::dbd::get_harvester_coordinate_translation_request&>(base_request);
            respond(implementation->get_harvester_coordinate_translation(request.chip_id));
            break;
        }
        case tt::dbd::request_type::get_device_ids:
            respond(implementation->get_device_ids());
            break;
        case tt::dbd::request_type::get_device_arch: {
            auto& request = static_cast<const tt::dbd::get_device_arch_request&>(base_request);
            respond(implementation->get_device_arch(request.chip_id));
            break;
        }
        case tt::dbd::request_type::get_device_soc_description: {
            auto& request = static_cast<const tt::dbd::get_device_soc_description_request&>(base_request);
            respond(implementation->get_device_soc_description(request.chip_id));
            break;
        }
    }
}

void tt::dbd::server::respond_not_supported() {
    static std::string not_supported = "NOT_SUPPORTED";
    communication::respond(not_supported);
}

void tt::dbd::server::respond(std::optional<std::string> response) {
    if (!response) {
        respond_not_supported();
    } else {
        communication::respond(response.value());
    }
}

void tt::dbd::server::respond(std::optional<uint32_t> response) {
    if (!response) {
        respond_not_supported();
    } else {
        communication::respond(&response.value(), sizeof(response.value()));
    }
}

void tt::dbd::server::respond(std::optional<std::vector<uint8_t>> response) {
    if (!response) {
        respond_not_supported();
    } else {
        communication::respond(response.value().data(), response.value().size());
    }
}
