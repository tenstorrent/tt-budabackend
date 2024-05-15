// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

struct CompressedIndexControlEntry {
    unsigned int unused_lower : 26;  // bits 0:25
    unsigned int push_tiles : 1;     // bits 26
    unsigned int continue_loop : 1;  // bits 27
    unsigned int loop : 1;           // bits 28
    unsigned int control : 1;        // bits 29
    unsigned int unused_upper : 2;   // bits 30:31
};
struct CompressedIndexDataEntry {
    unsigned int data_lower : 16;     // bits 0:15
    unsigned int data_upper : 13;     // bits 16:28
    unsigned int control : 1;         // bits 29
    unsigned int last_out_block : 1;  // bits 30
    unsigned int last_tile : 1;       // bits 31
};

union CompressedIndex {
    std::uint32_t val;
    CompressedIndexControlEntry ctrl_fields;
    CompressedIndexDataEntry data_fields;
    CompressedIndex(uint32_t init_val) : val(init_val){};
};

// Redefinition of what is in matmul_iden_ljb.cpp for use in golden.
struct strip_info_struct {
    uint16_t enc0;
    uint16_t enc1;
    uint16_t nz_ublocks;
    uint16_t index_array[];
};
