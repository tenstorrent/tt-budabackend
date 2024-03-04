// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "utils/math.h"

using namespace ttl;

uint32_t ttl::convert_float_to_1_8_7(float input) {
    uint32_t* p_uint_input = (uint32_t*)&input;
    uint32_t uint_input = *p_uint_input;
    uint32_t sign = uint_input >> 31;
    uint32_t exp = (uint_input >> 23) & 0xFF;
    uint32_t man = (uint_input >> 16) & 0x7F;

    uint32_t result = (sign << 15) | (exp << 7) | man;
    return result;
}