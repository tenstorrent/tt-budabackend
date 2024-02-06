// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _BUF_SIZES_H_
#define _BUF_SIZES_H_


#include <assert.h>
#include "l1_address_map.h"
#include "dram_address_map.h"


const int PIPEGEN_RESERVED_SPACE_SIZE = 256 * 1024; // we can adjust this downward if needed
const int OP_BUF_SPACE_SIZE =
  l1_mem::address_map::MAX_SIZE -
  l1_mem::address_map::DATA_BUFFER_SPACE_BASE -
  PIPEGEN_RESERVED_SPACE_SIZE; // = 1024KB - 160KB - 256KB = 608KB

const int TILE_PAYLOAD_SIZE_NUMS = 32 * 32;
const int TILE_HEADER_SIZE_BYTES = 16;
const int TILE_SIZE_ALIGN_BYTES = 32;

enum OpType
  {
   MatMul,
   EltwiseUnary,
   EltwiseBinary
  };

enum OutputBufLevel 
  {
   // No minimal output buffering requirement.
   // Inputs of consumer ops cannot do TMs, reblocking, or ublock-transpose.
   Streaming,

   // Output buffer large enough to double-buffer a macro-block.
   // Inputs of consumer ops can do reblocking, ublock-transpose,
   // and/or TMs that do not require random access across t dimension. 
   BufferMblock,
   
   // Output buffer large enough to double-buffer the entire output,
   // including the full t-dimension. 
   // Inputs of consumer ops can do reblocking, ublock-transpose,
   // and/or TMs that do not require random access across t-dimension. 
   BufferFull
  };


inline int op_input_buf_size_bytes(OpType op_type,
                                   int input_id,
                                   int mblock_ublocks_m, int mblock_ublocks_n,
                                   int ublock_tiles_r, int ublock_tiles_c,
                                   int num_format_size_bytes) {

  int tile_size_bytes = TILE_PAYLOAD_SIZE_NUMS*num_format_size_bytes + TILE_HEADER_SIZE_BYTES;
  if ((tile_size_bytes % TILE_SIZE_ALIGN_BYTES) != 0) {
    tile_size_bytes += (TILE_SIZE_ALIGN_BYTES - (tile_size_bytes % TILE_SIZE_ALIGN_BYTES));
  }

  if (op_type == OpType::EltwiseUnary) {    
    assert (input_id == 0);
  } else {
    assert (input_id <= 1);
  }

  int input_single_buf_size_bytes;
  if (op_type == OpType::MatMul) {
    // For matmul, we need to provide one row/column of micro-blocks on input,
    // depending on scan order.
    int ublock_size_bytes = ublock_tiles_r * ublock_tiles_c * tile_size_bytes;
    int scan_dim_ublocks = (input_id == 0) ? mblock_ublocks_m : mblock_ublocks_n;
    input_single_buf_size_bytes = scan_dim_ublocks * ublock_size_bytes;
  }
  else {
    // For eltwise ops, front-end can assume that we need to buffer only one tile.
    // Router in net2pipe may decide to increase this buffer for performance
    // (which will likely be needed for data-credit round-trip latency hiding).
    input_single_buf_size_bytes = tile_size_bytes;
  }

  // need to double-buffer input so that new one is received while
  // unpacker is processing the previous one
  int input_double_buf_size_bytes = 2*input_single_buf_size_bytes;
  return input_double_buf_size_bytes;
}


inline int op_output_buf_size_bytes(OpType op_type,
                                    OutputBufLevel output_buf_level,
                                    int t,
                                    int mblock_ublocks_m, int mblock_ublocks_n, 
                                    int ublock_tiles_r, int ublock_tiles_c,
                                    int num_format_size_bytes) {

  int tile_size_bytes = TILE_PAYLOAD_SIZE_NUMS*num_format_size_bytes + TILE_HEADER_SIZE_BYTES;
  if ((tile_size_bytes % TILE_SIZE_ALIGN_BYTES) != 0) {
    tile_size_bytes += (TILE_SIZE_ALIGN_BYTES - (tile_size_bytes % TILE_SIZE_ALIGN_BYTES));
  }

  if (op_type == OpType::MatMul) {
    // minimal output requirement for matmul is one mblock
    assert(output_buf_level != OutputBufLevel::Streaming); 
  }

  int output_single_buf_size_bytes;
  if (output_buf_level == OutputBufLevel::Streaming) {
    output_single_buf_size_bytes = tile_size_bytes;
  }
  else {
    int ublock_size_bytes = ublock_tiles_r * ublock_tiles_c * tile_size_bytes;
    int output_mblock_size_bytes = mblock_ublocks_m * mblock_ublocks_n * ublock_size_bytes;
    if (output_buf_level == OutputBufLevel::BufferMblock) {
      output_single_buf_size_bytes = output_mblock_size_bytes;
    }
    else {
      // OutputBufLevel::BufferFull
      output_single_buf_size_bytes = t * output_mblock_size_bytes;
    }
  }
  
  // need to double-buffer output so that new one is packed while
  // the previous one is being sent off
  int output_double_buf_size_bytes = 2*output_single_buf_size_bytes;
  return output_double_buf_size_bytes;
}


inline int op_total_buf_size_bytes(OpType op_type,
                                   OutputBufLevel output_buf_level,
                                   int t,
                                   int mblock_ublocks_m, int mblock_ublocks_n, 
                                   int ublock_tiles_r, int ublock_tiles_c,
                                   int num_format_size_bytes) {

  int total_buf_size_bytes = 0;
  
  int num_inputs = (op_type == OpType::EltwiseUnary) ? 1 : 2;
  for (int i = 0; i < num_inputs; i++) {
    total_buf_size_bytes += op_input_buf_size_bytes(op_type, i,
                                                    mblock_ublocks_m, mblock_ublocks_n,
                                                    ublock_tiles_r, ublock_tiles_c,
                                                    num_format_size_bytes);
  }

  total_buf_size_bytes += op_output_buf_size_bytes(op_type, output_buf_level,
                                                   t,
                                                   mblock_ublocks_m, mblock_ublocks_n, 
                                                   ublock_tiles_r, ublock_tiles_c,
                                                   num_format_size_bytes);
  
  assert(total_buf_size_bytes <= OP_BUF_SPACE_SIZE);
  return total_buf_size_bytes;
}



#endif
