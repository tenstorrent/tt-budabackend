// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>


inline uint32_t get_operand_id(uint32_t operand) 
{
    const int INTERMEDIATE_BASE_ID = 24;
    const int OPERAND_BASE_ID = 0;
    return (operand>=INTERMEDIATE_BASE_ID) ? operand - 8 : operand - OPERAND_BASE_ID;
}

inline uint32_t get_intermediate_base_id()
{
    constexpr int INTERMEDIATE_BASE_ID = 24;
    return  INTERMEDIATE_BASE_ID;
}

inline const uint32_t get_operand_src_format(const std::uint32_t operand_id)
{
   return unpack_src_format[operand_id];
}

inline const uint32_t get_operand_dst_format(const std::uint32_t operand_id)
{
   return unpack_dst_format[operand_id];
}

inline const uint32_t get_operand_num_faces(const std::uint32_t operand_id)
{
   return unpack_tile_num_faces[operand_id];
}

inline const uint32_t get_operand_partial_face(const std::uint32_t operand_id)
{
   return unpack_partial_face[operand_id];
}

inline const uint32_t get_operand_face_r_dim(const std::uint32_t operand_id)
{
   return unpack_tile_face_r_dim[operand_id];
}

inline const uint32_t get_operand_narrow_tile(const std::uint32_t operand_id)
{
   return unpack_narrow_tile[operand_id];
}