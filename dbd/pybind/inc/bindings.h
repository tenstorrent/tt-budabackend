// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <dbdserver/debuda_implementation.h>
#include <dbdserver/umd_with_open_implementation.h>
#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>

#include "device/tt_device.h"

bool open_device(const std::string &binary_directory, const std::string &runtime_yaml_path);
void set_debuda_implementation(std::unique_ptr<tt::dbd::debuda_implementation> imp);

std::optional<uint32_t> pci_read4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address);
std::optional<uint32_t> pci_write4(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data);

std::optional<pybind11::object> pci_read(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                         uint32_t size);
std::optional<uint32_t> pci_write(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                  pybind11::buffer data, uint32_t size);

std::optional<uint32_t> pci_read4_raw(uint8_t chip_id, uint64_t address);
std::optional<uint32_t> pci_write4_raw(uint8_t chip_id, uint64_t address, uint32_t data);

std::optional<uint32_t> dma_buffer_read4(uint8_t chip_id, uint64_t address, uint32_t channel);
std::optional<std::string> pci_read_tile(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size,
                                         uint8_t data_format);

std::optional<std::string> get_runtime_data();
std::optional<std::string> get_cluster_description();
std::optional<std::string> get_harvester_coordinate_translation(uint8_t chip_id);
std::optional<std::vector<uint8_t>> get_device_ids();
std::optional<std::string> get_device_arch(uint8_t chip_id);
std::optional<std::string> get_device_soc_description(uint8_t chip_id);