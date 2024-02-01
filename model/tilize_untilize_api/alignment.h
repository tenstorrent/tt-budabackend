// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

constexpr std::uint32_t input_alignment = 32;
constexpr std::uint32_t output_alignment = 16;

// Unaligned memory accesses require data to be aligned to a 32 bit boundary for AVX2 and a 16 bit boundary for AVX
constexpr std::uint32_t input_alignment_avx2 = 32;
constexpr std::uint32_t input_alignment_avx = 16;
