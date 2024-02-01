`include "global_constraints.sv"
`include "test_config.sv"


class unary_op_with_fd_constraints extends global_constraints;

  rand e_unary_type unary_type;
  rand e_data_format in_data_format;
  rand e_data_format out_data_format;
  rand e_vector_mode sfpu_vector_mode; // does nothing for datacopy
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit transpose;
  rand integer in_tile_size;
  rand integer out_tile_size;
  rand integer num_loops;
  rand bit [7:0] feeder_grid_loc_x;
  rand bit [7:0] drainer_grid_loc_x;
  rand bit [7:0] feeder_grid_loc_y;
  rand bit [7:0] drainer_grid_loc_y;
  rand bit[5:0] in_tile_dim_r;
  rand bit[5:0] in_tile_dim_c;

  constraint unary_tile_dims {
    if (enable_tiny_tile != 0) {
      `default_tile_dim_constraints(in_tile_dim_r, in_tile_dim_c)
      out_tile_dim_c == 16 -> in_tile_dim_r == 32 && in_tile_dim_c == 16;
      out_tile_dim_c == 32 -> in_tile_dim_c == 32;
    } else {
      in_tile_dim_r == 32;
      in_tile_dim_c == 32;
      out_tile_dim_r == 32;
      out_tile_dim_c == 32;
    }
  }

  constraint constraint_sfpu_vector_mode {
    if (in_tile_dim_r != 32 || in_tile_dim_c != 32) {
      // For cases input == 32x16, we want vector mode c
      if (in_tile_dim_c == 16) {
        sfpu_vector_mode == vector_mode_c;
      } else {
        // Otherwise, for cases of Nx32, we want vector mode r (N != 32)
        sfpu_vector_mode == vector_mode_r;
      }
    } else {
      sfpu_vector_mode dist {vector_mode_rc:=50, vector_mode_r:=25, vector_mode_c:=25};
    }
  }

  constraint rand_fd_grid_loc { // layout ops assuming horizontal split
    feeder_grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    feeder_grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
    drainer_grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    drainer_grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
    feeder_grid_loc_x + grid_size_x <= grid_loc_x;
    grid_loc_x + grid_size_x <= drainer_grid_loc_x;
    drainer_grid_loc_x + grid_size_x <= `GRID_SIZE_X;

    feeder_grid_loc_y + grid_size_y  <= `GRID_SIZE_Y;
    drainer_grid_loc_y + grid_size_y <= `GRID_SIZE_Y;
  }

  constraint formats {
    in_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    in_data_format == out_data_format;
    in_tile_size == `get_tile_size(in_data_format);
    out_tile_size == `get_tile_size(out_data_format);
    (dest_fp32_acc_enable == 1) -> in_data_format == fp32;
  }

  constraint untilize {
      (out_data_format inside {fp16,fp16_b,fp32}) -> (untilize_output == 1);
      (out_data_format inside {bfp8,bfp8_b}) -> (untilize_output == 0);
  }

  constraint force_transpose {
    transpose dist {0:=100, 1:=0}; //covered with op test
    ((`DEVICE != "WORMHOLE_B0")&&(unary_type != datacopy)&&(unary_type != nop)) -> (transpose == 0);
  }
   
  constraint fast_tilize {
    fast_tilize_input == 1;  // enable hw tilizer
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((2*ublock_rt*ublock_ct*in_tile_size + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE);
  }
  
  constraint rand_dram_channel_fast_tilize {
    foreach (dram_channel[n]) {
       (fast_tilize_input == 1) -> dram_channel[n] == 0;
    }
  }

  rand bit[39:0] in_q_dram_addr;
  rand bit[39:0] out_q_dram_addr;

  rand bit [39:0] in_q_size;
  rand bit [39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*in_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    in_q_dram_addr[4:0] == 5'b00000; // align to 32B
    in_q_dram_addr inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
    in_q_dram_addr + (in_q_size*grid_size_x*grid_size_y) < out_q_dram_addr;

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*out_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`FAST_TILIZER_DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }

  constraint reduce_num_inputs {
     num_inputs inside {[1:32]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }


  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_n*ublock_ct*ublock_rt <= 4096);
     // Reduce runtime when host has to convert input to bfp format
     ((in_data_format inside {[bfp8:bfp8_b]}) || (out_data_format inside {[bfp8:bfp8_b]})) -> ((num_inputs inside {[1:4]}) && (t inside {[1:2]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}));
  }

endclass

integer num_rand_loops=1;
integer ntb_random_seed=0;
string out_filename;
integer out_filehandle;

bit [15:0] in_mblock_m;
bit [15:0] in_mblock_n;
bit [ 7:0] in_ublock_rt;
bit [ 7:0] in_ublock_ct;

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
    unary_op_with_fd_constraints constraints = new();
    test_config test_cfg = new();
    out_filehandle = $fopen(out_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       constraints.randomize() ;
       if (constraints.fast_tilize_input) begin
         in_mblock_m = constraints.mblock_m*constraints.ublock_rt;
         in_mblock_n = constraints.mblock_n*constraints.ublock_ct;
         in_ublock_rt = 1;
         in_ublock_ct = 1;
       end else begin
         in_mblock_m = constraints.mblock_m;
         in_mblock_n = constraints.mblock_n;
         in_ublock_rt = constraints.ublock_rt;
         in_ublock_ct = constraints.ublock_ct;
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
       $fwrite(out_filehandle, "  ublock_rt: %0d\n", constraints.ublock_rt);
       $fwrite(out_filehandle, "  ublock_ct: %0d\n", constraints.ublock_ct);
       $fwrite(out_filehandle, "  out_tile_dim_r: %0d\n", constraints.out_tile_dim_r);
       $fwrite(out_filehandle, "  out_tile_dim_c: %0d\n", constraints.out_tile_dim_c);
       $fwrite(out_filehandle, "  in_mblock_m: %0d\n", in_mblock_m);
       $fwrite(out_filehandle, "  in_mblock_n: %0d\n", in_mblock_n);
       $fwrite(out_filehandle, "  in_ublock_rt: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  in_ublock_ct: %0d\n", in_ublock_ct);
       $fwrite(out_filehandle, "  in_tile_dim_r: %0d\n", constraints.in_tile_dim_r);
       $fwrite(out_filehandle, "  in_tile_dim_c: %0d\n", constraints.in_tile_dim_c);
       $fwrite(out_filehandle, "  in_data_format: '%s'\n", get_data_format(constraints.in_data_format));
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  sfpu_vector_mode: '%s'\n", get_vector_mode(constraints.sfpu_vector_mode));
       $fwrite(out_filehandle, "  in_dram_buffers: '[");
       for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
          $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr + core*constraints.in_q_size);
          if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  unary_type: %s\n", get_unary_type(constraints.unary_type));
       $fwrite(out_filehandle, "  feeder_grid_loc_x: %0d\n", constraints.feeder_grid_loc_x);
       $fwrite(out_filehandle, "  feeder_grid_loc_y: %0d\n", constraints.feeder_grid_loc_y);
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  drainer_grid_loc_x: %0d\n", constraints.drainer_grid_loc_x);
       $fwrite(out_filehandle, "  drainer_grid_loc_y: %0d\n", constraints.drainer_grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       $fwrite(out_filehandle, "  transpose: %s\n", constraints.transpose ? "[transpose]" : "");
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
       test_cfg.write_unary_comparison_config(out_filehandle, constraints.out_data_format, constraints.unary_type, constraints.in_tile_dim_r, constraints.out_tile_dim_r, constraints.in_tile_dim_c, constraints.sfpu_vector_mode);
       test_cfg.write_unary_stimulus_config(out_filehandle, constraints.unary_type, constraints.in_data_format, constraints.out_data_format);
    end
    $fclose(out_filehandle);
  end

endprogram
