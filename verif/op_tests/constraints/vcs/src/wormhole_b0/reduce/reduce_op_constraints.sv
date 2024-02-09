`include "global_constraints.sv"
`include "test_config.sv"

class reduce_op_constraints extends global_constraints;

  rand e_reduce_type reduce_type;
  rand e_reduce_dim reduce_dim;
  rand e_data_format in_data_format;
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output;
  rand bit gradient_acc;
  rand integer in_tile_size;
  rand integer out_tile_size;
  rand integer num_loops;
  rand integer z_dim;
  rand integer out_t;
  rand integer reduced_mblock_m;
  rand integer reduced_ublock_rt;
  rand integer reduced_mblock_n;
  rand integer reduced_ublock_ct;

  constraint reduce_dims {
    reduce_dim == r -> (grid_size_y == 1);
    reduce_dim == r -> (reduced_mblock_m == 1);
    reduce_dim == r -> (reduced_ublock_rt == 1);

    reduce_dim != r -> (reduced_mblock_m == mblock_m);
    reduce_dim != r -> (reduced_ublock_rt == ublock_rt);

    reduce_dim == c -> (grid_size_x == 1);
    reduce_dim == c -> (reduced_mblock_n == 1);
    reduce_dim == c -> (reduced_ublock_ct == 1);

    reduce_dim != c -> (reduced_mblock_n == mblock_n);
    reduce_dim != c -> (reduced_ublock_ct == ublock_ct);
  }

  constraint rand_z_dim {
    z_dim < t;
    t % z_dim == 0;
  }

  constraint set_out_t {
    reduce_dim == z -> out_t == (t/z_dim);  
    reduce_dim != z -> out_t == t;
  }

  constraint untilize {
    (out_data_format inside {fp16,fp16_b,fp32}) && (reduce_dim != z) -> (untilize_output == 1);
    (out_data_format inside {bfp8,bfp8_b,bfp4,bfp4_b,bfp2,bfp2_b}) || (reduce_dim == z) -> (untilize_output == 0);
  }

  constraint formats {
    in_data_format dist {[bfp8:fp16_b]:=80, fp32:=10, [bfp4:bfp2_b]:=10};
    out_data_format dist {[bfp8:fp16_b]:=80, fp32:=10, [bfp4:bfp2_b]:=10};
    in_data_format == out_data_format;
    (dest_fp32_acc_enable == 1) -> in_data_format == fp32;
    gradient_acc == 1 -> (in_data_format inside {[bfp8:fp32]}) && (out_data_format inside {[bfp8:fp32]});
  }

   constraint format_tile_sizes {
      in_tile_size == `get_tile_size(in_data_format);
      out_tile_size == `get_tile_size(out_data_format);
   }

  constraint fast_tilize {
    fast_tilize_input == 0;  // disable fast tilizer for reduce op test
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((untilize_output == 1)&&(gradient_acc == 0)) -> (2*ublock_rt*ublock_ct*in_tile_size + 2*mblock_n*reduced_ublock_rt*ublock_ct*out_tile_size <= `MAX_L1_MEM_BUFFER_SIZE); // if output is untilized we need to double buffer only row of ublocks
     ((untilize_output == 0)&&(gradient_acc == 0)) -> (2*ublock_rt*ublock_ct*in_tile_size + 2*reduced_mblock_n*reduced_mblock_m*reduced_ublock_rt*reduced_ublock_ct*out_tile_size <= `MAX_L1_MEM_BUFFER_SIZE); // if output is not untilized we need to buffer entire block 
     ((untilize_output == 0)&&(gradient_acc == 1)) -> (2*ublock_rt*ublock_ct*in_tile_size + 2*reduced_mblock_n*reduced_mblock_m*reduced_ublock_rt*reduced_ublock_ct*out_tile_size*t <= `MAX_L1_MEM_BUFFER_SIZE); // we need to buffer entire t for grad acc
  }

  rand bit[39:0] in_q_dram_addr;
  rand bit[39:0] out_q_dram_addr;

  rand bit [39:0] in_q_size;
  rand bit [39:0] out_q_size;

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size == (t*mblock_m*ublock_rt*mblock_n*ublock_ct*in_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    in_q_dram_addr[4:0] == 5'b00000; // align to 32B
    in_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    in_q_dram_addr + (in_q_size*grid_size_x*grid_size_y) < out_q_dram_addr;

    out_q_size == (out_t*reduced_mblock_m*reduced_ublock_rt*reduced_mblock_n*reduced_ublock_ct*out_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    out_q_dram_addr[4:0] == 5'b00000; // align to 32B
    out_q_dram_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
    out_q_dram_addr + (out_q_size*grid_size_x*grid_size_y) <= (`DRAM_BUFFER_END_ADDR);
  }

  constraint max_dest_dim {
     //use half of the dest space for half sync mode
     dest_fp32_acc_enable == 0 -> reduced_ublock_rt*reduced_ublock_ct <= `MAX_TILES_IN_DEST_REDUCE/2;
     dest_fp32_acc_enable == 1 -> reduced_ublock_rt*reduced_ublock_ct <= `MAX_TILES_IN_DEST_REDUCE_FP32/2;
  }

  constraint rand_num_inputs {
     num_inputs inside {[1:16]};
  }

  constraint rand_program_loops {
     num_loops inside {[1:2]};
  }

  constraint reduce_runtime {
     // Limit tile count per input
     (t*mblock_m*mblock_n*ublock_ct*ublock_rt <= 4096);
     // Reduce runtime when host has to convert input to bfp format
     (in_data_format inside {[bfp8:bfp8_b],[bfp4:bfp2_b]}) -> ((num_inputs inside {[1:4]}) && (t inside {[1:4]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}) &&
                                                                       (ublock_ct inside {[1:4]}) && (ublock_rt inside {[1:4]})); 
  }


