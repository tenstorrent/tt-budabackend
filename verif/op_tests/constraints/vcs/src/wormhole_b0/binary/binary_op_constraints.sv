`include "global_constraints.sv"
`include "test_config.sv"

class binary_op_constraints extends global_constraints;

  rand e_binary_type binary_type;
  rand e_data_format in_data_format[2];
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit gradient_acc;
  rand integer in_tile_size[2];
  rand integer out_tile_size;
  rand e_buffer_loc out_buffer_loc;
  rand integer num_loops;

  rand bit transpose;
  rand bit[5:0] in_0_tile_dim_r;
  rand bit[5:0] in_0_tile_dim_c;
  rand bit[5:0] in_1_tile_dim_r;
  rand bit[5:0] in_1_tile_dim_c;

  constraint binary_tile_dims {
   if (enable_tiny_tile != 0) {
      `default_tile_dim_constraints(in_0_tile_dim_r, in_0_tile_dim_c)
      `default_tile_dim_constraints(in_1_tile_dim_r, in_1_tile_dim_c)

      // constraint in_0_tile_dim == in_1_tile_dim
      transpose == 0 -> in_0_tile_dim_c == in_1_tile_dim_c && in_0_tile_dim_r == in_1_tile_dim_r;
      transpose == 1 -> in_0_tile_dim_c == in_1_tile_dim_r && in_0_tile_dim_r == in_1_tile_dim_c && in_1_tile_dim_r inside {16, 32} && in_1_tile_dim_r dist {[32:32]:=90, [1:31]:=10};

      // out == 32x16 => in1 must be 32x16 as well
      out_tile_dim_c == 16 -> in_1_tile_dim_r == 32 && in_1_tile_dim_c == 16;
      out_tile_dim_c == 32 -> in_1_tile_dim_c == 32;
   } else {
      in_0_tile_dim_r == 32;
      in_0_tile_dim_c == 32;
      in_1_tile_dim_r == 32;
      in_1_tile_dim_c == 32;
      out_tile_dim_c == 32;
      out_tile_dim_r == 32;
   }   
  }

  constraint rand_binary_type {
     ((gradient_acc == 1) && (dest_add_sub_acc_enable == 0)) -> binary_type == multiply;
     binary_type == maximum -> gradient_acc == 0;
  }

  constraint untilize {
      (out_data_format inside {fp16,fp16_b,fp32}) && (gradient_acc == 0) -> (untilize_output == 1); 
      (out_data_format inside {bfp8,bfp8_b,bfp4,bfp4_b,bfp2,bfp2_b}) || (gradient_acc == 1) -> (untilize_output == 0);
  }

  constraint force_transpose {
    transpose dist {0:=80, 1:=20};
    (`DEVICE == "GRAYSKULL") -> (transpose == 0); // inline transpose on binary input 0 is not supported on GS
    (grid_size_x >= `GRID_SIZE_Y) || (grid_size_y >= `GRID_SIZE_X) -> (transpose == 0);
    (gradient_acc == 1) -> (transpose == 0); // in binary_op with gradient_acc template, we have no transpose. Set it to 0 here so it doesn't interfere with tile dims.
  }

  constraint formats {
    foreach (in_data_format[n]) {
       in_data_format[n] dist {[bfp8:fp16_b]:=80, fp32:=10, [bfp4:bfp2_b]:=10};
    }
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    (out_data_format inside {bfp8_b, fp32, fp16_b}) -> ((in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) &&  (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}));
    (out_data_format inside {bfp8, fp16}) -> ((in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) &&  (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16})); 
    (in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b});
    (in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) -> (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16});

    (in_1_tile_dim_r == 1) -> (`constraint_data_format_tile_dim_1x32(in_data_format[0]) && `constraint_data_format_tile_dim_1x32(in_data_format[1]));
    (in_1_tile_dim_r == 2) -> (`constraint_data_format_tile_dim_2x32(in_data_format[0]) && `constraint_data_format_tile_dim_2x32(in_data_format[1]));
    (out_tile_dim_r == 1) -> (`constraint_data_format_tile_dim_1x32(out_data_format));
    (out_tile_dim_r == 2) -> (`constraint_data_format_tile_dim_2x32(out_data_format));
    
    ((`DEVICE != "WORMHOLE_B0") && (gradient_acc == 1)) -> out_data_format == in_data_format[0]; // for gradient acc input[0] and output formats have to match, for whb0 reconfig is used
    ((dest_fp32_acc_enable == 1) && (`DEVICE != "WORMHOLE_B0")) -> (in_data_format[0] == fp32 && in_data_format[1] == fp32);  //For whb0: reconfig unpack is used
  }

  constraint format_tile_sizes {
   foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
  }

  constraint fast_tilize {
    fast_tilize_input == 0;  // disable fast tilizer for binary op test
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((untilize_output == 1)&&(gradient_acc == 0)) -> (2*ublock_rt*ublock_ct*in_tile_size[0] + 2*ublock_rt*ublock_ct*in_tile_size[1] + 2*mblock_n*ublock_rt*ublock_ct*out_tile_size <= `MAX_L1_MEM_BUFFER_SIZE); // if output is untilized we need to double buffer only row of ublocks
     ((untilize_output == 0)&&(gradient_acc == 0)) -> (2*ublock_rt*ublock_ct*in_tile_size[0] + 2*ublock_rt*ublock_ct*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size <= `MAX_L1_MEM_BUFFER_SIZE); // if output is not untilized we need to buffer entire block 
     ((untilize_output == 0)&&(gradient_acc == 1)) -> (2*ublock_rt*ublock_ct*in_tile_size[0] + 2*ublock_rt*ublock_ct*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size*t <= `MAX_L1_MEM_BUFFER_SIZE); // buffer entire t for grad acc
  }

  rand bit[39:0] in_q_dram_addr[2];
  rand bit[39:0] out_q_dram_addr;
  rand bit[39:0] in_q_host_addr[2];

  rand bit [39:0] in_q_size[2];
  rand bit[39:0] out_q_size;

  constraint rand_buffer_loc {
    out_buffer_loc dist {dram:=50, host:=50};
    (untilize_output == 0)->(out_buffer_loc==dram); //we can't untilize from host buffer 
  }

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    foreach (in_tile_size[n]) {
       in_q_size[n] == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*in_tile_size[n]*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    }

    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       if (n < 1) {
          in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (in_q_dram_addr[n+1]);
       } else {
          in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (out_q_dram_addr);
       }
    }

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*num_entries*num_loops*out_tile_size + `DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + out_q_size*grid_size_x*grid_size_y <= (`DRAM_BUFFER_END_ADDR);
  }
  rand bit[39:0] out_q_host_addr;

  constraint rand_host_addr {
    out_q_host_addr[4:0] == 5'b00000; // align to 32B
    out_q_host_addr inside {[`HOST_BUFFER_START_ADDR:`HOST_BUFFER_END_ADDR]};
    out_q_host_addr + out_q_size*grid_size_x*grid_size_y <= (`HOST_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }

  constraint rand_num_inputs {
     num_inputs inside {[1:16]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_n*ublock_ct*ublock_rt <= 2048);
     // Reduce runtime when host has to convert input to bfp format
     foreach (in_data_format[n]) {
        ((in_data_format[n] inside {[bfp8:bfp8_b],[bfp4:bfp2_b]}) || (out_data_format inside {[bfp8:bfp8_b],[bfp4:bfp2_b]})) -> ((num_inputs inside {[1:4]}) && (t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}));
     }
  }

endclass

class binary_op_constraints_int8 extends binary_op_constraints;
   // Base class constraints that are replaced with new constraints
   constraint formats {
      out_data_format inside {int8, int32};
      if (binary_type == add) {
         in_data_format[0] dist {int8:=70, int32:=30};
         in_data_format[1] dist {int8:=70, int32:=30};
         in_data_format[0] == int32 || in_data_format[1] == int32 -> out_data_format == int32;
         in_data_format[0] == int32 || in_data_format[1] == int32 -> transpose == 0;
      } else {
         in_data_format[0] == int8;
         in_data_format[1] == int8;
      }

      dest_fp32_acc_enable == 0;
   }

   constraint in_tile_dims {
      in_0_tile_dim_r == 32;
      in_0_tile_dim_c == 32;
      in_1_tile_dim_r == 32;
      in_1_tile_dim_c == 32;
   }

   constraint tile_dims {
      out_tile_dim_c == 32;
      out_tile_dim_r == 32;
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

endclass

integer ntb_random_seed=0;
integer num_rand_loops=1;
integer gradient_acc=0;
integer int8_enabled=0;
string out_filename;
integer out_filehandle;

bit [15:0] in_mblock_m;
bit [15:0] in_mblock_n;
bit [ 7:0] in_ublock_rt;
bit [ 7:0] in_ublock_ct;

bit [ 7:0] in_grid_size_x;
bit [ 7:0] in_grid_size_y;

bit [15:0] out_mblock_m;
bit [15:0] out_mblock_n;
bit [ 7:0] out_ublock_rt;
bit [ 7:0] out_ublock_ct;

bit [ 7:0] out_grid_size_x;
bit [ 7:0] out_grid_size_y;

program generator();
  initial begin
      $value$plusargs("num_rand_loops=%d", num_rand_loops);
      $value$plusargs("ntb_random_seed=%d", ntb_random_seed);
      $value$plusargs("gradient_acc=%d", gradient_acc);
      $value$plusargs("int8=%d", int8_enabled);
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end
  end

  initial begin
    test_config test_cfg = new();
    binary_op_constraints constraints = new;
    if (int8_enabled) begin
      constraints = binary_op_constraints_int8::new;
      if (gradient_acc) begin
            $fatal("gradient_acc for int8 is not supported!");
      end
    end

    out_filehandle = $fopen(out_filename, "w");

    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       if (gradient_acc) begin
          constraints.randomize() with {
              gradient_acc == 1;
          };
       end else begin
          constraints.randomize() with {
              gradient_acc == 0;
          };
       end

       if (constraints.fast_tilize_input) begin
          $fatal("fast_tilize_input==1 is not supported!");
       end

       if (constraints.transpose) begin
          in_mblock_m  = constraints.mblock_n;
          in_mblock_n  = constraints.mblock_m;
          in_ublock_rt = constraints.ublock_ct;
          in_ublock_ct = constraints.ublock_rt;
          
          in_grid_size_x = constraints.grid_size_y;
          in_grid_size_y = constraints.grid_size_x;
       end else begin
          in_mblock_m  = constraints.mblock_m;
          in_mblock_n  = constraints.mblock_n;
          in_ublock_rt = constraints.ublock_rt;
          in_ublock_ct = constraints.ublock_ct;
          
          in_grid_size_x = constraints.grid_size_x;
          in_grid_size_y = constraints.grid_size_y;
       end

       if (constraints.untilize_output) begin
         if (constraints.gradient_acc) begin
            $fatal("untilize_output==1 is not supported whe gradient accumulation is set to true!");
         end
         out_mblock_m = constraints.mblock_m*constraints.grid_size_y*constraints.ublock_rt;
         out_mblock_n = constraints.mblock_n*constraints.grid_size_x*constraints.ublock_ct;
         out_ublock_rt = 1;
         out_ublock_ct = 1;
         out_grid_size_x = 1;
         out_grid_size_y = 1;
       end else begin
         out_mblock_m = constraints.mblock_m;
         out_mblock_n = constraints.mblock_n;
         out_ublock_rt = constraints.ublock_rt;
         out_ublock_ct = constraints.ublock_ct;
         out_grid_size_x = constraints.grid_size_x;
         out_grid_size_y = constraints.grid_size_y;
       end
       // Random test inputs
       $fwrite(out_filehandle, "#Test id=%0d\n", i);
       $fwrite(out_filehandle, "- device: %s\n", `DEVICE);
       $fwrite(out_filehandle, "  entries: %0d\n", constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  grid_size_x: %0d\n", constraints.grid_size_x);
       $fwrite(out_filehandle, "  grid_size_y: %0d\n", constraints.grid_size_y);
       $fwrite(out_filehandle, "  in_grid_size_x: %0d\n", in_grid_size_x);
       $fwrite(out_filehandle, "  in_grid_size_y: %0d\n", in_grid_size_y);
       $fwrite(out_filehandle, "  t: %0d\n", constraints.t);
       $fwrite(out_filehandle, "  mblock_m: %0d\n", constraints.mblock_m);
       $fwrite(out_filehandle, "  mblock_n: %0d\n", constraints.mblock_n);
       $fwrite(out_filehandle, "  ublock_rt: %0d\n", constraints.ublock_rt);
       $fwrite(out_filehandle, "  ublock_ct: %0d\n", constraints.ublock_ct);
       $fwrite(out_filehandle, "  out_tile_dim_r: %0d\n", constraints.out_tile_dim_r);
       $fwrite(out_filehandle, "  out_tile_dim_c: %0d\n", constraints.out_tile_dim_c);
       $fwrite(out_filehandle, "  in_mblock_m: %0d\n",  in_mblock_m);
       $fwrite(out_filehandle, "  in_mblock_n: %0d\n",  in_mblock_n);
       $fwrite(out_filehandle, "  in_ublock_rt: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  in_ublock_ct: %0d\n", in_ublock_ct);
       $fwrite(out_filehandle, "  in_0_tile_dim_r: %0d\n", constraints.in_0_tile_dim_r);
       $fwrite(out_filehandle, "  in_0_tile_dim_c: %0d\n", constraints.in_0_tile_dim_c);
       $fwrite(out_filehandle, "  in_1_tile_dim_r: %0d\n", constraints.in_1_tile_dim_r);
       $fwrite(out_filehandle, "  in_1_tile_dim_c: %0d\n", constraints.in_1_tile_dim_c);
       for (int i=0;i<2; i++) begin
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
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
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  binary_type: %s\n", get_binary_type(constraints.binary_type));
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       if (constraints.gradient_acc) begin
          $fwrite(out_filehandle, "  gradient_acc: true\n");
       end
       $fwrite(out_filehandle, "  transpose: %s\n", constraints.transpose ? "[transpose]" : "");
       $fwrite(out_filehandle, "  ublock_order: r\n");
       $fwrite(out_filehandle, "  buf_size_mb: 2\n");
       $fwrite(out_filehandle, "  # Scale output queue grid and mblock size if output is untilized\n");
       $fwrite(out_filehandle, "  # Net2pipe requires grid size 1x1 if untilized output is written to the queue\n");
       $fwrite(out_filehandle, "  out_buffer_loc: %s\n", constraints.out_buffer_loc == dram ? "'dram'" : "'host'"); 
       if (constraints.untilize_output) begin
          if (constraints.out_buffer_loc == dram) begin
             $fwrite(out_filehandle, "  out_dram_buffers: '[[%0d, 0x%0x]]'\n", constraints.dram_channel[0], constraints.out_q_dram_addr);
          end else begin
             $fwrite(out_filehandle, "  out_dram_buffers: '[0x%0x]'\n", constraints.out_q_host_addr);
          end
       end else begin  
          $fwrite(out_filehandle, "  out_dram_buffers: '[");
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             if (constraints.out_buffer_loc == dram) begin
                $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.out_q_dram_addr + core*constraints.out_q_size);
             end else begin
                $fwrite(out_filehandle, "0x%0x", constraints.out_q_host_addr + core*constraints.out_q_size);
             end
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
       test_cfg.write_binary_comparison_config(out_filehandle, constraints.out_data_format, constraints.binary_type);
       test_cfg.write_binary_stimulus_config(out_filehandle, constraints.out_data_format, constraints.binary_type);
    end
    $fclose(out_filehandle);
  end

endprogram
