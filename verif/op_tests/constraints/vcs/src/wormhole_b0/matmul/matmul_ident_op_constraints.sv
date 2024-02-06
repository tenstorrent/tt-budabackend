`include "global_constraints.sv"
`include "test_config.sv"

`define EXTRA_PIPEGEN_L1_MEM (128*1024)


class matmul_ident_op_constraints extends global_constraints;

  rand int num_loops;
  rand e_data_format in_data_format[3];
  rand e_data_format out_data_format;
  rand e_data_format intermed_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit [15:0] in_tile_size[3];
  rand bit [15:0] out_tile_size;
  rand bit [15:0] intermed_tile_size;
  rand bit [7:0] mblock_k;
  rand bit [7:0] ublock_kt;
  rand bit[5:0] in_1_tile_dim_r;
  rand bit[5:0] in_1_tile_dim_c;

  rand int num_sparse_tiles;
  rand int num_index_tiles;
  rand int sparse_tile_ptr_bits;
  rand bit l1_acc;
  rand e_stoch_rnd_mode stoch_rnd_mode;

  constraint tile_dims {
   enable_tiny_tile dist {[0:0]:=70, [1:1]:=30};
   if (enable_tiny_tile != 0) {
      out_tile_dim_r == in_1_tile_dim_r;
      out_tile_dim_c == in_1_tile_dim_c;

      in_1_tile_dim_r == 32;
      in_1_tile_dim_c dist {32:=10, 16:=90};

   } else {
      in_1_tile_dim_r == 32;
      in_1_tile_dim_c == 32;
      out_tile_dim_r == 32;
      out_tile_dim_c == 32;
   }
  }

  constraint inner_dims {
    mblock_k inside {[1:1024]};
    ublock_kt inside {[1:8]};
  }

  constraint set_num_sparse_tiles {
    num_sparse_tiles == 128; //always pad to this size
  }

  constraint set_num_index_tiles {
    num_index_tiles == 16; //always pad to this size
  }

  constraint set_sparse_tile_ptr_bits {
    sparse_tile_ptr_bits == 8; 
  }

  constraint untilize {
    (untilize_output == 0);
  }

  constraint formats {
    in_data_format[0] inside {bfp2, bfp2_b};
    in_data_format[1] dist {[bfp8:fp16_b]:=80, fp32:=20};
    in_data_format[2] == fp32; //same size as RawUInt32
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    intermed_data_format dist {[bfp8:fp16_b]:=80, fp32:=20};
    (out_data_format inside {bfp8_b, fp32, fp16_b}) -> ((in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) &&  (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}));
    (out_data_format inside {bfp8, fp16}) -> ((in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) &&  (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16})); 
    (in_data_format[0] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> (in_data_format[1] inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b});
    (in_data_format[0] inside {bfp8, bfp4, bfp2, fp16}) -> (in_data_format[1] inside {bfp8, bfp4, bfp2, fp16});

    //((mblock_k) > 1) -> (in_data_format[1] inside {[bfp8:fp32]} && out_data_format == in_data_format[1]); //if we are doing spill and reload unpacker1 and packer format have to match
    (out_data_format inside {bfp8_b, bfp4_b, bfp2_b, fp32, fp16_b}) -> (intermed_data_format inside {bfp8_b, fp32, fp16_b});
    (out_data_format inside {bfp8, bfp4, bfp2, fp16}) -> (intermed_data_format inside {bfp8, fp16});

    (dest_fp32_acc_enable == 1) -> (intermed_data_format == fp32);  //accumulates use mova2d, which needs intermediates to be same data format as dest format
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

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     if (untilize_output == 0) { 
	if (out_data_format != intermed_data_format) {
		((2*num_sparse_tiles*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + 2*num_index_tiles*in_tile_size[2] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size + mblock_n*mblock_m*ublock_rt*ublock_ct*intermed_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE-`EXTRA_PIPEGEN_L1_MEM);
	} else { 
                ((2*num_sparse_tiles*in_tile_size[0] + 2*ublock_kt*ublock_ct*mblock_n*in_tile_size[1] + 2*num_index_tiles*in_tile_size[2] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE-`EXTRA_PIPEGEN_L1_MEM));
        }
     }
  }

  rand bit[39:0] in_q_dram_addr[3];
  rand bit[39:0] out_q_dram_addr;

  rand bit[39:0] in_q_size[3];
  rand bit[39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size[0] == (num_sparse_tiles*num_entries*in_tile_size[0] + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[1] == (t*mblock_k*ublock_kt*mblock_n*ublock_ct*in_tile_size[1]*num_entries*num_loops + `DRAM_QUEUE_HEADER_SIZE);
    in_q_size[2] == (num_index_tiles*num_entries*in_tile_size[2] + `DRAM_QUEUE_HEADER_SIZE);
    foreach (in_q_dram_addr[n]) {
       in_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       in_q_dram_addr[n] inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       if (n < 2) {
          if (n==0) {
            in_q_dram_addr[n] + in_q_size[n]*grid_size_y < (in_q_dram_addr[n+1]);
          } else {
            in_q_dram_addr[n] + in_q_size[n]*grid_size_x< (in_q_dram_addr[n+1]);
          }
       } else {
          in_q_dram_addr[n] + (in_q_size[n]*grid_size_y) < (out_q_dram_addr);
       }
    }

    out_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*out_tile_size*num_entries*num_loops + `DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     dest_fp32_acc_enable == 1 -> ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
  }
  
  constraint rand_stoch_rnd_mode {
    (srnd_enable == 0) -> stoch_rnd_mode == None;
    (srnd_enable == 1) -> {
      out_data_format == fp16_b && mblock_k* grid_size_x* ublock_kt > 128 -> stoch_rnd_mode == All;
      out_data_format == fp16 && mblock_k* grid_size_x* ublock_kt > 512 -> stoch_rnd_mode == All;
    }
    (intermed_data_format inside {bfp8, bfp2, bfp4, bfp8_b, bfp2_b, bfp4_b}) -> (stoch_rnd_mode inside {None, Fpu});
  }

  constraint reduce_num_inputs {
     num_inputs inside {[1:4]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint set_t {
     mblock_k % t == 0;
  }

  //constraint reduce_runtime {
  //   // Limit tile count per input
  //   (t*mblock_m*mblock_k*ublock_rt*ublock_kt <= 64*1024);
  //   (t*mblock_k*mblock_n*ublock_kt*ublock_ct <= 64*1024);
  //}


endclass

class matmul_ident_op_constraints_int8 extends matmul_ident_op_constraints;
   // Base class constraints that are replaced with new constraints
   constraint formats {
      in_data_format[0] == int8;
      in_data_format[1] == int8;
      in_data_format[2] == int32;
      intermed_data_format == int32;
      out_data_format dist {int8:=60, int32:=40};
      dest_fp32_acc_enable == 0;
   }

   constraint rand_num_inputs {
     // generate less number of tensors for int8 to execute tests faster
     // tenstorrent/budabackend#1817
      num_inputs inside {[1:2]};
   }

   constraint mm_ident_tile_dim {
      in_1_tile_dim_r == 32;
      in_1_tile_dim_c == 32;
      out_tile_dim_r == 32;
      out_tile_dim_c == 32;
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
bit int8_enabled=0;

string out_filename;
string cmd_filename;
integer out_filehandle;
integer cmd_filehandle;

bit [15:0] out_mblock_m;
bit [15:0] out_mblock_n;
bit [ 7:0] out_ublock_rt;
bit [ 7:0] out_ublock_ct;

bit [ 7:0] out_grid_size_x;
bit [ 7:0] out_grid_size_y;
integer core_count_per_input=0;
string cmd;
integer spm_tool_test_id_offset=100;

program generator();
  initial begin
      $value$plusargs("num_rand_loops=%d", num_rand_loops);
      $value$plusargs("ntb_random_seed=%d", ntb_random_seed);
      $value$plusargs("int8=%d", int8_enabled);
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end

      if (!$value$plusargs("cmd=%s", cmd_filename)) begin
         $fatal("Cmd file name not specified!");
      end else begin
         $display("INFO: Writing spm generator commands to %s", cmd_filename);
      end
  end

  initial begin
    matmul_ident_op_constraints constraints = new();
    test_config test_cfg = new();
    if (int8_enabled) begin
      constraints = matmul_ident_op_constraints_int8::new;
    end
    out_filehandle = $fopen(out_filename, "w");
    out_filehandle = $fopen(out_filename, "w");
    cmd_filehandle = $fopen(cmd_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);

    $fwrite(cmd_filehandle, "#!/bin/bash\n");
    $fwrite(cmd_filehandle, "out_dir=\"out-bin\"\n");

    $fwrite(cmd_filehandle, "if [ \"$#\" -eq  \"0\" ]\n");
    $fwrite(cmd_filehandle, "    then\n");
    $fwrite(cmd_filehandle, "    out_dir=out-bin\n");
    $fwrite(cmd_filehandle, "else\n");
    $fwrite(cmd_filehandle, "    out_dir=$1\n");
    $fwrite(cmd_filehandle, "fi\n");


    for (int i =0; i<num_rand_loops  ; i++) begin
    //$system("ls");
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

       cmd = $psprintf("bash -c \"./build/bin/spm %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d $out_dir\"\n", spm_tool_test_id_offset+i, ntb_random_seed, 
                                                               constraints.grid_size_y, constraints.grid_size_x, 
                                                               constraints.mblock_m, constraints.mblock_n, constraints.mblock_k,
                                                               constraints.ublock_rt, constraints.ublock_ct, constraints.ublock_kt,
                                                               constraints.num_index_tiles, constraints.num_sparse_tiles, constraints.sparse_tile_ptr_bits);
                                                               
       //$display("%s", cmd);  

       $fwrite(cmd_filehandle, "echo %s\n", cmd);
       $fwrite(cmd_filehandle, cmd);

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
       $fwrite(out_filehandle, "  out_tile_dim_r: %0d\n", constraints.out_tile_dim_r);
       $fwrite(out_filehandle, "  out_tile_dim_c: %0d\n", constraints.out_tile_dim_c);
       $fwrite(out_filehandle, "  in_1_tile_dim_r: %0d\n", constraints.in_1_tile_dim_r);
       $fwrite(out_filehandle, "  in_1_tile_dim_c: %0d\n", constraints.in_1_tile_dim_c); 
       $fwrite(out_filehandle, "  m_k: %0d\n", constraints.mblock_k);
       $fwrite(out_filehandle, "  u_kt: %0d\n", constraints.ublock_kt);
       $fwrite(out_filehandle, "  l1_acc: %s\n", constraints.l1_acc ? "true" : "false");
       $fwrite(out_filehandle, "  stoch_rnd_mode: %s\n", get_stoch_rnd_mode(constraints.stoch_rnd_mode));
       $fwrite(out_filehandle, "  num_sparse_tiles: %0d\n", constraints.num_sparse_tiles);
       $fwrite(out_filehandle, "  num_index_tiles: %0d\n", constraints.num_index_tiles);
       $fwrite(out_filehandle, "  sparse_tile_ptr_bits: %0d\n", constraints.sparse_tile_ptr_bits);
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  intermed_data_format: '%s'\n", get_data_format(constraints.intermed_data_format));
       for (int i=0;i<3; i++) begin
          if (i==1) core_count_per_input = constraints.grid_size_x; else core_count_per_input=constraints.grid_size_y;
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<core_count_per_input; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<core_count_per_input-1) $fwrite(out_filehandle, ", ");
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
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
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
       test_cfg.write_matmul_comparison_config(out_filehandle, constraints.out_data_format, constraints.num_inputs, constraints.mblock_k*constraints.ublock_kt, constraints.math_fidelity, 0, 1);
       test_cfg.write_matmul_stimulus_config(out_filehandle, constraints.out_data_format);
    end
    $fclose(out_filehandle);
    $fclose(cmd_filehandle);
  end

endprogram
