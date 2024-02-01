`include "global_constraints.sv"
`include "test_config.sv"


class matmul_op_constraints extends global_constraints;

  rand e_data_format in_data_format[3];
  rand e_data_format out_data_format;
  rand e_data_format intermed_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit [15:0] in_tile_size[3];
  rand bit [15:0] out_tile_size;
  rand bit [15:0] intermed_tile_size;
  rand bit [7:0] mblock_k;
  rand bit [7:0] in1_mblock_k;
  rand bit [7:0] ublock_kt;
  rand bit transpose_in1;
  rand bit bias;
  rand bit l1_acc;
  rand bit sfpu_op_en;
  rand bit sfpu_pack_thread_en;
  rand bit relu_en;
  rand integer relu_threshold;
  rand e_relu_mode relu_mode;
  rand bit accumulate_z;
  rand integer z;
  rand integer num_loops;
  rand integer input_q_t;
  rand integer output_q_t;
  rand bit min_buffer_input;
  rand e_stoch_rnd_mode stoch_rnd_mode;

  constraint inner_dims {
    mblock_k inside {[1:16]};
    in1_mblock_k inside {[1:16]};
    ublock_kt inside {[1:16]};
  }

  constraint input_size {
     (grid_size_x*mblock_k) == (grid_size_y*in1_mblock_k);
  }

  constraint untilize {
    (untilize_output == 0);
  }

  constraint rand_transpose_in1 {
      transpose_in1 dist {0:=80, 1:=20};
      (grid_size_x >= `GRID_SIZE_Y) || (grid_size_y >= `GRID_SIZE_X) -> (transpose_in1 == 0);
  }

  constraint formats {
    foreach (in_data_format[n]) {
       in_data_format[n] dist {[bfp8:fp16_b]:=80, fp32:=10, [bfp4:bfp2_b]:=10};
    }
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=15, [bfp4:bfp2_b]:=5};
    intermed_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    (out_data_format inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> ((in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) &&  (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) && (in_data_format[2] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}));
    (out_data_format inside {bfp8, bfp4, bfp2,fp16}) -> ((in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) &&  (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16}) && (in_data_format[2] inside {bfp8, bfp4, bfp2, fp16})); 
    (in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> ((in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) && 
                                                                          (in_data_format[2] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}));
    (in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) -> (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16}) && (in_data_format[2] inside {bfp8, bfp4, bfp2, fp16}) ;
    (out_data_format inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> (intermed_data_format inside {bfp8_b, fp32, fp16_b});
    (out_data_format inside {bfp8, bfp4, bfp2, fp16}) -> (intermed_data_format inside {bfp8, fp16});
    
    //accumulates use mova2d, which needs intermediates to be same data format as dest format, for whb0 its elwadd
    //l1 acc needs intermed fp32 with dest fp32
    ((dest_fp32_acc_enable == 1) && ((`DEVICE != "WORMHOLE_B0") || l1_acc == 1)) -> (intermed_data_format == fp32);  
    
    //Intermediates cant be bfp4/2 due to saturation issue: tenstorrent/budabackend#975
    (dest_fp32_acc_enable == 0) -> (intermed_data_format inside {bfp8, bfp8_b, fp16, fp16_b});  
   }

   constraint format_tile_sizes {
    foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
    intermed_tile_size == `get_tile_size(intermed_data_format);
  }

  constraint fast_tilize {
    fast_tilize_input == 0;  // disable fast tilizer for binary op test
  }

  constraint rand_l1_acc {
    l1_acc_enable == 0 || (intermed_data_format inside {fp16, bfp8, bfp2, bfp4, bfp8_b, bfp2_b, bfp4_b}) -> l1_acc == 0;
  }
  
  constraint rand_stoch_rnd_mode {
    (srnd_enable == 0) -> stoch_rnd_mode == None;
    (srnd_enable == 1) -> {
      z == 0 -> {
         out_data_format == fp16_b && mblock_k* grid_size_x* ublock_kt > 128 -> stoch_rnd_mode == All;
         out_data_format == fp16 && mblock_k* grid_size_x* ublock_kt > 512 -> stoch_rnd_mode == All;
      }
      z > 0 -> {
         out_data_format == fp16_b && mblock_k* grid_size_x* ublock_kt * z > 128 -> stoch_rnd_mode == All;
         out_data_format == fp16 && mblock_k* grid_size_x* ublock_kt * z > 512 -> stoch_rnd_mode == All;
      }
    }
  }

  constraint rand_relu_threshold {
    relu_threshold dist{[0:100]}; 
    relu_en dist {0:=80, 1:=20};
    (relu_en == 0) -> (relu_threshold == 0);
   }

   constraint rand_z_accumulate {
      (accumulate_z == 1) -> (t > 1) && (z inside {[1:32]});
      (accumulate_z == 0) -> (z == 0);
   }

   constraint rand_t {
      input_q_t == t;
      output_q_t <= t; //Constraint for issue #2111
      (accumulate_z == 0) -> (input_q_t == output_q_t);
      (accumulate_z == 1) -> (input_q_t == (output_q_t * z));
   }

   constraint rand_min_buffer_input {
      (l1_acc == 1) -> (min_buffer_input == 0); //TODO: Add fix for l1 acc with min_buffer_input = 1
      ((dest_fp32_acc_enable == 1) && (intermed_data_format != fp32)) -> (l1_acc == 0); //l1 acc intermediate df is set with dest df
   }

   constraint rand_sfpu_op {
      sfpu_op_en dist {0:=80, 1:=20};
      (`DEVICE == "GRAYSKULL") -> (sfpu_pack_thread_en == 0); //grayskull does not support sfpu on pack thread
   }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     if (untilize_output == 0) {
	if (out_data_format != intermed_data_format) {
		((2*ublock_rt*ublock_kt*mblock_m*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size + mblock_n*mblock_m*ublock_rt*ublock_ct*intermed_tile_size) + ((bias == 1) ? 2*ublock_ct*ublock_rt*in_tile_size[2] : 0)  <= `MAX_L1_MEM_BUFFER_SIZE);
	} else { 
		((2*ublock_rt*ublock_kt*mblock_m*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size + ((bias == 1) ? 2*ublock_ct*ublock_rt*in_tile_size[2] : 0)) <= `MAX_L1_MEM_BUFFER_SIZE);
	}
     }  
  }

  rand bit[39:0] in_q_dram_addr[3];
  rand bit[39:0] out_q_dram_addr;

  rand bit[39:0] in_q_size[3];
  rand bit[39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size[0] == (input_q_t*mblock_m*ublock_rt*mblock_k*ublock_kt*num_entries*num_loops*in_tile_size[0] + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[1] == (input_q_t*in1_mblock_k*ublock_kt*mblock_n*ublock_ct*num_entries*num_loops*in_tile_size[1] + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[2] == (input_q_t*mblock_m*mblock_n*ublock_rt*ublock_ct*num_entries*num_loops*in_tile_size[2] + `DRAM_QUEUE_HEADER_SIZE);
    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       if (n < 2) {
          in_q_dram_addr[n] + in_q_size[n]*grid_size_x*grid_size_y < (in_q_dram_addr[n+1]);
       } else {
          in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (out_q_dram_addr);
       }
    }

    out_q_size == (output_q_t*mblock_m*ublock_rt*mblock_n*ublock_ct*num_entries*num_loops*out_tile_size + `DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }

 
  constraint reduce_num_inputs {
     num_inputs inside {[1:4]};
  }
  
  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (output_q_t*mblock_m*mblock_k*ublock_rt*ublock_kt <= 2048);
     (input_q_t*in1_mblock_k*mblock_n*ublock_kt*ublock_ct <= 2048);
     // Reduce runtime when host has to convert input to bfp format
     foreach (in_data_format[n]) {
        ((in_data_format[n] inside {[bfp8:bfp8_b],[bfp4:bfp2_b]}) || (out_data_format inside {[bfp8:bfp8_b],[bfp4:bfp2_b]})) -> ((input_q_t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}) && (mblock_k inside {[1:4]}) && (in1_mblock_k inside {[1:4]}) && (ublock_kt inside {[1:2]}));
     }
  }


endclass

class matmul_op_constraints_int8 extends matmul_op_constraints;
   // Base class constraints that are replaced with new constraints
   constraint formats {
      in_data_format[0] == int8;
      in_data_format[1] == int8;
      in_data_format[2] == int32;
      intermed_data_format == int32;
      out_data_format dist {int8:=60, int32:=40};
      relu_en == 1 -> out_data_format == int8;
      dest_fp32_acc_enable == 0;
   }

   constraint rand_num_inputs {
     // generate less number of tensors for int8 to execute tests faster
     // tenstorrent/budabackend#1817
      num_inputs inside {[1:2]};
   }

   constraint max_dest_dim {
      ublock_rt * ublock_ct <= `MAX_TILES_IN_DEST_INT32/2;
   }

   constraint math_fidelity_constraint {
      math_fidelity == HiFi4;
   }

   constraint rand_l1_acc_enable {
     l1_acc_enable == 0;
   }

   constraint rand_sfpu_op {
      sfpu_op_en == 0;
   }

   constraint rand_relu_threshold {
    relu_threshold dist{[0:12700]}; 
    relu_en dist {0:=80, 1:=20};
    (relu_en == 0) -> (relu_threshold == 0);
   }

endclass

integer ntb_random_seed=0;
integer num_rand_loops=1;
string out_filename;
integer out_filehandle;

bit b_arg_bias_enabled=0;

bit [15:0] in1_mblock_n;
bit [15:0] in1_mblock_k;
bit [ 7:0] in1_ublock_ct;
bit [ 7:0] in1_ublock_kt;
bit [ 7:0] in1_grid_size_x;
bit [ 7:0] in1_grid_size_y;

bit [15:0] out_mblock_m;
bit [15:0] out_mblock_n;
bit [ 7:0] out_ublock_rt;
bit [ 7:0] out_ublock_ct;

bit [ 7:0] out_grid_size_x;
bit [ 7:0] out_grid_size_y;

real real_relu_thresh;
bit b_arg_accumulate_z_enabled=0;
bit int8_enabled=0;

program generator();
  initial begin
      $value$plusargs("num_rand_loops=%d", num_rand_loops);
      $value$plusargs("ntb_random_seed=%d", ntb_random_seed);
      $value$plusargs("bias=%d", b_arg_bias_enabled);
      $value$plusargs("int8=%d", int8_enabled);
      $value$plusargs("accumulate_z=%d", b_arg_accumulate_z_enabled);
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end
  end

  initial begin
    matmul_op_constraints constraints = new();
    test_config test_cfg = new();
    if (int8_enabled) begin
      constraints = matmul_op_constraints_int8::new;
    end
    out_filehandle = $fopen(out_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
      constraints.randomize() with {
         bias == b_arg_bias_enabled;
         accumulate_z == b_arg_accumulate_z_enabled;};

       if (constraints.fast_tilize_input) begin
          $fatal("fast_tilize_input==1 is not supported!");
       end
       if (constraints.untilize_output) begin
         $fatal("untilize_output==1 is not supported for matmul!");
       end else begin
         out_mblock_m = constraints.mblock_m;
         out_mblock_n = constraints.mblock_n;
         out_ublock_rt = constraints.ublock_rt;
         out_ublock_ct = constraints.ublock_ct;
         out_grid_size_x = constraints.grid_size_x;
         out_grid_size_y = constraints.grid_size_y;
       end

       if (constraints.transpose_in1) begin
         in1_mblock_n = constraints.in1_mblock_k;
         in1_mblock_k = constraints.mblock_n;
         in1_ublock_ct = constraints.ublock_kt;
         in1_ublock_kt = constraints.ublock_ct;
         in1_grid_size_x = constraints.grid_size_y;
         in1_grid_size_y = constraints.grid_size_x;
       end else begin
         in1_mblock_n = constraints.mblock_n;
         in1_mblock_k = constraints.in1_mblock_k;
         in1_ublock_ct = constraints.ublock_ct;
         in1_ublock_kt = constraints.ublock_kt;
         in1_grid_size_x = constraints.grid_size_x;
         in1_grid_size_y = constraints.grid_size_y;
       end

       // Random test inputs
       $fwrite(out_filehandle, "#Test id=%0d\n", i);
       $fwrite(out_filehandle, "- device: %s\n", `DEVICE);
       $fwrite(out_filehandle, "  entries: %0d\n", constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  grid_size_x: %0d\n", constraints.grid_size_x);
       $fwrite(out_filehandle, "  grid_size_y: %0d\n", constraints.grid_size_y);
       $fwrite(out_filehandle, "  in1_grid_size_x: %0d\n", in1_grid_size_x);
       $fwrite(out_filehandle, "  in1_grid_size_y: %0d\n", in1_grid_size_y);
       $fwrite(out_filehandle, "  input_q_t: %0d\n", constraints.input_q_t);
       $fwrite(out_filehandle, "  output_q_t: %0d\n", constraints.output_q_t);
       $fwrite(out_filehandle, "  mblock_m: %0d\n", constraints.mblock_m);
       $fwrite(out_filehandle, "  mblock_n: %0d\n", constraints.mblock_n);
       $fwrite(out_filehandle, "  mblock_k: %0d\n", constraints.mblock_k);
       $fwrite(out_filehandle, "  ublock_rt: %0d\n", constraints.ublock_rt);
       $fwrite(out_filehandle, "  ublock_ct: %0d\n", constraints.ublock_ct);
       $fwrite(out_filehandle, "  ublock_kt: %0d\n", constraints.ublock_kt); 
       $fwrite(out_filehandle, "  in1_mblock_n: %0d\n", in1_mblock_n);
       $fwrite(out_filehandle, "  in1_mblock_k: %0d\n", in1_mblock_k);
       $fwrite(out_filehandle, "  in1_ublock_ct: %0d\n", in1_ublock_ct);
       $fwrite(out_filehandle, "  in1_ublock_kt: %0d\n", in1_ublock_kt); 
       $fwrite(out_filehandle, "  m_k: %0d\n", constraints.mblock_k*constraints.grid_size_x);
       $fwrite(out_filehandle, "  u_kt: %0d\n", constraints.ublock_kt);
       $fwrite(out_filehandle, "  l1_acc: %s\n", constraints.l1_acc ? "true" : "false");
       $fwrite(out_filehandle, "  stoch_rnd_mode: %s\n", get_stoch_rnd_mode(constraints.stoch_rnd_mode));
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  intermed_data_format: '%s'\n", get_data_format(constraints.intermed_data_format));
       for (int i=0;i<3; i++) begin
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.grid_size_x*(i==2 ? 1 : constraints.grid_size_y); core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.grid_size_x*(i==2 ? 1 : constraints.grid_size_y)-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
       end
       if (constraints.dest_fp32_acc_enable) begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp32));
       end else if (int8_enabled) begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(int32));
       end else begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp16));
       end
       
       $fwrite(out_filehandle, "  sfpu_op: '%0s'\n", constraints.sfpu_op_en ? "gelu" : "invalid");
       $fwrite(out_filehandle, "  sfpu_execution_thread: '%s'\n", constraints.sfpu_pack_thread_en ? "pack" : "math");
       
       real_relu_thresh = real'(constraints.relu_threshold)/100;
       $fwrite(out_filehandle, "  relu_en: '%s'\n", constraints.relu_en ? "true" : "false");
       $fwrite(out_filehandle, "  relu_mode: '%s'\n", get_relu_mode(constraints.relu_mode));
       $fwrite(out_filehandle, "  relu_threshold: '%f'\n", real_relu_thresh);
       
       $fwrite(out_filehandle, "  z: %0d\n", constraints.z);

       $fwrite(out_filehandle, "  min_buffer_input: %0d\n", constraints.min_buffer_input);

       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       $fwrite(out_filehandle, "  transpose_in1: %s\n", constraints.transpose_in1 ? "[transpose]" : "");
       $fwrite(out_filehandle, "  bcast_in2: '[broadcast: {r: %0d}]'\n", b_arg_bias_enabled ? constraints.grid_size_y*constraints.mblock_m*constraints.ublock_rt : 1);
       $fwrite(out_filehandle, "  ublock_order: r\n");
       $fwrite(out_filehandle, "  buf_size_mb: 2\n");
       $fwrite(out_filehandle, "  # Scale output queue grid and mblock size if output is untilized\n");
       $fwrite(out_filehandle, "  # Net2pipe requires grid size 1x1 if untilized output is written to the queue\n");
       if (constraints.untilize_output) begin
          $fwrite(out_filehandle, "  out_dram_buffers: '[[%0d, 0x%0x]]'\n", constraints.dram_channel[0], constraints.out_q_dram_addr);
       end else begin  
          $fwrite(out_filehandle, "  out_dram_buffers: '[");
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.out_q_dram_addr + core*constraints.out_q_size);
             if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
       end   
       $fwrite(out_filehandle, "  out_grid_size_x: %0d\n", out_grid_size_x);
       $fwrite(out_filehandle, "  out_grid_size_y: %0d\n", out_grid_size_y);
       $fwrite(out_filehandle, "  out_mblock_m: %0d\n", out_mblock_m);
       $fwrite(out_filehandle, "  out_mblock_n: %0d\n", out_mblock_n);
       $fwrite(out_filehandle, "  out_ublock_rt: %0d\n", out_ublock_rt);
       $fwrite(out_filehandle, "  out_ublock_ct: %0d\n", out_ublock_ct);
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_matmul_comparison_config(out_filehandle, constraints.out_data_format, constraints.num_inputs, constraints.mblock_k*constraints.grid_size_x*constraints.ublock_kt, 
       constraints.math_fidelity, 0, 0, constraints.intermed_data_format, constraints.z, (constraints.relu_en) ? get_relu_mode(constraints.relu_mode) : "");
       test_cfg.write_matmul_stimulus_config(out_filehandle, constraints.out_data_format);
    end
    $fclose(out_filehandle);
  end

endprogram
