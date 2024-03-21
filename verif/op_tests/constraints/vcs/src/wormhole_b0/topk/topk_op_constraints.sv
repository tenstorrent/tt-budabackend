`include "global_constraints.sv"
`include "test_config.sv"

class topk_op_constraints extends global_constraints;

  rand e_data_format in_data_format[2];
  rand e_data_format out_data_format;
  rand integer in_tile_size[2];
  rand integer out_tile_size;
  rand integer num_loops;
  rand integer attributes_k;
  rand e_topk_sort attributes_sort;
  rand e_topk_reduce attributes_kreduce;

  constraint attributes {
    attributes_k inside {4, 8, 16, 32, 64};
    // bug with K=4/8 and N = 32
    (attributes_k == 4) -> (mblock_n*ublock_ct >= 2);
    (attributes_k == 8) -> (mblock_n*ublock_ct >= 2);
  }

  constraint formats {
    in_data_format[0] dist {bfp8_b:=10, fp16_b:=10};
    in_data_format[1] dist {uint16:=10};
    (attributes_sort == k_max) -> (out_data_format == in_data_format[0]);
    (attributes_sort == k_argmax) -> (out_data_format == in_data_format[1]);
  }

  constraint input_dims {
    mblock_m == 1;
    mblock_n inside {1, 2, 4, 8, 16, 32};
    ublock_rt == 1;
    (attributes_k == 64) -> (ublock_ct inside {2, 4});
    (attributes_k < 64) -> (ublock_ct inside {1, 2, 4});
  }

  constraint format_tile_sizes {
   foreach (in_data_format[n]) {
       in_tile_size[n] == `get_tile_size(in_data_format[n]);
    }
    out_tile_size == `get_tile_size(out_data_format);
  }

  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     (2*ublock_rt*ublock_ct*in_tile_size[0] + 2*ublock_rt*ublock_ct*in_tile_size[1] + 2*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size <= `MAX_L1_MEM_BUFFER_SIZE); // if output is not untilized we need to buffer entire block 
  }

  rand bit[39:0] in_q_dram_addr[2];
  rand bit[39:0] out_q_dram_addr;
  rand bit[39:0] in_q_host_addr[2];

  rand bit [39:0] in_q_size[2];
  rand bit[39:0] out_q_size;

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

  constraint max_dest_dim {
     ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST/2;
     ublock_rt*ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
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
        ((in_data_format[n] == bfp8_b) || (out_data_format == bfp8_b)) -> ((num_inputs inside {[1:4]}) && (t inside {[1:4]}));
     }
  }

endclass

integer ntb_random_seed=0;
integer num_rand_loops=1;
integer gradient_acc=0;
integer int8_enabled=0;
string out_filename;
integer out_filehandle;

bit [15:0] out_mblock_m;
bit [15:0] out_mblock_n;
bit [ 7:0] out_ublock_rt;
bit [ 7:0] out_ublock_ct;

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
    topk_op_constraints constraints = new;
    
    out_filehandle = $fopen(out_filename, "w");

    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       
       constraints.randomize() with {
         gradient_acc == 0;
       };

       if (constraints.attributes_k == 64) begin
         out_mblock_m = constraints.mblock_m;
         out_mblock_n = 1;
         out_ublock_rt = constraints.ublock_rt;
         out_ublock_ct = 2;
       end else begin
         out_mblock_m = constraints.mblock_m;
         out_mblock_n = 1;
         out_ublock_rt = constraints.ublock_rt;
         out_ublock_ct = 1;
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
       for (int i=0;i<2; i++) begin
          $fwrite(out_filehandle, "  in%0d_data_format: '%s'\n", i, get_data_format(constraints.in_data_format[i]));
          $fwrite(out_filehandle, "  in%0d_dram_buffers: '[", i);
          for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
             $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr[i] + core*constraints.in_q_size[i]);
             if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
          end
          $fwrite(out_filehandle, "]'\n");
       end
       $fwrite(out_filehandle, "  dest_accumulate_data_format: Float16_b\n");
       $fwrite(out_filehandle, "  intermed_data_format: '%s'\n", get_data_format(constraints.in_data_format[0]));
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  grid_loc_x: %0d\n", constraints.grid_loc_x);
       $fwrite(out_filehandle, "  grid_loc_y: %0d\n", constraints.grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: HiFi2\n");
       $fwrite(out_filehandle, "  attributes_k: %0d\n", constraints.attributes_k);
       $fwrite(out_filehandle, "  attributes_sort: %s\n", get_attribute_sort(constraints.attributes_sort));
       $fwrite(out_filehandle, "  attributes_kreduce: %s\n", get_attribute_kreduce(constraints.attributes_kreduce));
       
       $fwrite(out_filehandle, "  ublock_order: r\n");
       $fwrite(out_filehandle, "  buf_size_mb: 2\n");
       $fwrite(out_filehandle, "  # Scale output queue grid and mblock size if output is untilized\n");
       $fwrite(out_filehandle, "  # Net2pipe requires grid size 1x1 if untilized output is written to the queue\n");
       $fwrite(out_filehandle, "  out_dram_buffers: '[");
       for (int core=0;core<constraints.grid_size_x*constraints.grid_size_y; core++) begin
         $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.out_q_dram_addr + core*constraints.out_q_size);
         if (core<constraints.grid_size_x*constraints.grid_size_y-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");
       $fwrite(out_filehandle, "  out_mblock_m: %0d\n", out_mblock_m);
       $fwrite(out_filehandle, "  out_mblock_n: %0d\n", out_mblock_n);
       $fwrite(out_filehandle, "  out_ublock_rt: %0d\n", out_ublock_rt);
       $fwrite(out_filehandle, "  out_ublock_ct: %0d\n", out_ublock_ct);
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_topk_comparison_config(out_filehandle, constraints.in_data_format[0], constraints.attributes_k, constraints.attributes_sort);
       test_cfg.write_topk_stimulus_config(out_filehandle, constraints.attributes_k);
    end
    $fclose(out_filehandle);
  end

endprogram
