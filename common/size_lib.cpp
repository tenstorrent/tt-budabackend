// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/size_lib.hpp"

#include "common/model/constants.hpp"
#include "io_lib.hpp"

int tt::size::sizeof_tile_header() {
    return 32;
}

int tt::size::sizeof_format(DataFormat data_format) {
    int size = 0;
    switch (data_format) {
        case DataFormat::Float32: size = 4; break;
        case DataFormat::Tf32: size = 4; break;
        case DataFormat::Float16: size = 2; break;
        case DataFormat::Float16_b: size = 2; break;
        case DataFormat::UInt16: size = 2; break;
        case DataFormat::Int8: size = 1; break;
        case DataFormat::UInt8: size = 1; break;
        case DataFormat::Int32: size = 4; break;
        case DataFormat::RawUInt8: size = 1; break;
        case DataFormat::RawUInt16: size = 2; break;
        case DataFormat::RawUInt32: size = 4; break;
        case DataFormat::Invalid: size = 0xbadface; break;
        default: log_fatal("sizeof_format doesn't support this format.."); break;
    }
    return size;
}

int tt::size::get_tile_size_in_bytes(DataFormat data_formati, bool include_header_padding, int tile_height, int tile_width) {
    int size = 0;
    int num_faces_x = ceil(float(tile_width) / 16);
    int header_size = include_header_padding ? 16 : 0; // Header is always 16 Bytes
    // Exponent section size can change based on the tile dims. When tile_height < 16, pack exponents into a 16 Byte section
    int exponent_section_height = (tile_height < 16) ? 16 : (ceil(float(tile_height) / 16) * 16) * num_faces_x;

    switch (data_formati) {
        case DataFormat::Float32: 
            size = tile_height * tile_width * 4 + header_size;
            break;
        case DataFormat::Tf32: 
            size = tile_height * tile_width * 4 + header_size;
            break;
        case DataFormat::Float16:
            size = tile_height * tile_width * 2 + header_size;
            break;
        case DataFormat::Bfp8: 
            size = tile_height * tile_width + exponent_section_height + header_size;
            break;
        case DataFormat::Bfp4: 
            size = tile_height * tile_width / 2 + exponent_section_height + header_size;
            break;
        case DataFormat::Bfp2: 
            size = tile_height * tile_width / 4 + exponent_section_height + header_size;
            break;
        case DataFormat::Float16_b: 
            size = tile_height * tile_width * 2 + header_size;
            break;
        case DataFormat::Bfp8_b: 
            size = tile_height * tile_width + exponent_section_height + header_size;
            break;
        case DataFormat::Bfp4_b: 
            size = tile_height * tile_width / 2 + exponent_section_height + header_size;
            break;
        case DataFormat::Bfp2_b: 
            size = tile_height * tile_width / 4 + exponent_section_height + header_size;
            break;
        case DataFormat::Lf8: 
            size = tile_height * tile_width + header_size;
            break;
        case DataFormat::Fp8_e4m3: 
            size = tile_height * tile_width + header_size;
            break;
        case DataFormat::UInt16: 
            size = tile_height * tile_width * 2 + header_size;
            break;
        case DataFormat::Int8: 
            size = tile_height * tile_width + header_size;
            break;
        case DataFormat::UInt8: 
            size = tile_height * tile_width + header_size; 
            break;
        case DataFormat::Int32: 
            size = tile_height * tile_width * 4 + header_size;
            break;
        case DataFormat::RawUInt8:
            size = tile_height * tile_width + header_size;
            break;
        case DataFormat::RawUInt16:
            size = tile_height * tile_width * 2 + header_size;
            break;
        case DataFormat::RawUInt32:
            size = tile_height * tile_width * 4 + header_size;
            break;
        case DataFormat::Invalid:
            size = 0; 
            break;
        default: 
            log_fatal("get_tile_size_in_bytes doesn't support this format..");
            break;
    }
    
    if (include_header_padding) {
        // Account for trailer added at the end of the tile to pad up to the required alignment
        size = tt::io::align_up(size, tt::io::tile_alignment_bytes); 
    }
    return size;
}

int tt::size::get_size_of_n_tiles_in_bytes(DataFormat data_format, int num_tiles, bool include_header_padding) {
    return tt::size::get_tile_size_in_bytes(data_format, include_header_padding) * num_tiles;
}

//! The parameters are flattened to prevent too much include overhead.
//! users can call this or call the higher level function implemented in header which has that struct
int tt::size::get_ublock_size_in_tiles(int ublock_ct, int ublock_rt) {
    return ublock_ct * ublock_rt;
}
int tt::size::get_mblock_size_in_tiles(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n) {
    return ublock_ct * ublock_rt * mblock_m * mblock_n;
}
int tt::size::get_entry_size_in_tiles(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n, int t) {
    return get_mblock_size_in_tiles(ublock_ct, ublock_rt, mblock_m, mblock_n) * t;
}
int tt::size::get_mblock_size_in_bytes(
    DataFormat data_formati, bool include_header_padding, int ublock_ct, int ublock_rt, int mblock_m, int mblock_n) {
    return get_mblock_size_in_tiles(ublock_ct, ublock_rt, mblock_m, mblock_n) *
           get_tile_size_in_bytes(data_formati, include_header_padding);
}
int tt::size::get_entry_size_in_bytes(
    DataFormat data_formati,
    bool include_header_padding,
    int ublock_ct,
    int ublock_rt,
    int mblock_m,
    int mblock_n,
    int t,
    int tile_height,
    int tile_width) {
    return get_entry_size_in_tiles(ublock_ct, ublock_rt, mblock_m, mblock_n, t) *
           get_tile_size_in_bytes(data_formati, include_header_padding, tile_height, tile_width);
}
int tt::size::get_mblock_size_in_elements(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n) {
    return get_mblock_size_in_tiles(ublock_ct, ublock_rt, mblock_m, mblock_n) * tt::constants::TILE_HEIGHT *
           tt::constants::TILE_WIDTH;
}
int tt::size::get_entry_size_in_elements(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n, int t, int tile_height, int tile_width) {
    return get_entry_size_in_tiles(ublock_ct, ublock_rt, mblock_m, mblock_n, t) * tile_height * tile_width;
}
