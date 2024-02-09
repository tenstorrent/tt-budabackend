// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "netlist/tt_backend_api_types.hpp"

namespace tt {
//! Meant to include functions that help calculate size off "base" objects like.
namespace size {
int sizeof_tile_header();
int sizeof_format(DataFormat data_format);
int get_tile_size_in_bytes(DataFormat data_formati, bool include_header_padding, int tile_height = 32, int tile_width = 32);
int get_size_of_n_tiles_in_bytes(DataFormat data_format, int num_tiles, bool include_header_padding);

//! The fields are flattened to prevent too much include overhead. users can call this or call the higher level function
//! from netlist_info types which provides a queue_info
int get_mblock_size_in_tiles(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n);
int get_ublock_size_in_tiles(int ublock_ct, int ublock_rt);
int get_entry_size_in_tiles(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n, int t);
int get_mblock_size_in_bytes(
    DataFormat data_formati, bool include_header_padding, int ublock_ct, int ublock_rt, int mblock_m, int mblock_n);
int get_entry_size_in_bytes(
    DataFormat data_formati,
    bool include_header_padding,
    int ublock_ct,
    int ublock_rt,
    int mblock_m,
    int mblock_n,
    int t,
    int tile_height,
    int tile_width);
int get_mblock_size_in_elements(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n);
int get_entry_size_in_elements(int ublock_ct, int ublock_rt, int mblock_m, int mblock_n, int t, int tile_height = 32, int tile_width = 32);
}  // namespace size
}  // namespace tt