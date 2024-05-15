`include "global_constraints.sv"
`include "test_config.sv"


class matmul_op_constraints extends global_constraints;

  rand e_data_format in_data_format[2];
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit [15:0] in_tile_size[2];
  rand bit [15:0] out_tile_size;
  rand bit [7:0] mblock_k;
  rand bit [7:0] in1_mblock_k;
  rand bit [7:0] ublock_kt;
  rand bit transpose_in1;
  rand integer num_loops;
  rand bit l1_acc;
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
  
  constraint rand_stoch_rnd_mode {
    (srnd_enable == 0) -> stoch_rnd_mode == None;
    (srnd_enable == 1) -> {
      out_data_format == fp16_b && mblock_k* grid_size_x* ublock_kt > 128 -> stoch_rnd_mode == All;
      out_data_format == fp16 && mblock_k* grid_size_x* ublock_kt > 512 -> stoch_rnd_mode == All;
    }
  }

  constraint formats {
    foreach (in_data_format[n]) {
       in_data_format[n] dist {[fp16:fp16_b]:=90, fp32:=10, [bfp4:bfp2_b]:=0};
    }
    out_data_format dist {[fp16:fp16_b]:=80, fp32:=20};
    (out_data_format inside {bfp8_b, fp32, fp16_b}) -> ((in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) &&  (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}));
    (out_data_format inside {bfp8, fp16}) -> ((in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) &&  (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16})); 
    (in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b});
    (in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) -> (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16});
    foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
    //accumulates use mova2d, which needs intermediates to be same data format as dest format, for whb0 its elwadd
    ((dest_fp32_acc_enable == 1) && (`DEVICE != "WORMHOLE_B0")) -> ((in_data_format[0] == fp32) && (in_data_format[1] == fp32) && (out_data_format == fp32));  

    //l1 acc needs intermed fp32 with dest fp32
    ((dest_fp32_acc_enable == 1) && (l1_acc == 1)) -> out_data_format == fp32;
  }

  constraint fast_tilize {
    fast_tilize_input == 0;  // disable fast tilizer for binary op test
  }

  constraint rand_l1_acc { // enable l1 acc
    l1_acc_enable == 0 || (out_data_format inside {fp16, bfp8, bfp2, bfp4, bfp8_b, bfp2_b, bfp4_b}) -> l1_acc == 0;
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     (untilize_output == 0) -> ((2*ublock_rt*ublock_kt*mblock_m*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + t*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE);
  }
  
  rand bit[39:0] in_q_dram_addr[2];
  rand bit[39:0] out_q_dram_addr;

  rand bit[39:0] in_q_size[2];
  rand bit[39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size[0] == (t*mblock_m*ublock_rt*mblock_k*ublock_kt*num_entries*num_loops*in_tile_size[0] + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[1] == (t*in1_mblock_k*ublock_kt*mblock_n*ublock_ct*num_entries*num_loops*in_tile_size[1] + `DRAM_QUEUE_HEADER_SIZE);
    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       if (n < 1) {
          in_q_dram_addr[n] + in_q_size[n]*grid_size_x*grid_size_y < (in_q_dram_addr[n+1]);
       } else {
          in_q_dram_addr[n] + (in_q_size[n]*grid_size_x*grid_size_y) < (out_q_dram_addr);
       }
    }

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*num_entries*num_loops*out_tile_size + `DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }

  constraint rand_num_inputs {
     num_inputs inside {[1:16]};
    (num_inputs != 1) -> (num_inputs%2==0);
  }

  constraint set_t {
     t <= 4; 
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_k*ublock_rt*ublock_kt <= 2048);
     (t*in1_mblock_k*mblock_n*ublock_kt*ublock_ct <= 2048);
     // Reduce runtime when host has to convert input to bfp format
     foreach (in_data_format[n]) {
        ((in_data_format[n] inside {[bfp8:bfp8_b],[bfp4:bfp2_b]}) || (out_data_format inside {[bfp8:bfp8_b],[bfp4:bfp2_b]})) -> ((t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}) && (mblock_k inside {[1:4]}) && (in1_mblock_k inside {[1:4]}) && (ublock_kt inside {[1:2]}));
     }
  }


endclass

integer ntb_random_seed=0;
integer num_rand_loops=1;
string out_filename;
integer out_filehandle;

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
    matmul_op_constraints constraints = new();
    test_config test_cfg = new();
    out_filehandle = $fopen(out_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       constraints.randomize() ;
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
       $fwrite(out_filehandle, "  t: %0d\n", constraints.t);
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
       end else begin
          $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp16));
       end
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       $fwrite(out_filehandle, "  transpose_in1: %s\n", constraints.transpose_in1 ? "[transpose]" : "");
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
       test_cfg.write_matmul_comparison_config(out_filehandle, constraints.out_data_format, constraints.num_inputs, constraints.mblock_k*constraints.grid_size_x*constraints.ublock_kt, constraints.math_fidelity, 1, 0, constraints.out_data_format);
       test_cfg.write_matmul_stimulus_config(out_filehandle);
    end
    $fclose(out_filehandle);
  end

endprogram
