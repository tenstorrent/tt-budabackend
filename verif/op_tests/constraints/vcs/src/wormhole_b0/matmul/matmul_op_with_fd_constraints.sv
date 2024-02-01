`include "global_constraints.sv"
`include "test_config.sv"


class matmul_op_with_fd_constraints extends global_constraints;

  rand e_binary_type binary_type;
  rand e_data_format in_data_format[2];
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand integer in_tile_size[2];
  rand integer out_tile_size;
  rand bit [7:0] feeder_grid_loc_x[2];
  rand bit [7:0] drainer_grid_loc_x;
  rand bit [7:0] feeder_grid_loc_y[2];
  rand bit [7:0] drainer_grid_loc_y;
  rand bit [7:0] mblock_k;
  rand bit [7:0] ublock_kt;
  rand bit [5:0] in_0_tile_dim_r;
  rand bit [5:0] in_0_tile_dim_c;
  rand bit [5:0] in_1_tile_dim_r;
  rand bit [5:0] in_1_tile_dim_c;
  rand bit transpose_in1;
  rand integer num_loops;
  rand bit l1_acc;
  rand e_stoch_rnd_mode stoch_rnd_mode;

  rand bit [3:0] in_grid_size_x[2];
  rand bit [3:0] in_grid_size_y[2];

  constraint in_grid_size {
    in_grid_size_x[0] == 1;
    in_grid_size_y[0] == grid_size_y;
    in_grid_size_x[1] == grid_size_x;
    in_grid_size_y[1] == 1;
  };

  constraint inner_dims {
    mblock_k inside {[1:16]};
    ublock_kt inside {[1:16]};
  }

  // Override global constraints
  constraint tile_dims {
   enable_tiny_tile dist {[0:0]:=70, [1:1]:=30};
   // in_1_tile_dim_c is always 32, with or without tiny tiles
   in_1_tile_dim_c == 32;
   if (enable_tiny_tile != 0) {
      out_tile_dim_c == 32;
      out_tile_dim_r inside {1, 2, 4, 8, 16, 32};
      out_tile_dim_r dist {[32:32]:= 5, [1:16]:=95};

      in_0_tile_dim_r inside {1, 2, 4, 8, 16, 32};
      in_0_tile_dim_r dist {[32:32]:=5, [1:16]:=95};

      in_0_tile_dim_c inside {16, 32};
      in_0_tile_dim_c dist {[32:32]:=80, [16:16]:=20};

      in_0_tile_dim_c == 16 -> in_0_tile_dim_r == 32;
      in_1_tile_dim_r inside {16, 32};
      in_1_tile_dim_r dist {[16:16]:=50, [32:32]:=50};
   } else {
      in_0_tile_dim_r == 32;
      in_0_tile_dim_c == 32;
      in_1_tile_dim_r == 32;
      out_tile_dim_r == 32;
      out_tile_dim_c == 32;
   }
  }

  constraint rand_transpose_in1 {
    transpose_in1 == 0; //covered in matmul_op_constraints
  }

  constraint rand_fd_grid_loc { // layout ops assuming horizontal split
    foreach (feeder_grid_loc_x[n]) {
       feeder_grid_loc_x[n] inside  {[0:`GRID_SIZE_X-1]};
    }
    foreach (feeder_grid_loc_y[n]) {
       feeder_grid_loc_y[n] inside  {[0:`GRID_SIZE_Y-1]};
       feeder_grid_loc_y[n] + in_grid_size_y[n]  <= `GRID_SIZE_Y;
    }
    drainer_grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    drainer_grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
    feeder_grid_loc_x[0] + in_grid_size_x[0] <= feeder_grid_loc_x[1];
    feeder_grid_loc_x[1] + in_grid_size_x[1] <= grid_loc_x;
    grid_loc_x + grid_size_x <= drainer_grid_loc_x;

    drainer_grid_loc_x + grid_size_x <= `GRID_SIZE_X;
    drainer_grid_loc_y + grid_size_y <= `GRID_SIZE_Y;
  }

  constraint formats {
    foreach (in_data_format[n]) {
       in_data_format[n] dist {[bfp8:fp16_b]:=80, fp32:=20};
    }
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    (out_data_format inside {bfp8_b, fp32, fp16_b}) -> ((in_data_format[0] inside {bfp8_b, fp32, fp16_b}) &&  (in_data_format[1] inside {bfp8_b, fp32, fp16_b}));
    (out_data_format inside {bfp8, fp16}) -> ((in_data_format[0] inside {bfp8, fp16}) &&  (in_data_format[1] inside {bfp8, fp16})); 
    (in_data_format[0] inside {bfp8_b, fp32, fp16_b}) -> (in_data_format[1] inside {bfp8_b, fp32, fp16_b});
    (in_data_format[0] inside {bfp8, fp16}) -> (in_data_format[1] inside {bfp8, fp16});
    (mblock_k > 1) -> (out_data_format == in_data_format[1]); //if we are doing spill and reload unpacker1 and packer format have to match
    foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
    (dest_fp32_acc_enable == 1) -> ((in_data_format[0] == fp32) && (in_data_format[1] == fp32)); //Issue #338

    (in_0_tile_dim_r == 1) -> (`constraint_data_format_tile_dim_1x32(in_data_format[0]));
    (in_0_tile_dim_r == 2) -> (`constraint_data_format_tile_dim_2x32(in_data_format[0]));
    (out_tile_dim_r == 1) -> (`constraint_data_format_tile_dim_1x32(out_data_format));
    (out_tile_dim_r == 2) -> (`constraint_data_format_tile_dim_2x32(out_data_format));
  }

  constraint untilize {
    (out_data_format inside {fp16,fp16_b,fp32}) -> (untilize_output == 1);
    (out_data_format inside {bfp8,bfp8_b}) -> (untilize_output == 0);
  }

  constraint fast_tilize {
    fast_tilize_input == 1;  // enable hw tilizer
  }

  constraint rand_l1_acc {
    l1_acc_enable == 0 || (out_data_format inside {fp16, bfp8, bfp2, bfp4, bfp8_b, bfp2_b, bfp4_b}) -> l1_acc == 0;
  }
  
  constraint rand_stoch_rnd_mode {
    (srnd_enable == 0) -> stoch_rnd_mode == None;
    (srnd_enable == 1) -> {
      out_data_format == fp16_b && mblock_k* grid_size_x* ublock_kt > 128 -> stoch_rnd_mode == All;
      out_data_format == fp16 && mblock_k* grid_size_x* ublock_kt > 512 -> stoch_rnd_mode == All;
    }
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((2*ublock_rt*ublock_kt*mblock_m*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE);
     ((2*ublock_rt*ublock_kt*mblock_m*in_tile_size[0] + 2*mblock_m*mblock_k*ublock_rt*ublock_kt*in_tile_size[0]) <= `MAX_L1_MEM_BUFFER_SIZE); // for feeder in format == out format
     ((2*ublock_kt*ublock_ct*mblock_m*in_tile_size[1] + 2*mblock_k*mblock_n*ublock_kt*ublock_ct*in_tile_size[1]) <= `MAX_L1_MEM_BUFFER_SIZE); // for feeder in format == out format
  }

  rand bit [2:0] dram_channel[0:`GRID_SIZE_X*`GRID_SIZE_Y-1]; 
  
  constraint rand_dram_channel {
    foreach (dram_channel[n]) {
       (fast_tilize_input == 1) -> dram_channel[n] == 0;
    }
  }

  rand bit[39:0] in_q_dram_addr[2];
  rand bit[39:0] out_q_dram_addr;

  rand bit [39:0] in_q_size[2];
  rand bit [39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size[0] == (t*mblock_m*ublock_rt*mblock_k*ublock_kt*num_entries*num_loops*in_tile_size[0] + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[1] == (t*mblock_k*ublock_kt*mblock_n*ublock_ct*num_entries*num_loops*in_tile_size[1] + `DRAM_QUEUE_HEADER_SIZE);

    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
       if (n < 1) {
          in_q_dram_addr[n] + (in_q_size[n]*in_grid_size_x[n]*in_grid_size_y[n]) < (in_q_dram_addr[n+1]);
       } else {
          in_q_dram_addr[n] + (in_q_size[n]*in_grid_size_x[n]*in_grid_size_y[n]) < (out_q_dram_addr);
       }
    }

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*out_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};

    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`FAST_TILIZER_DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_kt <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 0 -> ublock_kt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_kt <= `MAX_TILES_IN_DEST_FP32/2;
     dest_fp32_acc_enable == 1 -> ublock_kt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }

  constraint reduce_num_inputs {
     num_inputs inside {[1:16]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_k*ublock_rt*ublock_kt <= 2048);
     (t*mblock_k*mblock_n*ublock_kt*ublock_ct <= 2048);
     // Reduce runtime when host has to convert input to bfp format
     foreach (in_data_format[n]) {
        ((in_data_format[n] inside {[bfp8:bfp8_b]}) || (out_data_format inside {[bfp8:bfp8_b]})) -> ((t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}) &&
                                                                                                     (mblock_k inside {[1:4]}) && (ublock_kt inside {[1:2]})); 
     }
  }

