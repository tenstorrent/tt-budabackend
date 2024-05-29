// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// The implementation of read_tile given here is taken from
// budabackend/model/tile.hpp

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <string>

// TODO: Resolve this. Dependencies:
// (c)xy_pair
// tt_device
#include "device/device_api.h"

namespace tt::dbd::tile {

enum class TileDataFormat : uint8_t {
    Float32 = 0,
    Float16 = 1,
    Bfp8 = 2,
    Bfp4 = 3,
    Bfp2 = 11,
    Float16_b = 5,
    Bfp8_b = 6,
    Bfp4_b = 7,
    Bfp2_b = 15,
    Lf8 = 10,
    Fp8_e4m3 = 0x1A,
    UInt16 = 9,
    Int8 = 14,
    UInt8 = 30,
    Tf32 = 4,
    Int32 = 8,
    RawUInt8 = 0xf0,
    RawUInt16 = 0xf1,
    RawUInt32 = 0xf2,
    Invalid = 0xff
};
TileDataFormat to_data_format(uint8_t i);

std::optional<std::string> read_tile_implementation(
	uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t size, uint8_t data_format, tt_device* device);
std::optional<std::string> dump_tile(const std::vector<uint32_t>& mem_vector, TileDataFormat df);


TileDataFormat to_data_format(uint8_t i);

bool is_shared_exp_format(TileDataFormat format);
bool is_shared_exp_a_format(TileDataFormat format);

static __attribute__((always_inline)) inline uint32_t get_byte(uint32_t word, uint32_t index);
static __attribute__((always_inline)) inline uint32_t get_half_byte(uint32_t word, uint32_t index);
static __attribute__((always_inline)) inline uint32_t get_quarter_byte(uint32_t word, uint32_t index);
static __attribute__((always_inline)) inline uint32_t get_half(uint32_t word, uint32_t index);

uint32_t get_indexed_num(uint32_t data_index, const std::vector<uint32_t>& packed_data, TileDataFormat data_format);
float dword_to_float(uint32_t dword);

} // namespace dbd::tile