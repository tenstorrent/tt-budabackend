// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

typedef struct {
   uint32_t fifo_rd_ptr;
   uint32_t fifo_limit;
   uint16_t tiles_acked;
   uint16_t accumulation_buffer;
   uint32_t words_acked;
   uint32_t fifo_size;
   uint16_t blocks_per_iter; // total number of ublocks popped from interm buffer per input
   uint16_t curr_block; // current number of ublocks popped per input
   uint16_t num_iter;  // total number of passes through the interm buffer per input
   uint16_t curr_iter;  // current numer of passes through the interm buffer per input
   uint32_t fifo_rd_base_ptr;
} operand_t;

typedef union {
   operand_t f;
   uint32_t val[8];
} operand_u;

extern operand_u operands[24];

typedef struct {
   uint16_t fifo_wr_ptr;
   uint16_t fifo_limit;
   uint16_t fifo_size;
   uint16_t fifo_wr_tile_ptr;
   uint16_t fifo_size_tiles;
   bool     legacy_pack;
   bool     accumulation_buffer;
   uint16_t tiles_received;
   uint16_t fifo_wr_base_ptr; 
   uint32_t dram_output_no_push;
   uint8_t  fork;
   uint8_t  num_fork_streams;
   bool     shared_buffer;  // interm buffer is shared with output
   uint8_t  shared_buffer_operand; //shared buffer output operand 
   uint8_t  fork_stream_ids[16];
   union {
      uint16_t ublock_ct;       //ublock ct dim in tiles
      uint16_t out_tile_dim;   //output block dim in tiles
   };
   union {
      uint16_t ublock_tile_dim; //number of tiles in ublock for untilized output
      uint16_t blocks_per_iter; //total number of ublocks pushed to interm buffer per input
   };   
   union {
      uint16_t row_tile_dim;    //one row of tiles
   };   
   union {
      uint16_t block_tile_dim;  //one row of ublocks for untilized output 
      uint16_t num_iter; //total number of passes through the interm buffer per input
   };                          
   union {
      uint16_t ublock_tile_cnt;
      uint16_t curr_block;  //current number of ublocks pushed to interm buffer per input
   };   
   union {
      uint16_t block_tile_cnt;  //current number of packed tiles for untilized output 
      uint16_t curr_iter;  // current numer of passes through the interm buffer per input
   };   
} output_t;

typedef union {
   output_t f;
   uint32_t val[13];
} output_u;

extern output_u outputs[16];