endclass

class reduce_op_constraints_int8 extends reduce_op_constraints;
   // Base class constraints that are replaced with new constraints
   constraint formats {
      in_data_format == int8;
      out_data_format == int8;
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

endclass

integer num_rand_loops=1;
integer ntb_random_seed=0;
integer gradient_acc=0;
integer int8_enabled = 0;
string reduce_dimension = ""; // r, c, z
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
      $value$plusargs("reduce_dim=%s", reduce_dimension);
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end
  end

  initial begin
    reduce_op_constraints constraints = new();
    test_config test_cfg = new();
    if (int8_enabled) begin
      constraints = reduce_op_constraints_int8::new();
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
         if (reduce_dimension == "") begin
            constraints.randomize() with {
               gradient_acc == 0;
            };
         end else begin
            if (reduce_dimension == "r") begin
               constraints.randomize() with {
                  gradient_acc == 0;
                  reduce_dim == r;
               };
            end else if (reduce_dimension == "c") begin
               constraints.randomize() with {
                  gradient_acc == 0;
                  reduce_dim == c;
               };
            end else if (reduce_dimension == "z") begin
               constraints.randomize() with {
                  gradient_acc == 0;
                  reduce_dim == z;
               };
            end
         end
       end

       if (constraints.fast_tilize_input) begin
          $fatal("fast_tilize_input==1 is not supported!");
       end

       in_mblock_m  = constraints.mblock_m;
       in_mblock_n  = constraints.mblock_n;
       in_ublock_rt = constraints.ublock_rt;
       in_ublock_ct = constraints.ublock_ct;
       
       in_grid_size_x = constraints.grid_size_x;
       in_grid_size_y = constraints.grid_size_y;

       if (constraints.untilize_output) begin
         if (constraints.gradient_acc) begin
            $fatal("untilize_output==1 is not supported whe gradient accumulation is set to true!");
         end
         out_mblock_m = constraints.reduced_mblock_m*constraints.grid_size_y*constraints.reduced_ublock_rt;
         out_mblock_n = constraints.reduced_mblock_n*constraints.grid_size_x*constraints.reduced_ublock_ct;
         out_ublock_rt = 1;
         out_ublock_ct = 1;
         out_grid_size_x = 1;
         out_grid_size_y = 1;
       end else begin
         out_mblock_m = constraints.reduced_mblock_m;
         out_mblock_n = constraints.reduced_mblock_n;
         out_ublock_rt = constraints.reduced_ublock_rt;
         out_ublock_ct = constraints.reduced_ublock_ct;
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
       if (constraints.reduce_dim == r) begin
          $fwrite(out_filehandle, "  m_k: ', m_k: %0d'\n", constraints.mblock_m);
          $fwrite(out_filehandle, "  u_kt: ', u_kt: %0d'\n", constraints.ublock_rt);
       end else if (constraints.reduce_dim == c) begin
          $fwrite(out_filehandle, "  m_k: ', m_k: %0d'\n", constraints.mblock_n);
          $fwrite(out_filehandle, "  u_kt: ', u_kt: %0d'\n", constraints.ublock_ct);
       end else begin
          $fwrite(out_filehandle, "  m_k: ''\n");
          $fwrite(out_filehandle, "  u_kt: ''\n");
       end
       $fwrite(out_filehandle, "  reduced_mblock_m: %0d\n", constraints.reduced_mblock_m);
       $fwrite(out_filehandle, "  reduced_ublock_rt: %0d\n", constraints.reduced_ublock_rt);
       $fwrite(out_filehandle, "  reduced_mblock_n: %0d\n", constraints.reduced_mblock_n);
       $fwrite(out_filehandle, "  reduced_ublock_ct: %0d\n", constraints.reduced_ublock_ct);
       $fwrite(out_filehandle, "  in_mblock_m: %0d\n",  in_mblock_m);
       $fwrite(out_filehandle, "  in_mblock_n: %0d\n",  in_mblock_n);
       $fwrite(out_filehandle, "  in_ublock_rt: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  in_ublock_ct: %0d\n", in_ublock_ct);
       $fwrite(out_filehandle, "  in_data_format: '%s'\n", get_data_format(constraints.in_data_format));
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       if (constraints.dest_fp32_acc_enable) begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp32));
       end else if (int8_enabled) begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(int32));
       end else begin
         $fwrite(out_filehandle, "  dest_accumulate_data_format: '%s'\n", get_data_format(fp16));
       end

       $fwrite(out_filehandle, "  in_dram_buffers: '[");
       for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
          $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr + core*constraints.in_q_size);
          if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  reduce_type: %s\n", get_reduce_type(constraints.reduce_type));
       $fwrite(out_filehandle, "  reduce_dim: %s\n", get_reduce_dim(constraints.reduce_dim));

       if (constraints.reduce_dim == z) begin
          $fwrite(out_filehandle, "  reduce_z_dim: ',z: %0d'\n", constraints.z_dim);
       end else begin
          $fwrite(out_filehandle, "  reduce_z_dim: ''\n");
       end
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       $fwrite(out_filehandle, "  untilize_output: %s\n", constraints.untilize_output ? "'true'" : "'false'");
       $fwrite(out_filehandle, "  transpose: %s\n", "");
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
       $fwrite(out_filehandle, "  out_t: %0d\n", constraints.out_t);
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_unary_comparison_config(out_filehandle, constraints.out_data_format, nop, 32, 32);
       test_cfg.write_unary_stimulus_config(out_filehandle, nop, constraints.in_data_format, constraints.out_data_format);
    end
    $fclose(out_filehandle);
  end

endprogram
