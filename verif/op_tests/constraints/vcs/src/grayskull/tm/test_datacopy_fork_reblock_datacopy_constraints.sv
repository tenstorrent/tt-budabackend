`include "global_constraints.sv"
`include "test_config.sv"

`define ceil(a,b) (((a) / (b)) + (((a) % (b)) != 0)) 

class tm_op_fork_constraints extends global_constraints;

  `define NUM_FORKS (4)
  `define MAX_FORK_STREAMS_PER_CORE (16)

  rand e_unary_type unary_type;
  rand e_data_format in_data_format;
  rand e_data_format out_data_format;
  rand bit fast_tilize_input; //aka hw tilize
  rand bit untilize_output[`NUM_FORKS];
  rand bit [6:0] rand_tm[`NUM_FORKS];
  rand bit transpose[`NUM_FORKS];
  rand bit hstack[`NUM_FORKS];
  rand bit vstack[`NUM_FORKS];
  rand bit hslice[`NUM_FORKS];
  rand bit vslice[`NUM_FORKS];
  rand bit bcast[`NUM_FORKS];
  rand bit [7:0] hstack_dim[`NUM_FORKS];
  rand bit [7:0] vstack_dim[`NUM_FORKS];
  rand bit [7:0] hslice_dim[`NUM_FORKS];
  rand bit [7:0] vslice_dim[`NUM_FORKS];
  rand bit [7:0] bcast_z_dim[`NUM_FORKS];
  rand bit [7:0] bcast_r_dim[`NUM_FORKS];
  rand bit [7:0] bcast_c_dim[`NUM_FORKS];
  rand integer in_tile_size;
  rand integer out_tile_size;
  rand integer num_loops;
  rand bit [7:0] feeder_grid_loc_x;
  rand bit [7:0] feeder_grid_loc_y;
  rand bit [7:0] feeder_grid_size_x;
  rand bit [7:0] feeder_grid_size_y;
  rand bit [7:0] op_grid_loc_x[`NUM_FORKS];
  rand bit [7:0] op_grid_loc_y[`NUM_FORKS];
  rand bit [7:0] op_grid_size_x[`NUM_FORKS];
  rand bit [7:0] op_grid_size_y[`NUM_FORKS];
  rand bit [7:0] out_mblock_m[`NUM_FORKS];
  rand bit [7:0] out_mblock_n[`NUM_FORKS];
  rand bit [7:0] out_ublock_rt[`NUM_FORKS];
  rand bit [7:0] out_ublock_ct[`NUM_FORKS];
  rand integer in_block_ct;
  rand integer in_block_rt;
  rand integer out_block_ct[`NUM_FORKS];
  rand integer out_block_rt[`NUM_FORKS];
  rand bit [7:0] in_t;
  rand bit [7:0] out_t[`NUM_FORKS];
  rand integer in_buf_size_mb;

  // compute resource limits
  rand integer tm_c_factor[`NUM_FORKS];
  rand integer tm_r_factor[`NUM_FORKS]; 
  rand integer effective_consumer_grid_size_r[`NUM_FORKS];
  rand integer effective_consumer_grid_size_c[`NUM_FORKS];
  rand integer effective_consumer_grid_size_r_reblock[`NUM_FORKS];
  rand integer effective_consumer_grid_size_c_reblock[`NUM_FORKS];
  rand integer reblock_tm_fork_r_factor_int[`NUM_FORKS];
  rand integer reblock_tm_fork_c_factor_int[`NUM_FORKS];

  rand integer max_fork_streams_used_per_core[`NUM_FORKS];


  constraint onehot_tm {
    foreach (rand_tm[n]) {
      $onehot(rand_tm[n]) == 1;
    }
  } 

  constraint rand_unary_type { 
     unary_type == datacopy; //use only data copy for this test
  }

  constraint rand_op_grid_size {
    feeder_grid_size_x inside  {[1:`GRID_SIZE_X]};
    feeder_grid_size_y inside  {[1:`GRID_SIZE_Y]};

    foreach (op_grid_size_x[n]) {
      op_grid_size_x[n] inside  {[1:`GRID_SIZE_X]};
    }

    foreach (op_grid_size_y[n]) {
      op_grid_size_y[n] inside  {[1:`GRID_SIZE_Y]};
    }
  }

  constraint rand_op_grid_loc { // layout ops assuming horizontal split
    feeder_grid_loc_x inside  {[0:`GRID_SIZE_X-1]};
    feeder_grid_loc_y inside  {[0:`GRID_SIZE_Y-1]};
    foreach (op_grid_loc_x[n]) {
      op_grid_loc_x[n] inside  {[0:`GRID_SIZE_X-1]};
    }
    foreach (op_grid_loc_y[n]) {
      op_grid_loc_y[n] inside  {[0:`GRID_SIZE_Y-1]};
    }
    
    feeder_grid_loc_x + feeder_grid_size_x <= op_grid_loc_x[0];
    foreach (op_grid_loc_x[n]) {
      if (n<(`NUM_FORKS-1)) {
         op_grid_loc_x[n] + op_grid_size_x[n] <= op_grid_loc_x[n+1];
      } else {
         op_grid_loc_x[n] + op_grid_size_x[n] <= `GRID_SIZE_X;
      }
    }

    feeder_grid_loc_y + feeder_grid_size_y  <= `GRID_SIZE_Y;
    foreach (op_grid_loc_y[n]) {
       op_grid_loc_y[n] + op_grid_size_y[n] <= `GRID_SIZE_Y;
    }
  }

  constraint limit_max_fork_streams {
     // https://tenstorrent.sharepoint.com/:t:/r/sites/Specifications/Spatial/net2pipe/netlist_resource_constraints.txt?csf=1&web=1&e=PxlRtb

     // eltwise input (doesn't matter if unary or binary)
     // need to reblock across both rt and ct

     // If there are no TMs, the fan-outs of reblocking forks depend on the producer and
     // consumer op grid ratio.  In this case, the macro-block size ratio is the inverse
     // of grid size ratio in each dimension.  (We refer to macro-block size in tiles.)  
     // 
     // If there are TMs, they must change the producer output canonical form so that it's
     // identical to the consumer data format canonical form.  In order to get a fan-out
     // from an individual producer core to multiple consumer cores, we must have the
     // equivalent ratio of consumer vs. producer macro-block size.

     foreach (max_fork_streams_used_per_core[n]) {
        // Pessimistically, multiply all TM factors along each dimension, even though
        // in some cases they may cancel out.
        tm_c_factor[n] == (bcast_c_dim[n] * hslice_dim[n] * hstack_dim[n]);
        tm_r_factor[n] == (bcast_r_dim[n] * vslice_dim[n] * vstack_dim[n]);

        effective_consumer_grid_size_r[n] == op_grid_size_y[n] * tm_r_factor[n];
        effective_consumer_grid_size_c[n] == op_grid_size_x[n] * tm_c_factor[n];

        // If transposed, flip dimensions
        transpose[n] == 1 -> (effective_consumer_grid_size_r_reblock[n] == effective_consumer_grid_size_c[n]) && 
                             (effective_consumer_grid_size_c_reblock[n] == effective_consumer_grid_size_r[n]); 
        transpose[n] == 0 -> (effective_consumer_grid_size_r_reblock[n] == effective_consumer_grid_size_r[n]) &&
                             (effective_consumer_grid_size_c_reblock[n] == effective_consumer_grid_size_c[n]);


        (feeder_grid_size_y % effective_consumer_grid_size_r_reblock[n] == 0) -> reblock_tm_fork_r_factor_int[n] == 1;
        (feeder_grid_size_y % effective_consumer_grid_size_r_reblock[n] > 0) && 
        (effective_consumer_grid_size_r_reblock[n] % feeder_grid_size_y == 0) -> reblock_tm_fork_r_factor_int[n] == effective_consumer_grid_size_r_reblock[n]/feeder_grid_size_y;
        // We must account for cases such as reblocking from dimension 7 -> 6, where
        // producer cores may have a fanout of 2 or 3 because of non-divisibility.
        (feeder_grid_size_y % effective_consumer_grid_size_r_reblock[n] > 0) && 
        (effective_consumer_grid_size_r_reblock[n] % feeder_grid_size_y > 0) -> reblock_tm_fork_r_factor_int[n] == (`ceil(effective_consumer_grid_size_r_reblock[n],feeder_grid_size_y)+1);

        (feeder_grid_size_x % effective_consumer_grid_size_c_reblock[n] == 0) -> reblock_tm_fork_c_factor_int[n] == 1;
        (feeder_grid_size_x % effective_consumer_grid_size_c_reblock[n] > 0) && 
        (effective_consumer_grid_size_c_reblock[n] % feeder_grid_size_x == 0) -> reblock_tm_fork_c_factor_int[n] == effective_consumer_grid_size_c_reblock[n]/feeder_grid_size_x;
        // We must account for cases such as reblocking from dimension 7 -> 6, where
        // producer cores may have a fanout of 2 or 3 because of non-divisibility.
        (feeder_grid_size_x % effective_consumer_grid_size_c_reblock[n] > 0) && 
        (effective_consumer_grid_size_c_reblock[n] % feeder_grid_size_x > 0) -> reblock_tm_fork_c_factor_int[n] == (`ceil(effective_consumer_grid_size_c_reblock[n],feeder_grid_size_x)+1);



        // The final result can't be higher than the total number of consumer cores
        (reblock_tm_fork_r_factor_int[n]*reblock_tm_fork_c_factor_int[n]) >  (op_grid_size_x[n]*op_grid_size_y[n]) -> max_fork_streams_used_per_core[n] == (op_grid_size_x[n]*op_grid_size_y[n] + 1);
        (reblock_tm_fork_r_factor_int[n]*reblock_tm_fork_c_factor_int[n]) <= (op_grid_size_x[n]*op_grid_size_y[n]) -> max_fork_streams_used_per_core[n] == (reblock_tm_fork_r_factor_int[n]*reblock_tm_fork_c_factor_int[n] + 1);

     }

     max_fork_streams_used_per_core.sum() <= `MAX_FORK_STREAMS_PER_CORE;
  }

  constraint rand_t {
     in_t inside {[1:64]};
     foreach (out_t[n]) {
        out_t[n] inside {[1:64]};
     }
  }

  constraint rand_output_block_size {
     foreach (out_mblock_m[n]) {
        out_mblock_m[n] inside {[1:16]};
     }
     foreach (out_mblock_n[n]) {
        out_mblock_n[n] inside {[1:16]};
     }
     foreach (out_ublock_rt[n]) {
        out_ublock_rt[n] inside {[1:8]};
     }
     foreach (out_ublock_ct[n]) {
        out_ublock_ct[n] inside {[1:8]};
        dest_fp32_acc_enable == 0 -> out_ublock_rt[n]*out_ublock_ct[n] <= `MAX_TILES_IN_DEST/2;
        dest_fp32_acc_enable == 1 -> out_ublock_rt[n]*out_ublock_ct[n] <= `MAX_TILES_IN_DEST_FP32/2;
     }
  }

  constraint rand_reblock_dims {
     in_block_rt == mblock_m*ublock_rt*feeder_grid_size_y;
     in_block_ct == mblock_n*ublock_ct*feeder_grid_size_x;
     foreach (out_block_rt[n]) {
        out_block_rt[n] == out_mblock_m[n]*out_ublock_rt[n]*op_grid_size_y[n];
        if (n == 0) {
          transpose[n] == 0 && vstack[n] == 0 && vslice[n] == 0 && bcast[n] == 0 -> out_block_rt[n] == in_block_rt;
          transpose[n] == 1 && vstack[n] == 0 && vslice[n] == 0 && bcast[n] == 0 -> out_block_rt[n] == in_block_ct;
          transpose[n] == 0 && vstack[n] == 1 && vslice[n] == 0 && bcast[n] == 0 -> out_block_rt[n] == in_block_rt*vstack_dim[n];
          transpose[n] == 0 && vstack[n] == 0 && vslice[n] == 1 && bcast[n] == 0 -> out_block_rt[n] == in_block_rt/vslice_dim[n];
          transpose[n] == 0 && vstack[n] == 0 && vslice[n] == 0 && bcast[n] == 1 -> out_block_rt[n] == in_block_rt*bcast_r_dim[n];
        } else {
          out_block_rt[n] == out_block_rt[n-1];
        }   
     }

     foreach (out_block_ct[n]) {
        out_block_ct[n] == out_mblock_n[n]*out_ublock_ct[n]*op_grid_size_x[n];
        if (n == 0) {
          transpose[n] == 0 && hstack[n] == 0 && hslice[n] == 0 && bcast[n] == 0 -> out_block_ct[n] == in_block_ct;
          transpose[n] == 1 && hstack[n] == 0 && hslice[n] == 0 && bcast[n] == 0 -> out_block_ct[n] == in_block_rt;
          transpose[n] == 0 && hstack[n] == 1 && hslice[n] == 0 && bcast[n] == 0 -> out_block_ct[n] == in_block_ct*hstack_dim[n];
          transpose[n] == 0 && hstack[n] == 0 && hslice[n] == 1 && bcast[n] == 0 -> out_block_ct[n] == in_block_ct/hslice_dim[n];
          transpose[n] == 0 && vstack[n] == 0 && vslice[n] == 0 && bcast[n] == 1 -> out_block_ct[n] == in_block_ct*bcast_c_dim[n];
        } else {
          out_block_ct[n] == out_block_ct[n-1];
        }   
     }
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
    foreach (untilize_output[n]) { 
       (out_data_format inside {fp16,fp16_b,fp32}) -> (untilize_output[n] == 1);
       (out_data_format inside {bfp8,bfp8_b}) -> (untilize_output[n] == 0);
    }
  }

  constraint force_dependancy {
    foreach (transpose[n]) { 
       transpose[n] == 1 -> hstack[n] == 0 && vstack[n] == 0 && hslice[n] == 0 && vslice[n] == 0 && bcast[n] == 0;
    }
    foreach (hstack[n]) { 
       hstack[n] == 1 -> transpose[n] == 0 && vstack[n] == 0 && hslice[n] == 0 && vslice[n] == 0 && bcast[n] == 0;
    }
    foreach (vstack[n]) { 
       vstack[n] == 1 -> transpose[n] == 0 && hstack[n] == 0 && hslice[n] == 0 && vslice[n] == 0 && bcast[n] == 0;
    }
    foreach (hslice[n]) { 
       hslice[n] == 1 -> transpose[n] == 0 && hstack[n] == 0 && vstack[n] == 0 && vslice[n] == 0 && bcast[n] == 0;
    }                                                       
    foreach (vslice[n]) {                                  
       vslice[n] == 1 -> transpose[n] == 0 && hstack[n] == 0 && vstack[n] == 0 && hslice[n] == 0 && bcast[n] == 0;
    }
    foreach (bcast[n]) {                                  
       bcast[n] == 1 -> transpose[n] == 0 && hstack[n] == 0 && vstack[n] == 0 && hslice[n] == 0 && vslice[n] == 0;
    }
   
    foreach (out_t[n]) { 
       hstack[n] == 0 && vstack[n] == 0 && hslice[n] == 0 && vslice[n] == 0 && bcast[n] == 0 -> (in_t == out_t[n]);
    }
  }

  constraint rand_transpose {
    foreach (transpose[n]) { 
       transpose[n] inside {[0:1]}; 
       ((unary_type != datacopy)&&(unary_type != nop)) -> (transpose[n] == 0);
    }
  }

  constraint rand_hstack {
    foreach (hstack[n]) { 
       hstack[n] inside {[0:1]}; 
       hstack[n] == 1 -> (hstack_dim[n] == (in_t/out_t[n]));
       hstack[n] == 1 -> ((in_t > out_t[n]) && ((in_t % out_t[n]) == 0));
       hstack[n] == 1 -> (out_block_ct[n] == (in_block_ct*hstack_dim[n]));
       hstack[n] == 1 -> ((in_block_ct  % (out_mblock_n[n]*out_ublock_ct[n])) == 0); //stacking block c size tiles must divisible in either direction with consumer mblock tiles c
       hstack[n] == 0 -> hstack_dim[n] == 1;
    }
  }

  constraint rand_vstack {
    foreach (vstack[n]) { 
       vstack[n] inside {[0:1]}; 
       vstack[n] == 1 -> (vstack_dim[n] == (in_t/out_t[n]));
       vstack[n] == 1 -> ((in_t > out_t[n]) && ((in_t % out_t[n]) == 0));
       vstack[n] == 1 -> (out_block_rt[n] == (in_block_rt*vstack_dim[n]));
       vstack[n] == 1 -> ((in_block_rt  % (out_mblock_m[n]*out_ublock_rt[n])) == 0); //stacking block r size tiles must divisible in either direction with consumer mblock tiles r
       vstack[n] == 0 -> vstack_dim[n] == 1;
    }
  }

  constraint set_in_buf_size_mb {
    foreach (hstack[n]) { 
       hstack[n] == 0 && vstack[n] == 0 && bcast[n] == 0 -> in_buf_size_mb == 2;
       if (n==0) {
          hstack[n] == 1 && vstack[n] == 0 && bcast[n] == 0 -> (in_buf_size_mb == 2*hstack_dim[n]);
       } else {
          ((hstack[n] == 1 && vstack[n] == 0 && bcast[n] == 0) && (2*hstack_dim[n] > in_buf_size_mb))  -> (in_buf_size_mb == 2*hstack_dim[n]);
          ((hstack[n] == 1 && vstack[n] == 0 && bcast[n] == 0) && (2*hstack_dim[n] <= in_buf_size_mb)) -> (in_buf_size_mb == in_buf_size_mb);
       }  
    }

    foreach (vstack[n]) { 
       hstack[n] == 0 && vstack[n] == 0 && bcast[n] == 0 -> in_buf_size_mb == 2;
       if (n==0) {
          hstack[n] == 0 && vstack[n] == 1 && bcast[n] == 0 -> (in_buf_size_mb == 2*vstack_dim[n]);
       } else {
          ((hstack[n] == 0 && vstack[n] == 1 && bcast[n] == 0) && (2*vstack_dim[n] > in_buf_size_mb))  -> (in_buf_size_mb == 2*vstack_dim[n]);
          ((hstack[n] == 0 && vstack[n] == 1 && bcast[n] == 0) && (2*vstack_dim[n] <= in_buf_size_mb)) -> (in_buf_size_mb == in_buf_size_mb);
       }  
    }

    foreach (bcast[n]) { 
       hstack[n] == 0 && vstack[n] == 0 && bcast[n] == 0 -> in_buf_size_mb == 2;
       hstack[n] == 0 && vstack[n] == 0 && bcast[n] == 1 -> (in_buf_size_mb == 2*in_t);
    }
  }

  constraint rand_hslice {
    foreach (hslice[n]) { 
       hslice[n] inside {[0:1]}; 
       hslice[n] == 1 -> (hslice_dim[n] == (out_t[n]/in_t));
       hslice[n] == 1 -> ((out_t[n]>in_t) && ((out_t[n]%in_t) == 0));
       hslice[n] == 1 -> (in_block_ct == (out_block_ct[n]*hslice_dim[n]));
       hslice[n] == 0 -> hslice_dim[n] == 1;
    }
  }

  constraint rand_vslice {
    foreach (vslice[n]) { 
       vslice[n] inside {[0:1]}; 
       vslice[n] == 1 -> (vslice_dim[n] == (out_t[n]/in_t));
       vslice[n] == 1 -> ((out_t[n]>in_t) && ((out_t[n]%in_t) == 0));
       vslice[n] == 1 -> (in_block_rt == (out_block_rt[n]*vslice_dim[n]));
       vslice[n] == 0 -> vslice_dim[n] == 1;

    }
  }

  constraint rand_bcast {
    foreach (bcast[n]) { 
       bcast[n] inside {[0:1]}; 
       bcast[n] == 1 -> bcast_z_dim[n] == out_t[n];
       bcast[n] == 1 -> in_t == 1;
       bcast[n] == 1 -> ((out_t[n]>=in_t) && ((out_t[n]%in_t) == 0));

       bcast[n] == 1 -> bcast_r_dim[n] == out_block_rt[n];
       bcast[n] == 1 -> in_block_rt == 1; //FIXME
       bcast[n] == 1 -> ((out_block_rt[n]>=in_block_rt) && ((out_block_rt[n]%in_block_rt) == 0));
 
       bcast[n] == 1 -> bcast_c_dim[n] == out_block_ct[n];
       bcast[n] == 1 -> in_block_ct == 1; //FIXME
       bcast[n] == 1 -> ((out_block_ct[n]>=in_block_ct) && ((out_block_ct[n]%in_block_ct) == 0));

       bcast[n] == 0 -> bcast_r_dim[n] == 1;
       bcast[n] == 0 -> bcast_c_dim[n] == 1;
       bcast[n] == 0 -> bcast_z_dim[n] == 1;
  
    }
  }
   
  // Make sure buffers can fit into l1
  constraint fit_in_l1 {
     ((2*ublock_rt*ublock_ct*in_tile_size + in_buf_size_mb*mblock_n*mblock_m*ublock_rt*ublock_ct*out_tile_size) <= `MAX_L1_MEM_BUFFER_SIZE);
     foreach (out_mblock_m[n]) {
        ((2*ublock_rt*ublock_ct*in_tile_size + 
          2*out_mblock_n[n]*out_mblock_m[n]*out_ublock_rt[n]*out_ublock_ct[n]*out_tile_size*`ceil(feeder_grid_size_x,op_grid_size_x[n])*`ceil(feeder_grid_size_y,op_grid_size_y[n])) <= `MAX_L1_MEM_BUFFER_SIZE);
     }
  }
  
  constraint rand_dram_channel_fast_tilize {
    foreach (dram_channel[n]) {
       (fast_tilize_input == 1) -> dram_channel[n] == 0;
    }
  }

  rand bit[39:0] in_q_dram_addr;
  rand bit[39:0] out_q_dram_addr[`NUM_FORKS];

  rand bit [39:0] in_q_size;
  rand bit [39:0] out_q_size[`NUM_FORKS];

  // Dram address must be aligned to 32 bytes
  constraint rand_dram_addr {
    in_q_size == (in_t*mblock_m*ublock_rt*mblock_n*ublock_ct*in_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
    in_q_dram_addr[4:0] == 5'b00000; // align to 32B
    in_q_dram_addr inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
    in_q_dram_addr + (in_q_size*feeder_grid_size_x*feeder_grid_size_y) < out_q_dram_addr[0];

    foreach (out_q_dram_addr[n]) {
       out_q_size[n] == (out_t[n]*out_mblock_m[n]*out_ublock_rt[n]*out_mblock_n[n]*out_ublock_ct[n]*out_tile_size*num_entries*num_loops+`DRAM_QUEUE_HEADER_SIZE);
       out_q_dram_addr[n][4:0] == 5'b00000; // align to 32B
       out_q_dram_addr[n] inside {[`FAST_TILIZER_DRAM_BUFFER_START_ADDR:`FAST_TILIZER_DRAM_BUFFER_END_ADDR]};
       if (n<(`NUM_FORKS-1)) {   
          out_q_dram_addr[n] + (out_q_size[n]*op_grid_size_x[n]*op_grid_size_y[n]) <= out_q_dram_addr[n+1];
       } else {
          out_q_dram_addr[n] + (out_q_size[n]*op_grid_size_x[n]*op_grid_size_y[n]) <= (`FAST_TILIZER_DRAM_BUFFER_END_ADDR);
       }
    }
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
     (in_t*mblock_m*mblock_n*ublock_ct*ublock_rt <= 4096);
     // Reduce runtime when host has to convert input to bfp format
     ((in_data_format inside {[bfp8:bfp8_b]}) || (out_data_format inside {[bfp8:bfp8_b]})) -> ((num_inputs inside {[1:4]}) && (in_t inside {[1:2]}) && (mblock_m inside {[1:4]}) && (mblock_n inside {[1:4]}));
  }

