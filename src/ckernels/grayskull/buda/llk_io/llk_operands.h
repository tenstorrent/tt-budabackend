// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

inline uint32_t get_operand_id(uint32_t operand) 
{
    constexpr uint32_t OPERAND_BASE_ID = 0;
    constexpr uint32_t INTERMEDIATE_BASE_ID = 24;
    return (operand>=INTERMEDIATE_BASE_ID) ? operand - 8 : operand - OPERAND_BASE_ID;
}

inline const uint32_t get_operand_src_format(const std::uint32_t operand_id)
{
   return unpack_src_format[operand_id];
}

inline const uint32_t get_operand_dst_format(const std::uint32_t operand_id)
{
   return unpack_dst_format[operand_id];
}