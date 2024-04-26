
`define DEVICE                   "GRAYSKULL"
`define HARVESTED_ROWS           (0)
`define GRID_SIZE_X              (12)
`define GRID_SIZE_Y              (10-`HARVESTED_ROWS)
`define L1_MEM_SIZE              (1024*1024)
`define FW_L1_MEM_SIZE           (204*1024)
`define PIPEGEN_L1_MEM_SIZE      (280*1024)
`define DRAM_BUFF_L1_MEM_SIZE    (8*1024) // Extra buffering space for latency hiding. Used by net2pipe to set min buffer size
`define MAX_L1_MEM_BUFFER_SIZE   (`L1_MEM_SIZE-`FW_L1_MEM_SIZE-`PIPEGEN_L1_MEM_SIZE-`DRAM_BUFF_L1_MEM_SIZE)
`define NUM_DRAM_CHANNELS        (8)
`define NUM_TARGET_DEVICES       (1)
`define MAX_TILES_IN_DEST        (16)
`define MAX_TILES_IN_DEST_FP32   (0)
`define MAX_TILES_IN_DEST_INT32  (0)

`define MAX_TILES_IN_DEST_REDUCE          (`MAX_TILES_IN_DEST)
`define MAX_TILES_IN_DEST_REDUCE_FP32     (`MAX_TILES_IN_DEST_FP32)

// ---- PIPGEN constraints ----
`define MAX_FORKS              (8)


`define FAST_TILIZER_DRAM_BUFFER_START_ADDR (768*1024*1024)
`define FAST_TILIZER_DRAM_BUFFER_END_ADDR   (1024*1024*1024-1)
`define FAST_TILIZER_DRAM_BUFFER_SIZE       (`FAST_TILIZER_DRAM_BUFFER_END_ADDR-`FAST_TILIZER_DRAM_BUFFER_START_ADDR+1)

`define DRAM_BUFFER_START_ADDR (256*1024*1024)
`define DRAM_BUFFER_END_ADDR   (1024*1024*1024-1)
`define DRAM_BUFFER_SIZE       (`DRAM_BUFFER_END_ADDR-`DRAM_BUFFER_START_ADDR+1)
`define DRAM_QUEUE_HEADER_SIZE (32)

`define HOST_BUFFER_START_ADDR (0*1024*1024)
`define HOST_BUFFER_END_ADDR   (256*1024*1024-1)

`define BUFFER_LOC             dram, host
typedef enum {`BUFFER_LOC} e_buffer_loc;

`define DATA_FORMATS             bfp8, bfp8_b, fp16, fp16_b, fp32, bfp4, bfp4_b, bfp2, bfp2_b, int8, int32
`define MATH_FIDELITY            LoFi, HiFi2, HiFi3, HiFi4
`define RELU_MODE                Min, Max
`define STOCH_RND_MODE           None, Fpu, Pack, All

`define VECTOR_MODE              vector_mode_r, vector_mode_c, vector_mode_rc

// On GS, only 32x32 tile dims are supported
`define default_tile_dim_constraints(tile_dim_r, tile_dim_c)   \
   tile_dim_c == 32;                                           \
   tile_dim_c == 32;                                           \

// Issue #2080:
// 1x32 tile_dim -> data format can't be bfp4(a|b) or bfp2(a|b): tile_size ends up as 48 and 40 bytes, which is not 32B aligned.
// 2x32 tile_dim -> data format can't be bfp2(a|b): tile_size ends up as 48 bytes.
// Grayskull currently does not support tiny tiles, still leaving these constraints here
// should it be implemented there, so it doesn't get forgotten.
`define constraint_data_format_tile_dim_1x32(data_format)                                        \
   data_format != bfp2 && data_format != bfp2_b && data_format != bfp4 && data_format != bfp4_b  \

`define constraint_data_format_tile_dim_2x32(data_format)   \
   data_format != bfp2 && data_format != bfp2_b             \

typedef enum {`DATA_FORMATS} e_data_format;
typedef enum {`MATH_FIDELITY}  e_math_fidelity;
typedef enum {`RELU_MODE}  e_relu_mode;
typedef enum {`STOCH_RND_MODE}  e_stoch_rnd_mode;
typedef enum {`VECTOR_MODE} e_vector_mode;

function string get_data_format(input e_data_format data_format);
   case (data_format)
      fp16   : get_data_format = "Float16";
      fp16_b : get_data_format = "Float16_b";
      bfp8   : get_data_format = "Bfp8";
      bfp8_b : get_data_format = "Bfp8_b";
      bfp4   : get_data_format = "Bfp4";
      bfp4_b : get_data_format = "Bfp4_b";
      bfp2   : get_data_format = "Bfp2";
      bfp2_b : get_data_format = "Bfp2_b";
      fp32   : get_data_format = "Float32";
      default: get_data_format = "INVALID";
   endcase 
endfunction

function string get_math_fidelity(input e_math_fidelity math_fidelity);
   case (math_fidelity)
      LoFi : get_math_fidelity = "LoFi";
      HiFi2 : get_math_fidelity = "HiFi2";
      HiFi3 : get_math_fidelity = "HiFi3";
      default: get_math_fidelity = "INVALID";
   endcase 
endfunction

function string get_relu_mode(input e_relu_mode relu_mode);
   case (relu_mode)
      Min : get_relu_mode = "min";
      Max : get_relu_mode = "max";
      default: get_relu_mode = "INVALID";
   endcase 
endfunction

`define TILE_SIZE_BFP2           (352)
`define TILE_SIZE_BFP4           (608)
`define TILE_SIZE_BFP8           (1120)
`define TILE_SIZE_FP16           (2080)
`define TILE_SIZE_FP32           (4128)

`define get_tile_size(data_format) ((data_format==bfp2 | data_format==bfp2_b)?`TILE_SIZE_BFP2:  \
                                    (data_format==bfp4 | data_format==bfp4_b)?`TILE_SIZE_BFP4:  \
                                    (data_format==bfp8 | data_format==bfp8_b)?`TILE_SIZE_BFP8:  \
                                    (data_format==fp16 | data_format==fp16_b)?`TILE_SIZE_FP16:  \
                                                                              `TILE_SIZE_FP32)

`define UNARY_TYPES             datacopy, nop, exp, log, sqrt, gelu, gelu_derivative, reciprocal, sigmoid, tanh  
typedef enum {`UNARY_TYPES}  e_unary_type;

function string get_unary_type(input e_unary_type unary_type);
   case (unary_type)
      datacopy        : get_unary_type = "datacopy";
      nop             : get_unary_type = "nop";
      exp             : get_unary_type = "exp";
      log             : get_unary_type = "log";
      sqrt            : get_unary_type = "sqrt";
      gelu            : get_unary_type = "gelu";
      gelu_derivative : get_unary_type = "gelu_derivative";
      reciprocal      : get_unary_type = "reciprocal";
      sigmoid         : get_unary_type = "sigmoid";
      tanh            : get_unary_type = "tanh";
      default         : get_unary_type = "INVALID";
   endcase 
endfunction

`define BINARY_TYPES             add, multiply, subtract, maximum
typedef enum {`BINARY_TYPES}  e_binary_type;

function string get_binary_type(input e_binary_type binary_type);
   case (binary_type)
      add      : get_binary_type = "add";
      multiply : get_binary_type = "multiply";
      subtract : get_binary_type = "subtract";
      maximum  : get_binary_type = "maximum";
      default  : get_binary_type = "INVALID";
   endcase 
endfunction

`define REDUCE_TYPES            max
typedef enum {`REDUCE_TYPES}  e_reduce_type;

function string get_reduce_type(input e_reduce_type reduce_type);
   case (reduce_type)
      max : get_reduce_type = "max";
      default: get_reduce_type = "INVALID";
   endcase 
endfunction

`define REDUCE_DIMS            r, c, z
typedef enum {`REDUCE_DIMS}  e_reduce_dim;

function string get_reduce_dim(input e_reduce_dim reduce_dim);
   case (reduce_dim)
      r : get_reduce_dim = "r";
      c : get_reduce_dim = "c";
      z : get_reduce_dim = "z";
      default: get_reduce_dim = "INVALID";
   endcase 
endfunction

function string get_stoch_rnd_mode(input e_stoch_rnd_mode stoch_rnd_mode);
   case (stoch_rnd_mode)
      None : get_stoch_rnd_mode = "none";
      Fpu : get_stoch_rnd_mode = "fpu";
      Pack : get_stoch_rnd_mode = "pack";
      All: get_stoch_rnd_mode = "all";
      default: get_stoch_rnd_mode = "invalid";
   endcase 
endfunction

function string get_vector_mode(input e_vector_mode vector_mode);
   case (vector_mode)
      vector_mode_r: get_vector_mode = "r";
      vector_mode_c: get_vector_mode = "c";
      vector_mode_rc: get_vector_mode = "rc";
      default: get_vector_mode = "INVALID";
   endcase
endfunction

class global_constraints;
  rand bit[2:0] target_device;
  rand bit[1:0] dest_fp32_acc_enable;
  rand bit      l1_acc_enable;
  rand bit      dest_add_sub_acc_enable;
  rand bit      srnd_enable;
  rand bit[16:0] num_entries;
  rand bit[15:0] num_inputs;
  rand bit[3:0] grid_size_x;
  rand bit[3:0] grid_size_y;
  rand bit[3:0] grid_loc_x;
  rand bit[3:0] grid_loc_y;
  rand bit[7:0] t;
  rand bit[7:0] mblock_m, mblock_n, ublock_rt, ublock_ct;
  rand bit enable_tiny_tile;
  rand bit[5:0] out_tile_dim_r, out_tile_dim_c;
  rand e_math_fidelity math_fidelity;

  constraint rand_target_device {
     target_device inside {[0:`NUM_TARGET_DEVICES-1]};
  }

  //no fp32 support on GS
  constraint rand_dest_fp32_acc_enable {
     dest_fp32_acc_enable == 0;
  }

  //no l1 acc support on GS
  constraint rand_l1_acc_enable {
     l1_acc_enable == 0;
  }

  constraint rand_dest_add_sub_acc_enable {
     dest_add_sub_acc_enable == 0;
  }

  constraint rand_srnd_enable {
     srnd_enable == 0;
  }

  constraint tile_dims {
   enable_tiny_tile == 0;
  }

  // must be 1 or multiple of 2 due to pipegen limitations
  constraint rand_num_inputs { 
    num_inputs dist {[1:16]:=95, [17:1023]:/4, [1024:1024]:=1};
    (num_inputs != 1) -> (num_inputs%2==0);
  }

  constraint rand_entries {
    num_entries dist  {num_inputs:=20, 2*num_inputs:=80};
  }

  constraint rand_grid_size {
    grid_size_x inside  {[1:`GRID_SIZE_X]};
    grid_size_y inside  {[1:`GRID_SIZE_Y]};
  }

  constraint rand_grid_loc {
    grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    grid_loc_x + grid_size_x <= `GRID_SIZE_X;
    grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
    grid_loc_y + grid_size_y <= `GRID_SIZE_Y;
  }

  constraint max_block_dims {
    t        inside  {[1:64]};
    mblock_m inside  {[1:16]};
    mblock_n inside  {[1:16]};
    ublock_rt inside  {[1:16]};
    ublock_ct inside  {[1:16]};
  }

  rand bit [2:0] dram_channel[0:`GRID_SIZE_X*`GRID_SIZE_Y-1];

   constraint rand_dram_channel {
    foreach (dram_channel[n]) {
      dram_channel[n] inside {[0:`NUM_DRAM_CHANNELS-1]};
    }
   }

  constraint math_fidelity_constraint {
    math_fidelity inside {LoFi, HiFi2, HiFi3};
  }

  // tenstorrent/budabackend#143
  constraint pipegen {
    (t>1) -> ((t%2) == 0);
  }

endclass