endclass

integer num_rand_loops=1;
integer ntb_random_seed=0;
string out_filename;
integer out_filehandle;

bit [15:0] in_mblock_m;
bit [15:0] in_mblock_n;
bit [15:0] in_mblock_k;
bit [ 7:0] in_ublock_rt;
bit [ 7:0] in_ublock_ct;
bit [ 7:0] in_ublock_kt;

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
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end
  end

  initial begin
    matmul_op_with_fd_constraints constraints = new();
    test_config test_cfg = new();
    out_filehandle = $fopen(out_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       constraints.randomize() ;
       if (constraints.fast_tilize_input) begin
         in_mblock_m = constraints.mblock_m*constraints.ublock_rt;
         in_mblock_n = constraints.mblock_n*constraints.ublock_ct;
         in_mblock_k = constraints.mblock_k*constraints.ublock_kt;
         in_ublock_rt = 1;
         in_ublock_ct = 1;
         in_ublock_kt = 1;
       end else begin
         in_mblock_m = constraints.mblock_m;
         in_mblock_n = constraints.mblock_n;
         in_mblock_k = constraints.mblock_k;
         in_ublock_rt = constraints.ublock_rt;
         in_ublock_ct = constraints.ublock_ct;
         in_ublock_kt = constraints.ublock_kt;
       end
       if (constraints.untilize_output) begin
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
       $fwrite(out_filehandle, "  t: %0d\n", constraints.t);
       $fwrite(out_filehandle, "  mblock_m: %0d\n", constraints.mblock_m);
       $fwrite(out_filehandle, "  mblock_n: %0d\n", constraints.mblock_n);
       $fwrite(out_filehandle, "  mblock_k: %0d\n", constraints.mblock_k);
       $fwrite(out_filehandle, "  ublock_rt: %0d\n", constraints.ublock_rt);
       $fwrite(out_filehandle, "  ublock_ct: %0d\n", constraints.ublock_ct);
       $fwrite(out_filehandle, "  ublock_kt: %0d\n", constraints.ublock_kt);
       $fwrite(out_filehandle, "  in_0_tile_dim_r: %0d\n", constraints.in_0_tile_dim_r);
       $fwrite(out_filehandle, "  in_0_tile_dim_c: %0d\n", constraints.in_0_tile_dim_c);
       $fwrite(out_filehandle, "  in_1_tile_dim_r: %0d\n", constraints.in_1_tile_dim_r);
       $fwrite(out_filehandle, "  in_1_tile_dim_c: %0d\n", constraints.in_1_tile_dim_c);
       $fwrite(out_filehandle, "  m_k: %0d\n", constraints.mblock_k);
       $fwrite(out_filehandle, "  u_kt: %0d\n", constraints.ublock_kt);
       $fwrite(out_filehandle, "  l1_acc: %s\n", constraints.l1_acc ? "true" : "false");
       $fwrite(out_filehandle, "  stoch_rnd_mode: %s\n", get_stoch_rnd_mode(constraints.stoch_rnd_mode));
       $fwrite(out_filehandle, "  in_mblock_m: %0d\n", in_mblock_m);
       $fwrite(out_filehandle, "  in_mblock_n: %0d\n", in_mblock_n);
       $fwrite(out_filehandle, "  in_mblock_k: %0d\n", in_mblock_k);
       $fwrite(out_filehandle, "  in_ublock_rt: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  in_ublock_ct: %0d\n", in_ublock_ct);
       $fwrite(out_filehandle, "  in_ublock_kt: %0d\n", in_ublock_kt);
       for (int i=0;i<2; i++) begin
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.in_grid_size_x[i]*constraints.in_grid_size_y[i]; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.in_grid_size_x[i]*constraints.in_grid_size_y[i]-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
       end
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       if (constraints.dest_fp32_acc_enable) $fwrite(out_filehandle, "  # Dest FP32 accumulation enabled\n");
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       for (int i=0;i<2; i++) begin
          $fwrite(out_filehandle, "  feeder%0d_grid_loc_x: %0d\n", i, constraints.feeder_grid_loc_x[i]);
          $fwrite(out_filehandle, "  feeder%0d_grid_loc_y: %0d\n", i, constraints.feeder_grid_loc_y[i]);
       end
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  drainer_grid_loc_x: %0d\n", constraints.drainer_grid_loc_x);
       $fwrite(out_filehandle, "  drainer_grid_loc_y: %0d\n", constraints.drainer_grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       $fwrite(out_filehandle, "  transpose_in1: %s\n", constraints.transpose_in1 ? "transpose_in1" : "");
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
       $fwrite(out_filehandle, "  out_tile_dim_r: %0d\n", constraints.out_tile_dim_r);
       $fwrite(out_filehandle, "  out_tile_dim_c: %0d\n", constraints.out_tile_dim_c);
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_matmul_comparison_config(out_filehandle, constraints.out_data_format, constraints.num_inputs);
       test_cfg.write_matmul_stimulus_config(out_filehandle);
    end
    $fclose(out_filehandle);
  end

endprogram
