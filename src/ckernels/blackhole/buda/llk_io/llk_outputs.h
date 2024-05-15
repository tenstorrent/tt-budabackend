// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>

inline uint32_t get_output_id(uint32_t output) 
{
   const uint32_t OUTPUT_BASE    = 16;
   return ((output) - OUTPUT_BASE);
}

inline const uint32_t get_output_base_id() 
{
   const uint32_t OUTPUT_BASE_ID = 0;
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

inline const uint32_t get_output_num_faces(const std::uint32_t output_id)
{
   return pack_tile_num_faces[output_id];
}

inline const uint32_t get_output_partial_face(const std::uint32_t output_id)
{
   return pack_partial_face[output_id];
}

inline const uint32_t get_output_face_r_dim(const std::uint32_t output_id)
{
   return pack_tile_face_r_dim[output_id];
}

inline const uint32_t get_output_narrow_tile(const std::uint32_t output_id)
{
   return pack_narrow_tile[output_id];
}

inline const uint32_t get_output_tile_c_dim(const std::uint32_t output_id)
{
   return pack_tile_dims[output_id][TileDim::C_IDX];
}