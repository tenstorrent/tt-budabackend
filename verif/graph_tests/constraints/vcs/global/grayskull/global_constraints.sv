`define DEVICE                   "GRAYSKULL"
`define TILE_R                   (32)
`define TILE_C                   (32)
`define GRID_SIZE_X              (12)
`define GRID_SIZE_Y              (10-`HARVESTED_ROWS)
`define L1_MEM_SIZE              (1024*1024)
`define FW_L1_MEM_SIZE           (170*1024)
`define PIPEGEN_L1_MEM_SIZE      (256*1024)
`define MAX_L1_MEM_BUFFER_SIZE   (`L1_MEM_SIZE-`FW_L1_MEM_SIZE-`PIPEGEN_L1_MEM_SIZE)
`define NUM_DRAM_CHANNELS        (8)
`define NUM_TARGET_DEVICES       (1)
`define MAX_TILES_IN_DEST        (16)
`define MAX_TILES_IN_DEST_FP32   (0)

`define DRAM_BUFFER_START_ADDR   (256*1024*1024)
`define DRAM_BUFFER_END_ADDR     (1024*1024*1024-1)
`define DRAM_BUFFER_SIZE         (`DRAM_BUFFER_END_ADDR-`DRAM_BUFFER_START_ADDR+1)
`define DRAM_QUEUE_HEADER_SIZE   (32)

`define HOST_BUFFER_START_ADDR   (0*1024*1024)
`define HOST_BUFFER_END_ADDR     (256*1024*1024-1)

//`define FAST_TILIZER_DRAM_BUFFER_START_ADDR (768*1024*1024)
//`define FAST_TILIZER_DRAM_BUFFER_END_ADDR   (1024*1024*1024-1)
//`define FAST_TILIZER_DRAM_BUFFER_SIZE       (`FAST_TILIZER_DRAM_BUFFER_END_ADDR-`FAST_TILIZER_DRAM_BUFFER_START_ADDR+1)

`define BUFFER_LOC               dram, host
typedef enum {`BUFFER_LOC}       e_buffer_loc;

`define DATA_FORMATS             bfp8, bfp8_b, fp16, fp16_b, fp32, bfp4, bfp4_b, bfp2, bfp2_b
`define MATH_FIDELITY            LoFi, HiFi2, HiFi3

typedef enum {`DATA_FORMATS}     e_data_format;
typedef enum {`MATH_FIDELITY}    e_math_fidelity;


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
      LoFi   : get_math_fidelity = "LoFi";
      HiFi2  : get_math_fidelity = "HiFi2";
      HiFi3  : get_math_fidelity = "HiFi3";
      default: get_math_fidelity = "INVALID";
   endcase
endfunction

`define TILE_SIZE_BFP2           (352)
`define TILE_SIZE_BFP4           (608)
`define TILE_SIZE_BFP8           (1120)
`define TILE_SIZE_FP16           (2080)
`define TILE_SIZE_FP32           (4128)

`define get_tile_size(data_format) ((data_format==bfp2 | data_format==bfp2_b)?`TILE_SIZE_BFP2: \
                                    (data_format==bfp4 | data_format==bfp4_b)?`TILE_SIZE_BFP4: \
                                    (data_format==bfp8 | data_format==bfp8_b)?`TILE_SIZE_BFP8: \
                                    (data_format==fp16 | data_format==fp16_b)?`TILE_SIZE_FP16: \
                                                                              `TILE_SIZE_FP32)


class global_constraints;
  rand integer unsigned target_device;
  rand integer unsigned dest_fp32_acc_enable;
  rand integer unsigned input_count; //microbatch_size
  rand integer unsigned microbatch_count;
  rand integer unsigned minibatch_count;
  rand integer unsigned num_entries;
  rand integer unsigned t;
  rand e_data_format    data_format;
  rand e_math_fidelity  math_fidelity;
  rand integer unsigned dram_channel[0:`GRID_SIZE_X*`GRID_SIZE_Y-1];

  constraint rand_target_device {
     target_device inside {[0:`NUM_TARGET_DEVICES-1]};
  }

  // No fp32 support on Grayskull
  constraint rand_dest_fp32_acc_enable {
     dest_fp32_acc_enable == 0;
  }

  // Randomize input_count == microbatch_size
  constraint rand_input_count {
    input_count dist {[1:16]:=95, [17:1023]:=4, [1024:1024]:=1};

    // must be 1 or multiple of 2 due to pipegen limitations
    (input_count != 1) -> (input_count%2==0);
  }

  // Randomize count of microbatches (netlist program loop iterations) per minibatch
  constraint rand_microbatch_count {
    microbatch_count dist {[1:4]:=80, [5:32]:=15, [33:64]:=5};
  }

  // Randomize count of minibatches (host program loop iterations)
  constraint rand_minibatch_count {
    minibatch_count dist {[1:4]:=80, [5:32]:=15, [33:64]:=5};
  }

  // Define input queue entries based on microbatch count and size
  constraint rand_num_entries {
    // input and output queues need to be equal to microbatch_count * microbatch_size
    (num_entries) == (microbatch_count * input_count);
  }

  // Randomize t (Pybuda z) dimension of input tensor
  constraint rand_t {
    t dist {[1:4]:=80, [5:32]:=15, [33:64]:=5};
  }

  // Randomize dram channel used for each input activation queue buffer per grid
  constraint rand_dram_channel {
    foreach (dram_channel[n]) {
      dram_channel[n] inside {[0:`NUM_DRAM_CHANNELS-1]};
    }
  }

  constraint rand_data_format {
    data_format dist {[fp16:fp16_b]:=80, fp32:=10, [bfp8:bfp8_b]:=10};
    (dest_fp32_acc_enable == 1) -> (data_format == fp32);
  }

  //t needs to be either 1 or an even number due to a current limitation in pipegen
  //tenstorrent/budabackend#143
  constraint pipegen_current_limit {
    (t>1) -> ((t%2) == 0);
  }

endclass