endclass

integer num_rand_loops=1;
integer ntb_random_seed=0;
string out_filename;
integer out_filehandle;
bit transpose=0;
bit hstack=0;
bit vstack=0;
bit hslice=0;
bit vslice=0;
bit bcast=0;
bit rand_tm=0;

bit [15:0] in_mblock_m;
bit [15:0] in_mblock_n;
bit [ 7:0] in_ublock_rt;
bit [ 7:0] in_ublock_ct;

bit [15:0] out_mblock_m[`NUM_FORKS];
bit [15:0] out_mblock_n[`NUM_FORKS];
bit [ 7:0] out_ublock_rt[`NUM_FORKS];
bit [ 7:0] out_ublock_ct[`NUM_FORKS];

bit [ 7:0] out_grid_size_x[`NUM_FORKS];
bit [ 7:0] out_grid_size_y[`NUM_FORKS];
bit first;

program generator();
  initial begin
      $value$plusargs("num_rand_loops=%d", num_rand_loops);
      $value$plusargs("ntb_random_seed=%d", ntb_random_seed);
      $value$plusargs("transpose=%d", transpose);
      $value$plusargs("hstack=%d", hstack);
      $value$plusargs("vstack=%d", vstack);
      $value$plusargs("hslice=%d", hslice);
      $value$plusargs("vslice=%d", vslice);
      $value$plusargs("bcast=%d", bcast);
      $value$plusargs("rand=%d", rand_tm);
      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("Out file name not specified!");
      end else begin
         $display("INFO: Writing rand vars to %s", out_filename);
      end
  end

  initial begin
    tm_op_fork_constraints constraints = new();
    test_config test_cfg = new();
    out_filehandle = $fopen(out_filename, "w");


    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);
    for (int i =0; i<num_rand_loops  ; i++) begin
       if (rand_tm) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == rand_tm[n][1];} 
             foreach (hstack[n]) {hstack[n] == rand_tm[n][2];} 
             foreach (vstack[n]) {vstack[n] == rand_tm[n][3];} 
             foreach (hslice[n]) {hslice[n] == rand_tm[n][4];} 
             foreach (vslice[n]) {vslice[n] == rand_tm[n][5];} 
             foreach (bcast[n]) {bcast[n] == rand_tm[n][6];} 
          };
       end else if (transpose) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 1;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {vslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end else if (hstack) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 1;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {hslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end else if (vstack) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 1;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {hslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end else if (hslice) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 1;} 
             foreach (vslice[n]) {vslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end else if (vslice) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {vslice[n] == 1;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end else if (bcast) begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {vslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 1;} 
          };
       end else begin
          constraints.randomize() with {
             foreach (transpose[n]) {transpose[n] == 0;} 
             foreach (hstack[n]) {hstack[n] == 0;} 
             foreach (vstack[n]) {vstack[n] == 0;} 
             foreach (hslice[n]) {hslice[n] == 0;} 
             foreach (vslice[n]) {vslice[n] == 0;} 
             foreach (bcast[n]) {bcast[n] == 0;} 
          };
       end
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
       for (int out=0;out<`NUM_FORKS;out++) begin
          if (constraints.untilize_output[out]) begin
             out_mblock_m[out] = constraints.out_mblock_m[out]*constraints.op_grid_size_y[out]*constraints.out_ublock_rt[out];
             out_mblock_n[out] = constraints.out_mblock_n[out]*constraints.op_grid_size_x[out]*constraints.out_ublock_ct[out];
             out_ublock_rt[out] = 1;
             out_ublock_ct[out] = 1;
             out_grid_size_x[out] = 1;
             out_grid_size_y[out] = 1;
          end else begin
             out_mblock_m[out]    = constraints.out_mblock_m[out];
             out_mblock_n[out]    = constraints.out_mblock_n[out];
             out_ublock_rt[out]   = constraints.out_ublock_rt[out];
             out_ublock_ct[out]   = constraints.out_ublock_ct[out];
             out_grid_size_x[out] = constraints.op_grid_size_x[out];
             out_grid_size_y[out] = constraints.op_grid_size_y[out];
          end
       end
       // Random test inputs
       $fwrite(out_filehandle, "#Test id=%0d\n", i);
       $fwrite(out_filehandle, "- device: %s\n", `DEVICE);
       $fwrite(out_filehandle, "  entries: %0d\n", constraints.num_entries*constraints.num_loops);
       for (int out=0;out<`NUM_FORKS;out++) begin
          $fwrite(out_filehandle, "  op%0d_loc_c: %0d\n", out, constraints.op_grid_loc_x[out]);
          $fwrite(out_filehandle, "  op%0d_loc_r: %0d\n", out, constraints.op_grid_loc_y[out]);
       end
       $fwrite(out_filehandle, "  input_t: %0d\n", constraints.in_t);
       $fwrite(out_filehandle, "  input_mb_r: %0d\n", in_mblock_m);
       $fwrite(out_filehandle, "  input_mb_c: %0d\n", in_mblock_n);
       $fwrite(out_filehandle, "  input_ub_r: %0d\n", in_ublock_rt);
       $fwrite(out_filehandle, "  input_ub_c: %0d\n", in_ublock_ct);
       $fwrite(out_filehandle, "  input_data_format: '%s'\n", get_data_format(constraints.in_data_format));
       $fwrite(out_filehandle, "  out_data_format: '%s'\n", get_data_format(constraints.out_data_format));
       $fwrite(out_filehandle, "  input_dram_buffers: '[");
       for (int core=0;core<constraints.feeder_grid_size_x*constraints.feeder_grid_size_y; core++) begin
          $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.in_q_dram_addr + core*constraints.in_q_size);
          if (core<constraints.feeder_grid_size_x*constraints.feeder_grid_size_y-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");
       $fwrite(out_filehandle, "  target_device: %0d\n", constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n", constraints.num_inputs);
       $fwrite(out_filehandle, "  unary_type: %s\n", get_unary_type(constraints.unary_type));
       $fwrite(out_filehandle, "  input_grid_size_c: %0d\n", constraints.feeder_grid_size_x);
       $fwrite(out_filehandle, "  input_grid_size_r: %0d\n", constraints.feeder_grid_size_y);
       $fwrite(out_filehandle, "  feeder_loc_c: %0d\n", constraints.feeder_grid_loc_x);
       $fwrite(out_filehandle, "  feeder_loc_r: %0d\n", constraints.feeder_grid_loc_y);
       $fwrite(out_filehandle, "  math_fidelity: %s\n", get_math_fidelity(constraints.math_fidelity));
       for (int out=0;out<`NUM_FORKS;out++) begin
          $fwrite(out_filehandle, "  untilize_output%0d: %s\n", out, constraints.untilize_output[out] ? "'true'" : "'false'");
       end
       for (int out=0;out<`NUM_FORKS;out++) begin
          if (constraints.transpose[out]) begin
             $fwrite(out_filehandle, "  input%0d_tms: %s\n", out, "[transpose]");
          end else if (constraints.hstack[out]) begin
             $fwrite(out_filehandle, "  input%0d_tms: '[hstack: %0d]'\n", out, constraints.hstack_dim[out]);
          end else if (constraints.vstack[out]) begin
             $fwrite(out_filehandle, "  input%0d_tms: '[vstack: %0d]'\n", out, constraints.vstack_dim[out]);
          end else if (constraints.hslice[out]) begin
             $fwrite(out_filehandle, "  input%0d_tms: '[hslice: %0d]'\n", out, constraints.hslice_dim[out]);
          end else if (constraints.vslice[out]) begin
             $fwrite(out_filehandle, "  input%0d_tms: '[vslice: %0d]'\n", out, constraints.vslice_dim[out]);
          end else if (constraints.bcast[out]) begin
             first = 1;
             $fwrite(out_filehandle, "  input%0d_tms: '[", out);
             if (constraints.bcast_z_dim[out]>1) begin
                $fwrite(out_filehandle, "broadcast: {z: %0d}", constraints.bcast_z_dim[out]);
                first = 0;
             end
             if (constraints.bcast_r_dim[out]>1) begin
                $fwrite(out_filehandle, "%sbroadcast: {r: %0d}", first==1 ? "" : ", ", constraints.bcast_r_dim[out]);
                first = 0;
             end
             if (constraints.bcast_c_dim[out]>1) begin
                $fwrite(out_filehandle, "%sbroadcast: {c: %0d}", first==1 ? "" : ", ", constraints.bcast_c_dim[out]);
                first = 0;
             end
             $fwrite(out_filehandle, "]'\n");
          end else begin
             $fwrite(out_filehandle, "  input%0d_tms: \n", out);
          end
       end
       $fwrite(out_filehandle, "  ublock_order: r\n");
       $fwrite(out_filehandle, "  buf_size_mb: %0d\n", constraints.in_buf_size_mb);
       $fwrite(out_filehandle, "  # Scale output queue grid and mblock size if output is untilized\n");
       $fwrite(out_filehandle, "  # Net2pipe requires grid size 1x1 if untilized output is written to the queue\n");
       for (int out=0;out<`NUM_FORKS;out++) begin
          if (constraints.untilize_output[out]) begin
             $fwrite(out_filehandle, "  output%0d_dram_buffers: '[[%0d, 0x%0x]]'\n", out, constraints.dram_channel[0], constraints.out_q_dram_addr[out]);
          end else begin  
             $fwrite(out_filehandle, "  output%0d_dram_buffers: '[",out);
             for (int core=0;core<constraints.op_grid_size_x[out]*constraints.op_grid_size_y[out]; core++) begin
                $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.out_q_dram_addr[out] + core*constraints.out_q_size[out]);
                if (core<constraints.op_grid_size_x[out]*constraints.op_grid_size_y[out]-1) $fwrite(out_filehandle, ", ");
             end
             $fwrite(out_filehandle, "]'\n");
          end   
       end
       for (int out=0;out<`NUM_FORKS;out++) begin
          // Dram output queues
          $fwrite(out_filehandle, "  output%0d_grid_size_c: %0d\n", out, out_grid_size_x[out]);
          $fwrite(out_filehandle, "  output%0d_grid_size_r: %0d\n", out, out_grid_size_y[out]);
          $fwrite(out_filehandle, "  output%0d_t: %0d\n", out, constraints.out_t[out]);
          $fwrite(out_filehandle, "  output%0d_mb_r: %0d\n", out, out_mblock_m[out]);
          $fwrite(out_filehandle, "  output%0d_mb_c: %0d\n", out, out_mblock_n[out]);
          $fwrite(out_filehandle, "  output%0d_ub_r: %0d\n", out, out_ublock_rt[out]);
          $fwrite(out_filehandle, "  output%0d_ub_c: %0d\n", out, out_ublock_ct[out]);

          // OPs
          $fwrite(out_filehandle, "  op%0d_grid_size_c: %0d\n", out, constraints.op_grid_size_x[out]);
          $fwrite(out_filehandle, "  op%0d_grid_size_r: %0d\n", out, constraints.op_grid_size_y[out]);
          $fwrite(out_filehandle, "  op%0d_t: %0d\n", out, constraints.out_t[out]);
          $fwrite(out_filehandle, "  op%0d_mb_r: %0d\n", out, constraints.out_mblock_m[out]);
          $fwrite(out_filehandle, "  op%0d_mb_c: %0d\n", out, constraints.out_mblock_n[out]);
          $fwrite(out_filehandle, "  op%0d_ub_r: %0d\n", out, constraints.out_ublock_rt[out]);
          $fwrite(out_filehandle, "  op%0d_ub_c: %0d\n", out, constraints.out_ublock_ct[out]);
       end
       $fwrite(out_filehandle, "  # Program vars\n");
       $fwrite(out_filehandle, "  loop_count: %0d\n", constraints.num_loops);
       $fwrite(out_filehandle, "  queue_wrap_size: %0d\n", 2*constraints.num_entries*constraints.num_loops);
       $fwrite(out_filehandle, "  # Test and stimulus config\n");
       test_cfg.write_unary_comparison_config(out_filehandle, constraints.out_data_format, constraints.unary_type);
       test_cfg.write_unary_stimulus_config(out_filehandle, constraints.unary_type, constraints.in_data_format, constraints.out_data_format);
       $fwrite(out_filehandle, "#===== Used Resources ====\n");
       $fwrite(out_filehandle, "# max_fork_streams_used_per_core=%0d\n",  constraints.max_fork_streams_used_per_core.sum());
       for (int out=0;out<`NUM_FORKS;out++) begin
          $fwrite(out_filehandle, "# tm_c_factor[%0d]=%0d\n", out, constraints.tm_c_factor[out]);
          $fwrite(out_filehandle, "# tm_r_factor[%0d]=%0d\n", out, constraints.tm_r_factor[out]); 
          $fwrite(out_filehandle, "# effective_consumer_grid_size_r[%0d]=%0d\n", out, constraints.effective_consumer_grid_size_r[out]);
          $fwrite(out_filehandle, "# effective_consumer_grid_size_c[%0d]=%0d\n", out, constraints.effective_consumer_grid_size_c[out]);
          $fwrite(out_filehandle, "# effective_consumer_grid_size_r_reblock[%0d]=%0d\n", out, constraints.effective_consumer_grid_size_r_reblock[out]);
          $fwrite(out_filehandle, "# effective_consumer_grid_size_c_reblock[%0d]=%0d\n", out, constraints.effective_consumer_grid_size_c_reblock[out]);
          $fwrite(out_filehandle, "# reblock_tm_fork_r_factor_int[%0d]=%0d\n", out, constraints.reblock_tm_fork_r_factor_int[out]);
          $fwrite(out_filehandle, "# reblock_tm_fork_c_factor_int[%0d]=%0d\n", out, constraints.reblock_tm_fork_c_factor_int[out]);
       end
    end
    $fclose(out_filehandle);
  end

endprogram
