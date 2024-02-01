// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "netlist/netlist_op_info_types.hpp"

#include <limits>

enum class StimulusType {
    Invalid,
    Uniform,
    Normal,
    Constant,
    DebugTileId,
    Sparse,
    SparseEncoding,
    SparseEncodingNonzeroCounts,
    Step,
    StepRowwise,
};

struct SparseEncodingAttributes {
    // Frequencies of zero strips, zero ublocks and zero tiles.
    double zero_strip_freq = 0.0;
    double zero_ublock_freq = 0.0;
    double zero_tile_freq = 0.0;

    // Counts of non-zero strips, ublocks and tiles.
    int nz_strips = -1;
    int nz_ublocks = -1;
    int nz_tiles = -1;

    int enc_tile_byte_size = 4096;

    // Name of the matmul ident op for which encoding is done.
    string matmul_ident_name = "";

    // Identity matmul details.
    tt_op_info matmul_ident_info;

    // Step details.
    float step_start = 0.0;
    float step_increment = 1.0f;
};

struct VerifStimulusConfig {
    StimulusType type = StimulusType::Invalid;
    double uniform_lower_bound = std::numeric_limits<double>::quiet_NaN();
    double uniform_upper_bound = std::numeric_limits<double>::quiet_NaN();
    double normal_mean = std::numeric_limits<double>::quiet_NaN();
    double normal_stddev = std::numeric_limits<double>::quiet_NaN();
    double constant_value = std::numeric_limits<double>::quiet_NaN();
    double debug_tile_id_base = std::numeric_limits<double>::quiet_NaN();
    double debug_tile_id_step = std::numeric_limits<double>::quiet_NaN();
    pair<int, int> set_tile_rows_range = pair<int, int>(0, tt::constants::TILE_HEIGHT - 1);
    pair<int, int> set_tile_cols_range = pair<int, int>(0, tt::constants::TILE_WIDTH - 1);
    SparseEncodingAttributes sparse_encoding_attrs;
    float step_start = std::numeric_limits<float>::quiet_NaN();
    float step_increment = std::numeric_limits<float>::quiet_NaN();
};
