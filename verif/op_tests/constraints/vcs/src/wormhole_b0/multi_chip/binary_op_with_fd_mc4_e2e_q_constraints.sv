`include "global_constraints.sv"
`include "test_config.sv"


class binary_op_with_fd_mc4_constraints extends global_constraints;

  `undef NUM_TARGET_DEVICES
  `define NUM_TARGET_DEVICES       (4)
  
  
  rand e_binary_type binary_type;
  rand e_data_format in_data_format[2];
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand integer in_tile_size[2];
  rand integer out_tile_size;
  rand integer num_loops;
  rand bit [7:0] feeder_grid_loc_x[2];
  rand bit [7:0] drainer_grid_loc_x;
  rand bit [7:0] feeder_grid_loc_y[2];
  rand bit [7:0] drainer_grid_loc_y;
  rand bit [2:0] in_target_device[2];
  rand bit [2:0] out_target_device;
  rand bit [2:0] op_target_device;
  rand bit[16:0] e2e_num_entries;

  constraint rand_target_devices {
     op_target_device inside {[0:`NUM_TARGET_DEVICES-1]};
     out_target_device inside {[0:`NUM_TARGET_DEVICES-1]};
     op_target_device != out_target_device;
     foreach (in_target_device[n]) {
       in_target_device[n] inside {[0:`NUM_TARGET_DEVICES-1]};
       if (n>0) {
          in_target_device[n-1]!= in_target_device[n]; 
       }
       in_target_device[n] != op_target_device;
       in_target_device[n] != out_target_device;
     }
  }

  constraint rand_e2e_entries {
    e2e_num_entries dist  {num_inputs:=20, 2*num_inputs:=80};
  }

  constraint rand_fd_grid_loc { // layout ops assuming horizontal split
    foreach (feeder_grid_loc_x[n]) {
       feeder_grid_loc_x[n] inside  {[0:`GRID_SIZE_X-1]};
       feeder_grid_loc_x[n] + grid_size_x  <= `GRID_SIZE_X;
    }
    foreach (feeder_grid_loc_y[n]) {
       feeder_grid_loc_y[n] inside  {[0:`GRID_SIZE_Y-1]};
       feeder_grid_loc_y[n] + grid_size_y  <= `GRID_SIZE_Y;
    }
    drainer_grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    drainer_grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
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
    foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
    (dest_fp32_acc_enable == 1) -> (in_data_format[0] == fp32) && (in_data_format[1] == fp32); 
  }

  constraint untilize {
    (out_data_format inside {fp16,fp16_b,fp32}) -> (untilize_output == 1);
    (out_data_format inside {bfp8,bfp8_b}) -> (untilize_output == 0);
  }

  constraint fast_tilize {
    fast_tilize_input == 1;  // enable hw tilizer
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((2*ublock_rt*ublock_ct*in_tile_size[0] + 2*ublock_rt*ublock_ct*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE);
  }

  rand bit [2:0] dram_channel[0:`GRID_SIZE_X*`GRID_SIZE_Y-1]; 
  
  constraint rand_dram_channel {
    foreach (dram_channel[n]) {
       (fast_tilize_input == 1) -> dram_channel[n] == 0;
    }
  }

  rand bit[39:0] in_q_dram_addr[2];
  rand bit[39:0] out_q_dram_addr;

  rand bit[39:0] e2e_in_q_dram_addr[2];
  rand bit[39:0] e2e_out_q_dram_addr;

  rand bit [39:0] in_q_size[2];
  rand bit [39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    foreach (in_tile_size[n]) {
       in_q_size[n] == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*in_tile_size[n]*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    }

    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
       in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (`FAST_TILIZER_DRAM_BUFFER_END_ADDR);
    }

    foreach (e2e_in_q_dram_addr[n]) {
       e2e_in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       e2e_in_q_dram_addr[n] inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
       if (n < 1) {
          e2e_in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (e2e_in_q_dram_addr[n+1]);
       } else {
          e2e_in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (`FAST_TILIZER_DRAM_BUFFER_END_ADDR);
       }
    }

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*out_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);

    e2e_out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    e2e_out_q_dram_addr inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
    e2e_out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) < out_q_dram_addr;

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
     num_inputs inside {[1:16]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:8]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_n*ublock_ct*ublock_rt <= 2048);
     // Reduce runtime when host has to convert input to bfp format
     foreach (in_data_format[n]) {
        ((in_data_format[n] inside {[bfp8:bfp8_b]}) || (out_data_format inside {[bfp8:bfp8_b]})) -> ((num_inputs inside {[1:4]}) && (t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}));
     }
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
    binary_op_with_fd_mc4_constraints constraints = new();
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
       $fwrite(out_filehandle, "  device_count: %0d\n", `NUM_TARGET_DEVICES);
       $fwrite(out_filehandle, "  entries: %0d\n", constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  e2e_entries: %0d\n", constraints.e2e_num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  grid_size_x: %0d\n", constraints.grid_size_x);
       $fwrite(out_filehandle, "  grid_size_y: %0d\n", constraints.grid_size_y);
       $fwrite(out_filehandle, "  t: %0d\n", constraints.t);
       $fwrite(out_filehandle, "  mblock_m: %0d\n", constraints.mblock_m);
       $fwrite(out_filehandle, "  mblock_n: %0d\n", constraints.mblock_n);
       $fwrite(out_filehandle, "  ublock_rt: %0d\n", constraints.ublock_rt);
       $fwrite(out_filehandle, "  ublock_ct: %0d\n", constraints.ublock_ct);
       $fwrite(out_filehandle, "  in_mblock_m: %0d\n", in_mblock_m);
       $fwrite(out_filehandle, "  in_mblock_n: %0d\n", in_mblock_n);
       $fwrite(out_filehandle, "  in_ublock_rt: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  in_ublock_ct: %0d\n", in_ublock_ct);
       for (int i=0;i<2; i++) begin
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
          $fwrite(out_filehandle, "  in%0d_target_device: %0d\n", i, constraints.in_target_device[i]);
          $fwrite(out_filehandle, "  e2e_in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.e2e_in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
       end
       if (constraints.dest_fp32_acc_enable) begin
          $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp32));
       end else begin
          $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp16));
       end
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  op_target_device: %0d\n", constraints.op_target_device);
       $fwrite(out_filehandle, "  out_target_device: %0d\n", constraints.out_target_device);
       if (constraints.dest_fp32_acc_enable) $fwrite(out_filehandle, "  # Dest FP32 accumulation enabled\n");
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  binary_type: %s\n", get_binary_type(constraints.binary_type));
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
       $fwrite(out_filehandle, "  e2e_out_dram_buffers: '[");
       for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
          $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.e2e_out_q_dram_addr + core*constraints.out_q_size);
          if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");
       $fwrite(out_filehandle, "  out_grid_size_x: %0d\n", out_grid_size_x);
       $fwrite(out_filehandle, "  out_grid_size_y: %0d\n", out_grid_size_y);
       $fwrite(out_filehandle, "  out_mblock_m: %0d\n", out_mblock_m);
       $fwrite(out_filehandle, "  out_mblock_n: %0d\n", out_mblock_n);
       $fwrite(out_filehandle, "  out_ublock_rt: %0d\n", out_ublock_rt);
       $fwrite(out_filehandle, "  out_ublock_ct: %0d\n", out_ublock_ct);
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  e2e_queue_wrap_size: %0d\n", 2*constraints.e2e_num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_binary_comparison_config(out_filehandle, constraints.out_data_format, constraints.binary_type);
       test_cfg.write_binary_stimulus_config(out_filehandle, constraints.binary_type);
    end
    $fclose(out_filehandle);
  end

endprogram
