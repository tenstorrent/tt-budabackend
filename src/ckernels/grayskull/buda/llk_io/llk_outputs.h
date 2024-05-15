// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

inline uint32_t get_output_id(uint32_t output) 
{
    constexpr uint32_t OUTPUT_BASE    = 16; 
    return ((output) - OUTPUT_BASE);
}

inline constexpr uint32_t get_output_base_id() 
{
    constexpr uint32_t OUTPUT_BASE_ID = 0; 
    return (OUTPUT_BASE_ID);
}

inline const uint32_t get_output_src_format(const std::uint32_t output_id)
{
   return pack_src_format[output_id];
}

inline const uint32_t get_output_dst_format(const std::uint32_t output_id)
{
   return pack_dst_format[output_id];
}