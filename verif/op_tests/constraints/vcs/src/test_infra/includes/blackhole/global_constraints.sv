`ifndef __GLOBAL_CONSTRAINTS__
`define __GLOBAL_CONSTRAINTS__

`include "includes/common/architecture.sv"

`define DEVICE                   "BLACKHOLE"
`define HARVESTED_ROWS           (0)
// Large grid size can cause what appears to be di/dt issues on BH
`define GRID_SIZE_X              (8)
`define GRID_SIZE_Y              (8-`HARVESTED_ROWS)
`define L1_MEM_SIZE              (1464*1024)
`define FW_L1_MEM_SIZE           (204*1024)
`define PIPEGEN_L1_MEM_SIZE      (312*1024)
`define MAX_L1_MEM_BUFFER_SIZE   (`L1_MEM_SIZE-`FW_L1_MEM_SIZE-`PIPEGEN_L1_MEM_SIZE)
`define NUM_DRAM_CHANNELS        (6)
`define NUM_TARGET_DEVICES       (1)
`define MAX_TILES_IN_DEST        (16)
`define MAX_TILES_IN_DEST_FP32   (8)
`define MAX_TILES_IN_DEST_INT32  (8)

`define MAX_TILES_IN_DEST_REDUCE          (`MAX_TILES_IN_DEST)
`define MAX_TILES_IN_DEST_REDUCE_FP32     (`MAX_TILES_IN_DEST_FP32)

`define FAST_TILIZER_DRAM_BUFFER_START_ADDR (768*1024*1024)
`define FAST_TILIZER_DRAM_BUFFER_END_ADDR   (1024*1024*1024-1)
`define FAST_TILIZER_DRAM_BUFFER_SIZE       (`FAST_TILIZER_DRAM_BUFFER_END_ADDR-`FAST_TILIZER_DRAM_BUFFER_START_ADDR+1)

`define DRAM_BUFFER_START_ADDR (256*1024*1024)
`define DRAM_BUFFER_END_ADDR   (1024*1024*1024-1)
`define DRAM_BUFFER_SIZE       (`DRAM_BUFFER_END_ADDR-`DRAM_BUFFER_START_ADDR+1)
`define DRAM_QUEUE_HEADER_SIZE (64)

`define HOST_BUFFER_START_ADDR (0*1024*1024)
`define HOST_BUFFER_END_ADDR   (256*1024*1024-1)


// On GS, only 32x32 tile dims are supported
`define default_tile_dim_constraints(tile_dim_r, tile_dim_c)   \
   tile_dim_c inside {32, 16};                                 \
   tile_dim_c dist {[32:32]:= 95, [16:16]:= 5};                \
   tile_dim_c == 16 -> tile_dim_r == 32;                       \
   if (tile_dim_c == 32) {                                     \
      tile_dim_r inside {1, 2, 4, 8, 16, 32};                  \
      tile_dim_r dist {[32:32]:= 5, [1:31]:= 95};              \      
   }                                                           \
   tile_dim_r == 16 -> tile_dim_c == 32;                       \




`endif // __GLOBAL_CONSTRAINTS__
