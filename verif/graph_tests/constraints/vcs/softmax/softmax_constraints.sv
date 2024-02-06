`include "global_constraints.sv"
//`include "test_config.sv"

`define MAX_ACT_R                (1024)
`define MAX_ACT_C                (1024)
`define MAX_ACT_TILES_R          ((`MAX_ACT_R + (`TILE_R-1)) / `TILE_R) // 32 tiles
`define MAX_ACT_TILES_C          ((`MAX_ACT_C + (`TILE_R-1)) / `TILE_R) // 32 tiles


class softmax_constraints extends global_constraints;

    //softmax act PyBuda parameter shape
    rand integer unsigned pybuda_act_w, pybuda_act_z, pybuda_act_r, pybuda_act_c;

    //softmax dim PyBuda parameter value
    rand integer          pybuda_dim;

    //input activation queue size, shape, grid size, and dram buffers
    rand integer unsigned inp_act_mblock_m, inp_act_mblock_n, inp_act_ublock_rt, inp_act_ublock_ct;
    rand integer unsigned inp_act_grid_size_y, inp_act_grid_size_x;
    rand integer unsigned inp_act_dram_buffer_size;
    rand integer unsigned inp_act_dram_buffer_addr;
    rand integer unsigned inp_act_tile_size;

    //input constant queue size, shape, grid size, and dram buffers
    rand integer unsigned inp_con_mblock_m, inp_con_mblock_n, inp_con_ublock_rt, inp_con_ublock_ct;
    rand integer unsigned inp_con_grid_size_y, inp_con_grid_size_x;
    rand integer unsigned inp_con_dram_buffer_size;
    rand integer unsigned inp_con_dram_buffer_addr;
    rand integer unsigned inp_con_tile_size;
    rand bit              inp_con_prologue;
    rand integer unsigned inp_con_entries;

    //softmax output queue size, shape, grid size, and host buffers
    rand integer unsigned out_mblock_m, out_mblock_n, out_ublock_rt, out_ublock_ct;
    rand integer unsigned out_grid_size_y, out_grid_size_x;
    rand integer unsigned out_dram_buffer_size;
    rand integer unsigned out_dram_buffer_addr;
    rand integer unsigned out_tile_size;
    rand e_buffer_loc     out_buffer_loc;

    //exponent op output size and shape
    rand integer unsigned exp_mblock_m, exp_mblock_n, exp_ublock_rt, exp_ublock_ct;
    rand integer unsigned exp_grid_size_y, exp_grid_size_x;
    rand integer unsigned exp_grid_loc_y, exp_grid_loc_x;
    rand integer unsigned exp_in_tile_size, exp_out_tile_size;

    //matmul op output size and shape
    rand integer unsigned sum_mblock_m, sum_mblock_n, sum_ublock_rt, sum_ublock_ct;
    rand integer unsigned sum_m_k, sum_u_kt;
    rand integer unsigned sum_in0_broadcast; //1 - broadcast not needed, >1 - broadcast of columns needed
    rand integer unsigned sum_in1_broadcast; //1 - broadcast not needed, >1 - broadcast of rows needed
    rand integer unsigned sum_grid_size_y, sum_grid_size_x;
    rand integer unsigned sum_grid_loc_y, sum_grid_loc_x;
    rand integer unsigned sum_in_tile_size[2], sum_out_tile_size;

    //reciprocal op output size and shape
    rand integer unsigned rec_mblock_m, rec_mblock_n, rec_ublock_rt, rec_ublock_ct;
    rand integer unsigned rec_grid_size_y, rec_grid_size_x;
    rand integer unsigned rec_grid_loc_y, rec_grid_loc_x;
    rand integer unsigned rec_in_tile_size, rec_out_tile_size;

    //multiply op output size and shape
    rand integer unsigned mul_mblock_m, mul_mblock_n, mul_ublock_rt, mul_ublock_ct;
    rand integer unsigned mul_row_broadcast; //1 - broadcast not needed, >1 - broadcast amount of rows needed on in1
    rand integer unsigned mul_col_broadcast; //1 - broadcast not needed, >1 - broadcast amount of columns needed on in1
    rand integer unsigned mul_grid_size_y, mul_grid_size_x;
    rand integer unsigned mul_grid_loc_y, mul_grid_loc_x;
    rand integer unsigned mul_in_tile_size[2], mul_out_tile_size;
    rand bit              mul_untilize_output;


    //range of values for PyBuda softmax parameters
    constraint pybuda_parameters_range {
       //softmax act PyBuda parameter (values in datums)
       pybuda_act_w inside {[1:1]}; //TODO: increase range of valid values.
       pybuda_act_z == t;
       pybuda_act_r inside {[1:`MAX_ACT_R]};
       pybuda_act_c inside {[1:`MAX_ACT_C]};

       //softmax dim PyBuda parameter
       //dim = -1 : softmax is calculated along column dimension, i.e. a row of input tensor is a softmax input vector
       //dim = -2 : softmax is calculated along row dimension, i.e. a column of input tensor is a softmax input vector
       pybuda_dim inside {-1, -2};
    }

    //distribution of dim values
    constraint pybuda_dim_distribution {
       pybuda_dim dist {-1:=50, -2:=50};
    }

    //TODO: temporary constraint until add->rec mismatch is resolved
    constraint pybuda_dim_and_act_relation {
      (pybuda_dim == -1) -> (pybuda_act_c > 1);
      (pybuda_dim == -2) -> (pybuda_act_r > 1);
    }

    //Distribution of untilize flag of mul operation
    constraint mul_untilize_output_distribution {
       mul_untilize_output dist {0:=50, 1:=50};

       (mul_untilize_output == 1) -> (data_format inside {fp16, fp16_b, fp32});
       (mul_untilize_output == 0) -> (data_format inside {bfp8, bfp8_b});
    }

    //range of values of inp_con queue prologue and entries
    constraint inp_con_prologue_and_entries {
       inp_con_prologue dist {0:=20, 1:=80};

       //inp_con queue that have prologue=false, can have entries equal to entries of input_activation queue
       (inp_con_prologue == 0) -> (inp_con_entries == num_entries);

       //inp_con queue that have prologue=true, must have entries=1
       (inp_con_prologue == 1) -> (inp_con_entries == 1);
    }

    //range of values for macro- and micro-block dimensions for each operation
    constraint max_block_size {
       inp_act_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       inp_act_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       inp_act_ublock_rt inside {[1:16]};
       inp_act_ublock_ct inside {[1:16]};

       inp_con_mblock_m  inside {[1:1]};
       inp_con_mblock_n  inside {[1:1]};
       inp_con_ublock_rt inside {[1:1]};
       inp_con_ublock_ct inside {[1:1]};

       out_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       out_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       out_ublock_rt inside {[1:16]};
       out_ublock_ct inside {[1:16]};

       exp_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       exp_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       exp_ublock_rt inside {[1:16]};
       exp_ublock_ct inside {[1:16]};

       sum_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       sum_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       sum_ublock_rt inside {[1:16]};
       sum_ublock_ct inside {[1:16]};
       sum_m_k       inside {[1:16]};
       sum_u_kt      inside {[1:16]};


       rec_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       rec_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       rec_ublock_rt inside {[1:16]};
       rec_ublock_ct inside {[1:16]}; //TODO: MAX_TILES_IN_DEST instead of 16

       mul_mblock_m  inside {[1:`MAX_ACT_TILES_R]};
       mul_mblock_n  inside {[1:`MAX_ACT_TILES_C]};
       mul_ublock_rt inside {[1:16]};
       mul_ublock_ct inside {[1:16]};
    }

    constraint ublock_size {
       if (dest_fp32_acc_enable == 0) {
         //micro blocks can be up to a half of DEST register file to allow for double buffering
         exp_ublock_rt*exp_ublock_ct <= `MAX_TILES_IN_DEST/2;
         sum_ublock_rt*sum_ublock_ct <= `MAX_TILES_IN_DEST/2;
         rec_ublock_rt*rec_ublock_ct <= `MAX_TILES_IN_DEST/2;
         mul_ublock_rt*mul_ublock_ct <= `MAX_TILES_IN_DEST/2;
       } else {
         //micro blocks can be up to a half of DEST register file to allow for double buffering
         exp_ublock_rt*exp_ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
         sum_ublock_rt*sum_ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
         rec_ublock_rt*rec_ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
         mul_ublock_rt*mul_ublock_ct <= `MAX_TILES_IN_DEST_FP32/2;
       }
    }

    constraint grid_size_and_loc {
//     inp_act_grid_size_y inside {[1:`GRID_SIZE_Y]};
//     inp_act_grid_size_x inside {[1:`GRID_SIZE_X]};
       inp_con_grid_size_y inside {[1:1]};
       inp_con_grid_size_x inside {[1:1]};
//     out_grid_size_y     inside {[1:`GRID_SIZE_Y]};
//     out_grid_size_x     inside {[1:`GRID_SIZE_X]};

       //grid size of inp_act tensor and the first operation (exp) in pipe have to match
       //TODO: Do we need this constraint?
       inp_act_grid_size_y == exp_grid_size_y;
       inp_act_grid_size_x == exp_grid_size_x;

       //grid size of out tensor and the last operation (mul) in pipe have to match
       out_grid_size_y == mul_grid_size_y;
       out_grid_size_x == mul_grid_size_x;

       //all grids sizes need to fit the chip grid
       exp_grid_size_y inside {[1:`GRID_SIZE_Y]};
       exp_grid_size_x inside {[1:`GRID_SIZE_X]};
       sum_grid_size_y inside {[1:`GRID_SIZE_Y]};
       sum_grid_size_x inside {[1:`GRID_SIZE_X]};
       rec_grid_size_y inside {[1:`GRID_SIZE_Y]};
       rec_grid_size_x inside {[1:`GRID_SIZE_X]};
       mul_grid_size_y inside {[1:`GRID_SIZE_Y]};
       mul_grid_size_x inside {[1:`GRID_SIZE_X]};

       //top left point of all grids need to be within the chip grid
       exp_grid_loc_y inside {[0:`GRID_SIZE_Y-1]};
       exp_grid_loc_x inside {[0:`GRID_SIZE_X-1]};
       sum_grid_loc_y inside {[0:`GRID_SIZE_Y-1]};
       sum_grid_loc_x inside {[0:`GRID_SIZE_X-1]};
       rec_grid_loc_y inside {[0:`GRID_SIZE_Y-1]};
       rec_grid_loc_x inside {[0:`GRID_SIZE_X-1]};
       mul_grid_loc_y inside {[0:`GRID_SIZE_Y-1]};
       mul_grid_loc_x inside {[0:`GRID_SIZE_X-1]};

       //bottom right of all grids need to be within the chip grid
       exp_grid_loc_y + exp_grid_size_y <= `GRID_SIZE_Y;
       exp_grid_loc_x + exp_grid_size_x <= `GRID_SIZE_X;
       sum_grid_loc_y + sum_grid_size_y <= `GRID_SIZE_Y;
       sum_grid_loc_x + sum_grid_size_x <= `GRID_SIZE_X;
       rec_grid_loc_y + rec_grid_size_y <= `GRID_SIZE_Y;
       rec_grid_loc_x + rec_grid_size_x <= `GRID_SIZE_X;
       mul_grid_loc_y + mul_grid_size_y <= `GRID_SIZE_Y;
       mul_grid_loc_x + mul_grid_size_x <= `GRID_SIZE_X;
    }

    constraint block_dims_relations {
       //TODO: pybuda_act_w relations to netlist parameters

       //input activation queue has to be the same size and shape as act parameter of PyBuda softmax
       (inp_act_grid_size_y*inp_act_mblock_m*inp_act_ublock_rt) == ((pybuda_act_r + (`TILE_R-1)) / `TILE_R);
       (inp_act_grid_size_x*inp_act_mblock_n*inp_act_ublock_ct) == ((pybuda_act_c + (`TILE_C-1)) / `TILE_C);

       //out: softmax output have to be the same size and shape as input activation queue
       (out_grid_size_y*out_mblock_m*out_ublock_rt) == (inp_act_grid_size_y*inp_act_mblock_m*inp_act_ublock_rt);
       (out_grid_size_x*out_mblock_n*out_ublock_ct) == (inp_act_grid_size_x*inp_act_mblock_n*inp_act_ublock_ct);

       //input: input constant queue in context of softmax network has to be smaller or equal to input activation queue
       (inp_con_grid_size_y*inp_con_mblock_m*inp_con_ublock_rt) <= (inp_act_grid_size_y*inp_act_mblock_m*inp_act_ublock_rt);
       (inp_con_grid_size_x*inp_con_mblock_n*inp_con_ublock_ct) <= (inp_act_grid_size_x*inp_act_mblock_n*inp_act_ublock_ct);

       //exp: exp op output has to be the same size and shape as input activation queue
       (exp_grid_size_y*exp_mblock_m*exp_ublock_rt) == (inp_act_grid_size_y*inp_act_mblock_m*inp_act_ublock_rt);
       (exp_grid_size_x*exp_mblock_n*exp_ublock_ct) == (inp_act_grid_size_x*inp_act_mblock_n*inp_act_ublock_ct);

       //matmul: matmul output size and shape relations to exp op output and input constant queue
       if (pybuda_dim == -1) {

         //matmul's 1st input: exp op output
         //matmul's 2nd input: input constant queue

         //number of rows of matmul's output has to be equal to the number of rows of its 1st input - exp op output
         (sum_grid_size_y*sum_mblock_m*sum_ublock_rt) == (exp_grid_size_y*exp_mblock_m*exp_ublock_rt);

         //number of columns of matmul's output has to be equal to the number of columns of its 2nd input - input constant queue
         (sum_grid_size_x*sum_mblock_n*sum_ublock_ct) == (inp_con_grid_size_x*inp_con_mblock_n*inp_con_ublock_ct);

         //common dimension of matmul's inputs has to be equal
         //  - columns of 1st input (exp op output)
         //  - rows of 2nd input (input constant queue with a broadcast of rows by sum_in1_broadcast) and
         //example: input_1_tms: [broadcast: {r: 2}]
         (inp_con_grid_size_y*inp_con_mblock_m*inp_con_ublock_rt*sum_in1_broadcast) == (exp_grid_size_x*exp_mblock_n*exp_ublock_ct);

         //broadcast on matmul's 1st input is not needed in this case
         sum_in0_broadcast == 1;

         //total iterations of matmul's outer and inner loops has to match column dimension of exp op output as matmul's 1st input
         (sum_m_k*sum_u_kt) == exp_grid_size_x*exp_mblock_n*exp_ublock_ct;

         //row broadcast of multiply's 2nd input is not needed in this case
         mul_row_broadcast == 1;

       } else {
         //matmul's 1st input: input constant queue
         //matmul's 2nd input: exp op output

         //number of rows of matmul's output has to be equal to the number of rows of its 1st input - input constant queue
         (sum_grid_size_y*sum_mblock_m*sum_ublock_rt) == (inp_con_grid_size_y*inp_con_mblock_m*inp_con_ublock_rt);

         //number of columns of matmul's output has to be equal to the number of columns of its 2nd input - exp op output
         (sum_grid_size_x*sum_mblock_n*sum_ublock_ct) == (exp_grid_size_x*exp_mblock_n*exp_ublock_ct);

         //common dimension of matmul's inputs has to be equal
         //  - columns of 1st input (input constant queue with a broadcast of columns by sum_in0_broadcast) and
         //  - rows of 2nd input (exp op output)
         //example: input_0_tms: [broadcast: {c: 2}]
         (inp_con_grid_size_x*inp_con_mblock_n*inp_con_ublock_ct*sum_in0_broadcast) == (exp_grid_size_y*exp_mblock_m*exp_ublock_rt);

         //broadcast on matmul's 2nd input is not needed in this case
         sum_in1_broadcast == 1;

         //total iterations of matmul's outer and inner loops has to match row dimension of exp op output as matmul's 2nd input
         (sum_m_k*sum_u_kt) == exp_grid_size_y*exp_mblock_m*exp_ublock_rt;

         //column broadcast of multiply's 2nd input is not needed in this case
         mul_col_broadcast == 1;
       }

       //reciprocal: rec output has to be the same size and shape as matmul output
       (rec_grid_size_y*rec_mblock_m*rec_ublock_rt) == (sum_grid_size_y*sum_mblock_m*sum_ublock_rt);
       (rec_grid_size_x*rec_mblock_n*rec_ublock_ct) == (sum_grid_size_x*sum_mblock_n*sum_ublock_ct);

       //multiply: mul output size and shape relation to the softmax output queue
       if (mul_untilize_output == 1) {
          //If tensor written to a DRAM queue is untilized, then the queue must be in canonical form,
          //i.e. grid size and ublock [1,1] and mblock upscaled to match the mul OP output
          out_mblock_m == (mul_grid_size_y*mul_mblock_m*mul_ublock_rt);
          out_mblock_n == (mul_grid_size_x*mul_mblock_n*mul_ublock_ct);
          out_ublock_rt == 1;
          out_ublock_ct == 1;
          out_grid_size_x == 1;
          out_grid_size_y == 1;
       } else {
          //No reblocking/tms supported on write to DRAM queue, hence dimensions have to match
          out_mblock_m == mul_mblock_m;
          out_mblock_n == mul_mblock_n;
          out_ublock_rt == mul_ublock_rt;
          out_ublock_ct == mul_ublock_ct;
          out_grid_size_x == mul_grid_size_x;
          out_grid_size_y == mul_grid_size_y;
       }

       //multiply: mul output has to be the same size and shape as its 1st input: exp op output
       (mul_grid_size_y*mul_mblock_m*mul_ublock_rt) == (exp_grid_size_y*exp_mblock_m*exp_ublock_rt);
       (mul_grid_size_x*mul_mblock_n*mul_ublock_ct) == (exp_grid_size_x*exp_mblock_n*exp_ublock_ct);

       //multiply: size and shape of mul's 2nd input (rec op output broadcasted by rows or columns) has to match 1st input (exp op output)
       // example for dim = -1 => input_1_tms: [broadcast: {c: 2}]
       // example for dim = -2 => input_1_tms: [broadcast: {r: 2}]
       (rec_grid_size_y*rec_mblock_m*rec_ublock_rt*mul_row_broadcast) == (exp_grid_size_y*exp_mblock_m*exp_ublock_rt);
       (rec_grid_size_x*rec_mblock_n*rec_ublock_ct*mul_col_broadcast) == (exp_grid_size_x*exp_mblock_n*exp_ublock_ct);
    }


    //all buffers must fit into L1
    constraint fit_in_l1 {
       //exp: 2 is for double buffering, one input ublock + one output mblock have to fit in L1
       (2*exp_ublock_rt*exp_ublock_ct*exp_in_tile_size + 2*exp_mblock_n*exp_mblock_m*exp_ublock_rt*exp_ublock_ct*exp_out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE); // since output is not untilized we need to buffer entire block

       //matmul: 2 is for double buffering, one ublock column of 1st input + one ublock row of 2nd input, and one whole output tensor (r*c) have to fit in L1
       (2*sum_ublock_rt*sum_u_kt*sum_mblock_m*sum_in_tile_size[0] + 2*sum_u_kt*sum_ublock_ct*sum_mblock_n*sum_in_tile_size[1] + 2*sum_mblock_n*sum_mblock_m*sum_ublock_rt*sum_ublock_ct*sum_out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE);

       //recip: 2 is for double buffering, one input ublock + one output mblock have to fit in L1
       (2*rec_ublock_rt*rec_ublock_ct*rec_in_tile_size + 2*rec_mblock_n*rec_mblock_m*rec_ublock_rt*rec_ublock_ct*rec_out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE); // since output is not untilized we need to buffer entire block

       //multiply: 2 is for double buffering, one ublock of each input + 1 output mblock have to fit in L1
       // if output is untilized we need to double buffer only row of ublocks
       (mul_untilize_output == 1) -> ((2*mul_ublock_rt*mul_ublock_ct*mul_in_tile_size[0] + 2*mul_ublock_rt*mul_ublock_ct*mul_in_tile_size[1] + 2*mul_mblock_n*           1*mul_ublock_rt*mul_ublock_ct*mul_out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE));
       // if output is not untilized we need to buffer entire block
       (mul_untilize_output == 0) -> ((2*mul_ublock_rt*mul_ublock_ct*mul_in_tile_size[0] + 2*mul_ublock_rt*mul_ublock_ct*mul_in_tile_size[1] + 2*mul_mblock_n*mul_mblock_m*mul_ublock_rt*mul_ublock_ct*mul_out_tile_size) <= (`MAX_L1_MEM_BUFFER_SIZE));
    }


    constraint non_overlapping_grid_shapes {
       //grid shapes must not overlap. exp op grid shape is on arbitrary position
       exp_grid_loc_y + exp_grid_size_y <= `GRID_SIZE_Y;
       exp_grid_loc_x + exp_grid_size_x <= `GRID_SIZE_X;

       //sum grid must not overlap with exp grid
       ((sum_grid_loc_x + sum_grid_size_x > exp_grid_loc_x) || (sum_grid_loc_x < exp_grid_loc_x + exp_grid_size_x)) -> ((sum_grid_loc_y + sum_grid_size_y <= exp_grid_loc_y) || (sum_grid_loc_y >= exp_grid_loc_y + exp_grid_size_y));

       //rec grid must not overlap with exp and sum grids
       ((rec_grid_loc_x + rec_grid_size_x > exp_grid_loc_x) || (rec_grid_loc_x < exp_grid_loc_x + exp_grid_size_x)) -> ((rec_grid_loc_y + rec_grid_size_y <= exp_grid_loc_y) || (rec_grid_loc_y >= exp_grid_loc_y + exp_grid_size_y));
       ((rec_grid_loc_x + rec_grid_size_x > sum_grid_loc_x) || (rec_grid_loc_x < sum_grid_loc_x + sum_grid_size_x)) -> ((rec_grid_loc_y + rec_grid_size_y <= sum_grid_loc_y) || (rec_grid_loc_y >= sum_grid_loc_y + sum_grid_size_y));

       //mul grid must not overlap with exp, sum, and rec grids
       ((mul_grid_loc_x + mul_grid_size_x > exp_grid_loc_x) || (mul_grid_loc_x < exp_grid_loc_x + exp_grid_size_x)) -> ((mul_grid_loc_y + mul_grid_size_y <= exp_grid_loc_y) || (mul_grid_loc_y >= exp_grid_loc_y + exp_grid_size_y));
       ((mul_grid_loc_x + mul_grid_size_x > sum_grid_loc_x) || (mul_grid_loc_x < sum_grid_loc_x + sum_grid_size_x)) -> ((mul_grid_loc_y + mul_grid_size_y <= sum_grid_loc_y) || (mul_grid_loc_y >= sum_grid_loc_y + sum_grid_size_y));
       ((mul_grid_loc_x + mul_grid_size_x > rec_grid_loc_x) || (mul_grid_loc_x < rec_grid_loc_x + rec_grid_size_x)) -> ((mul_grid_loc_y + mul_grid_size_y <= rec_grid_loc_y) || (mul_grid_loc_y >= rec_grid_loc_y + rec_grid_size_y));
    }


    //location of output queue
    constraint output_buffer_location {
      out_buffer_loc dist {dram:=50, host:=50};

      //Output tensor can be untilized only in dram
      (mul_untilize_output == 0) -> (out_buffer_loc==dram);
    }


    //dram buffers allocation for the whole grid
    constraint dram_buffers {
       //dram buffer size (per core in the grid)
       inp_act_dram_buffer_size == (t*inp_act_mblock_m*inp_act_ublock_rt*inp_act_mblock_n*inp_act_ublock_ct*    num_entries*inp_act_tile_size + `DRAM_QUEUE_HEADER_SIZE);
       inp_con_dram_buffer_size == (t*inp_con_mblock_m*inp_con_ublock_rt*inp_con_mblock_n*inp_con_ublock_ct*inp_con_entries*inp_con_tile_size + `DRAM_QUEUE_HEADER_SIZE);

       //dram addresses must be aligned to 32B
       inp_act_dram_buffer_addr[4:0] == 5'b00000;
       inp_con_dram_buffer_addr[4:0] == 5'b00000;

       //start address + size of buffers for all cores in the grid must be within the valid address range and non-overlapping
       inp_act_dram_buffer_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       inp_act_dram_buffer_addr + inp_act_grid_size_y*inp_act_grid_size_x*inp_act_dram_buffer_size == inp_con_dram_buffer_addr;

       inp_con_dram_buffer_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
       (out_buffer_loc == host) -> (inp_con_dram_buffer_addr + inp_con_grid_size_y*inp_con_grid_size_x*inp_con_dram_buffer_size <= `DRAM_BUFFER_END_ADDR);
       (out_buffer_loc == dram) -> (inp_con_dram_buffer_addr + inp_con_grid_size_y*inp_con_grid_size_x*inp_con_dram_buffer_size == out_dram_buffer_addr);


       //if out_buffer is in the host's dram memory
       if (out_buffer_loc == host) {
         //host buffer size (per core in the grid)
         out_dram_buffer_size == (t*out_mblock_m*out_ublock_rt*out_mblock_n*out_ublock_ct*num_entries*out_tile_size);

         //host dram addresses must be aligned to 32B
         out_dram_buffer_addr[4:0] == 5'b00000;

         //host start address + size of buffers for all cores in the grid must be within the valid address range
         out_dram_buffer_addr inside {[`HOST_BUFFER_START_ADDR:`HOST_BUFFER_END_ADDR]};
         out_dram_buffer_addr + out_grid_size_y*out_grid_size_x*out_dram_buffer_size <= `HOST_BUFFER_END_ADDR;

       //if out_buffer is in the device's dram memory
       } else {
         //host buffer size (per core in the grid)
         out_dram_buffer_size == (t*out_mblock_m*out_ublock_rt*out_mblock_n*out_ublock_ct*num_entries*out_tile_size + `DRAM_QUEUE_HEADER_SIZE);

         //host dram addresses must be aligned to 32B
         out_dram_buffer_addr[4:0] == 5'b00000;

         //host start address + size of buffers for all cores in the grid must be within the valid address range
         out_dram_buffer_addr inside {[`DRAM_BUFFER_START_ADDR:`DRAM_BUFFER_END_ADDR]};
         out_dram_buffer_addr + out_grid_size_y*out_grid_size_x*out_dram_buffer_size <= `DRAM_BUFFER_END_ADDR;
       }
    }


    constraint tile_sizes {
       inp_act_tile_size   == `get_tile_size(data_format);
       inp_con_tile_size   == `get_tile_size(data_format);
       out_tile_size       == `get_tile_size(data_format);

       exp_in_tile_size    == `get_tile_size(data_format);
       exp_out_tile_size   == `get_tile_size(data_format);

       sum_in_tile_size[0] == `get_tile_size(data_format);
       sum_in_tile_size[1] == `get_tile_size(data_format);
       sum_out_tile_size   == `get_tile_size(data_format);

       rec_in_tile_size    == `get_tile_size(data_format);
       rec_out_tile_size   == `get_tile_size(data_format);

       mul_in_tile_size[0] == `get_tile_size(data_format);
       mul_in_tile_size[1] == `get_tile_size(data_format);
       mul_out_tile_size   == `get_tile_size(data_format);
    }


    constraint limit_test_run_time {
       input_count      dist {[1:4]:=80, [5:8]:=20};
       microbatch_count dist {[1:4]:=80, [5:8]:=20};
       minibatch_count  dist {[1:4]:=80, [5:8]:=20};
       t                dist {[1:4]:=80, [5:8]:=20};

       // Limit total number of tiles to be processed to limit test execution time
       (t*input_count*microbatch_count*minibatch_count) <= (64);
    }

endclass


integer ntb_random_seed=0;
integer num_rand_loops=1;
string  out_filename;
integer out_filehandle;

string inp_con_prologue_str;
string inp_con_gptr_str;
string inp_con_lptr_str;
string out_buffer_loc_str;
string sum_inp_con_broadcast_val;
string sum_inp_con_broadcast_str;
string mul_inp_rec_broadcast_val;
string mul_inp_rec_broadcast_str;
string mul_untilize_output_str;
string sum_inputs_str;


program generator();
  initial begin
      $value$plusargs("num_rand_loops=%d", num_rand_loops);
      $value$plusargs("ntb_random_seed=%d", ntb_random_seed);

      if (!$value$plusargs("out=%s", out_filename)) begin
         $fatal("ERROR: Out file name not specified!");
      end else begin
         $display("INFO: Writing parameters of test cases parameters to %s file.", out_filename);
      end
  end

  initial begin
    softmax_constraints constraints = new();
//  test_config test_cfg = new();

    out_filehandle = $fopen(out_filename, "w");
    $fwrite(out_filehandle, "#ntb_random_seed=%0d\n", ntb_random_seed);

    //Generate num_rand_loops test cases, satisfying the defined constraints, and write the generated test case parameters to output test config file
    for (int i =0; i<num_rand_loops; i++) begin
       // Generate a test case
       constraints.randomize();

       /* ----------------------------------------------------------------------------------------------------------------------------------------------------------
          Processing some of the parameters before writing to a test config file
       ---------------------------------------------------------------------------------------------------------------------------------------------------------- */
       //Prepare inp_con buffer's prologue string property and local and globar read pointers
       inp_con_prologue_str = (constraints.inp_con_prologue ? "'true'"    : "'false'");
       inp_con_gptr_str     = (constraints.inp_con_prologue ? "'$c_zero'" : "'$gptr'");
       inp_con_lptr_str     = (constraints.inp_con_prologue ? "'$c_zero'" : "'$lptr'");

       //Prepare out_softmax buffer's location string
       out_buffer_loc_str = (constraints.out_buffer_loc == host ? "'host'" : "'dram'");

       //Prepare matmul's broadcast TM on the appropriate input (input that connects to input constant queue)
       sum_inp_con_broadcast_val = "";
       sum_inp_con_broadcast_str = "''";
       if (constraints.sum_in0_broadcast > 1 && constraints.sum_in1_broadcast > 1) begin
           $fatal("ERROR: Both input_0 and input_1 broadcast TMs on softmax matmul op are not supported! This is probably an issue in constraints definition.");

       end else if (constraints.sum_in0_broadcast > 1) begin
           sum_inp_con_broadcast_val.itoa(constraints.sum_in0_broadcast);
           sum_inp_con_broadcast_str = {"'input_0_tms: [broadcast: {c: ", sum_inp_con_broadcast_val, "}]'"};

       end else if (constraints.sum_in1_broadcast > 1) begin
           sum_inp_con_broadcast_val.itoa(constraints.sum_in1_broadcast);
           sum_inp_con_broadcast_str = {"'input_1_tms: [broadcast: {r: ", sum_inp_con_broadcast_val, "}]'"};
       end

       //Prepare matmul's inputs in correct order depending on dim value
       //  dim=-1 => inputs: [exp, input_constant]
       //  dim=-2 => inputs: [input_constant, exp]
       sum_inputs_str = "";
       if (constraints.pybuda_dim == -1) begin
           sum_inputs_str = "'[exp, input_constant]'";
       end else begin
           sum_inputs_str = "'[input_constant, exp]'";
       end

       //Prepare mul's broadcast TM on the appropriate input (input that connects to rec op output)
       mul_inp_rec_broadcast_val = "";
       mul_inp_rec_broadcast_str = "''";
       if (constraints.mul_row_broadcast > 1 && constraints.mul_col_broadcast > 1) begin
           $fatal("ERROR: Both row and column broadcast TMs on softmax multiply op are not supported! This is probably an issue in constraints definition.");

       end else if (constraints.mul_row_broadcast > 1) begin
           mul_inp_rec_broadcast_val.itoa(constraints.mul_row_broadcast);
           mul_inp_rec_broadcast_str = {"'input_1_tms: [broadcast: {r: ", mul_inp_rec_broadcast_val, "}]'"};

       end else if (constraints.mul_col_broadcast > 1) begin
           mul_inp_rec_broadcast_val.itoa(constraints.mul_col_broadcast);
           mul_inp_rec_broadcast_str = {"'input_1_tms: [broadcast: {c: ", mul_inp_rec_broadcast_val, "}]'"};
       end

       //Prepare mul's untilize_output string property
       mul_untilize_output_str = (constraints.mul_untilize_output ? "'true'" : "'false'");


       /* ----------------------------------------------------------------------------------------------------------------------------------------------------------
          Write test case parameters to a test config file
       ---------------------------------------------------------------------------------------------------------------------------------------------------------- */
       $fwrite(out_filehandle, "#Test id=%0d\n", i);
       $fwrite(out_filehandle, "- device: %s\n", `DEVICE);
       $fwrite(out_filehandle, "  target_device: %0d\n",    constraints.target_device);
       $fwrite(out_filehandle, "  input_count: %0d\n",      constraints.input_count);
       $fwrite(out_filehandle, "  microbatch_count: %0d\n", constraints.microbatch_count);
       $fwrite(out_filehandle, "  minibatch_count: %0d\n",  constraints.minibatch_count);
       $fwrite(out_filehandle, "  num_entries: %0d\n",      constraints.num_entries);
       $fwrite(out_filehandle, "  t: %0d\n",                constraints.t);
       $fwrite(out_filehandle, "  data_format: '%s'\n",     get_data_format(constraints.data_format));
       if (constraints.dest_fp32_acc_enable) begin
          $fwrite(out_filehandle, "  data_format_acc: '%s'\n", get_data_format(fp32));
       end else begin
          $fwrite(out_filehandle, "  data_format_acc: '%s'\n", get_data_format(fp16));
       end
       $fwrite(out_filehandle, "  math_fidelity: %s\n",     get_math_fidelity(constraints.math_fidelity));

       //PyBuda softmax parameters
       $fwrite(out_filehandle, "  pybuda_act_w: %0d\n", constraints.pybuda_act_w);
       $fwrite(out_filehandle, "  pybuda_act_z: %0d\n", constraints.pybuda_act_z);
       $fwrite(out_filehandle, "  pybuda_act_r: %0d\n", constraints.pybuda_act_r);
       $fwrite(out_filehandle, "  pybuda_act_c: %0d\n", constraints.pybuda_act_c);
       $fwrite(out_filehandle, "  pybuda_dim: %0d\n",   constraints.pybuda_dim);

       //input activation queue parameters
       $fwrite(out_filehandle, "  inp_act_mblock_m: %0d\n",    constraints.inp_act_mblock_m);
       $fwrite(out_filehandle, "  inp_act_mblock_n: %0d\n",    constraints.inp_act_mblock_n);
       $fwrite(out_filehandle, "  inp_act_ublock_rt: %0d\n",   constraints.inp_act_ublock_rt);
       $fwrite(out_filehandle, "  inp_act_ublock_ct: %0d\n",   constraints.inp_act_ublock_ct);
       $fwrite(out_filehandle, "  inp_act_grid_size_y: %0d\n", constraints.inp_act_grid_size_y);
       $fwrite(out_filehandle, "  inp_act_grid_size_x: %0d\n", constraints.inp_act_grid_size_x);
       //input activation queue dram buffers
//     $fwrite(out_filehandle, "  inp_act_dram_buffer_addr: %0x\n", constraints.inp_act_dram_buffer_addr);
//     $fwrite(out_filehandle, "  inp_act_dram_buffer_size: %0x\n", constraints.inp_act_dram_buffer_size);
       $fwrite(out_filehandle, "  inp_act_dram_buffers: '[");
       for (int core=0;core<constraints.inp_act_grid_size_y*constraints.inp_act_grid_size_x; core++) begin
           $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.inp_act_dram_buffer_addr + core*constraints.inp_act_dram_buffer_size);
           if (core<constraints.inp_act_grid_size_y*constraints.inp_act_grid_size_x-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");

       //input constant queue parameters
       $fwrite(out_filehandle, "  inp_con_mblock_m: %0d\n",    constraints.inp_con_mblock_m);
       $fwrite(out_filehandle, "  inp_con_mblock_n: %0d\n",    constraints.inp_con_mblock_n);
       $fwrite(out_filehandle, "  inp_con_ublock_rt: %0d\n",   constraints.inp_con_ublock_rt);
       $fwrite(out_filehandle, "  inp_con_ublock_ct: %0d\n",   constraints.inp_con_ublock_ct);
       $fwrite(out_filehandle, "  inp_con_grid_size_y: %0d\n", constraints.inp_con_grid_size_y);
       $fwrite(out_filehandle, "  inp_con_grid_size_x: %0d\n", constraints.inp_con_grid_size_x);
       $fwrite(out_filehandle, "  inp_con_prologue: %s\n",     inp_con_prologue_str);
       $fwrite(out_filehandle, "  inp_con_gptr: %s\n",         inp_con_gptr_str);
       $fwrite(out_filehandle, "  inp_con_lptr: %s\n",         inp_con_lptr_str);
       $fwrite(out_filehandle, "  inp_con_entries: %0d\n",     constraints.inp_con_entries);
       //input constant queue dram buffers
//     $fwrite(out_filehandle, "  inp_con_dram_buffer_addr: %0x\n", constraints.inp_con_dram_buffer_addr);
//     $fwrite(out_filehandle, "  inp_con_dram_buffer_size: %0x\n", constraints.inp_con_dram_buffer_size);
       $fwrite(out_filehandle, "  inp_con_dram_buffers: '[");
       for (int core=0;core<constraints.inp_con_grid_size_y*constraints.inp_con_grid_size_x; core++) begin
           //TODO: dram_channel is not indexed correctly here
           $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.inp_con_dram_buffer_addr + core*constraints.inp_con_dram_buffer_size);
           if (core<constraints.inp_con_grid_size_y*constraints.inp_con_grid_size_x-1) $fwrite(out_filehandle, ", ");
       end
       $fwrite(out_filehandle, "]'\n");

       //softmax output queue parameters
       $fwrite(out_filehandle, "  out_mblock_m: %0d\n",    constraints.out_mblock_m);
       $fwrite(out_filehandle, "  out_mblock_n: %0d\n",    constraints.out_mblock_n);
       $fwrite(out_filehandle, "  out_ublock_rt: %0d\n",   constraints.out_ublock_rt);
       $fwrite(out_filehandle, "  out_ublock_ct: %0d\n",   constraints.out_ublock_ct);
       $fwrite(out_filehandle, "  out_grid_size_y: %0d\n", constraints.out_grid_size_y);
       $fwrite(out_filehandle, "  out_grid_size_x: %0d\n", constraints.out_grid_size_x);
       //output queue host buffers
       $fwrite(out_filehandle, "  # Scale output queue grid, ublock, and mblock size if output is untilized\n");
       $fwrite(out_filehandle, "  # Net2pipe requires grid size and ublock of 1x1 if untilized output is written to the queue\n");
//     $fwrite(out_filehandle, "  out_dram_buffer_addr: %0x\n", constraints.out_dram_buffer_addr);
//     $fwrite(out_filehandle, "  out_dram_buffer_size: %0x\n", constraints.out_dram_buffer_size);
       $fwrite(out_filehandle, "  out_buffer_loc: %0s\n", out_buffer_loc_str);
       if (constraints.mul_untilize_output) begin
           if (constraints.out_buffer_loc == dram) begin
             $fwrite(out_filehandle, "  out_dram_buffers: '[[%0d, 0x%0x]]'\n", constraints.dram_channel[0], constraints.out_dram_buffer_addr);
           end else begin
             $fwrite(out_filehandle, "  out_dram_buffers: '[0x%0x]'\n", constraints.out_dram_buffer_addr);
           end
       end else begin
           $fwrite(out_filehandle, "  out_dram_buffers: '[");
           for (int core=0;core<constraints.out_grid_size_y*constraints.out_grid_size_x; core++) begin
             if (constraints.out_buffer_loc == dram) begin
               $fwrite(out_filehandle, "[%0d, 0x%0x]", constraints.dram_channel[core], constraints.out_dram_buffer_addr + core*constraints.out_dram_buffer_size);
             end else begin
               $fwrite(out_filehandle, "0x%0x", constraints.out_dram_buffer_addr + core*constraints.out_dram_buffer_size);
             end
             if (core<constraints.out_grid_size_y*constraints.out_grid_size_x-1) $fwrite(out_filehandle, ", ");
           end
           $fwrite(out_filehandle, "]'\n");
       end

       //exponent op parameters
       $fwrite(out_filehandle, "  exp_mblock_m: %0d\n",    constraints.exp_mblock_m);
       $fwrite(out_filehandle, "  exp_mblock_n: %0d\n",    constraints.exp_mblock_n);
       $fwrite(out_filehandle, "  exp_ublock_rt: %0d\n",   constraints.exp_ublock_rt);
       $fwrite(out_filehandle, "  exp_ublock_ct: %0d\n",   constraints.exp_ublock_ct);
       $fwrite(out_filehandle, "  exp_grid_size_y: %0d\n", constraints.exp_grid_size_y);
       $fwrite(out_filehandle, "  exp_grid_size_x: %0d\n", constraints.exp_grid_size_x);
       $fwrite(out_filehandle, "  exp_grid_loc_y: %0d\n",  constraints.exp_grid_loc_y);
       $fwrite(out_filehandle, "  exp_grid_loc_x: %0d\n",  constraints.exp_grid_loc_x);

       //matmul op parameters
       $fwrite(out_filehandle, "  sum_mblock_m: %0d\n",         constraints.sum_mblock_m);
       $fwrite(out_filehandle, "  sum_mblock_n: %0d\n",         constraints.sum_mblock_n);
       $fwrite(out_filehandle, "  sum_ublock_rt: %0d\n",        constraints.sum_ublock_rt);
       $fwrite(out_filehandle, "  sum_ublock_ct: %0d\n",        constraints.sum_ublock_ct);
       $fwrite(out_filehandle, "  sum_grid_size_y: %0d\n",      constraints.sum_grid_size_y);
       $fwrite(out_filehandle, "  sum_grid_size_x: %0d\n",      constraints.sum_grid_size_x);
       $fwrite(out_filehandle, "  sum_grid_loc_y: %0d\n",       constraints.sum_grid_loc_y);
       $fwrite(out_filehandle, "  sum_grid_loc_x: %0d\n",       constraints.sum_grid_loc_x);
       $fwrite(out_filehandle, "  sum_m_k: %0d\n",              constraints.sum_m_k);
       $fwrite(out_filehandle, "  sum_u_kt: %0d\n",             constraints.sum_u_kt);
       $fwrite(out_filehandle, "  sum_inp_con_broadcast: %s\n", sum_inp_con_broadcast_str);
       $fwrite(out_filehandle, "  sum_inputs: %s\n",            sum_inputs_str);

       //reciprocal op parameters
       $fwrite(out_filehandle, "  rec_mblock_m: %0d\n",    constraints.rec_mblock_m);
       $fwrite(out_filehandle, "  rec_mblock_n: %0d\n",    constraints.rec_mblock_n);
       $fwrite(out_filehandle, "  rec_ublock_rt: %0d\n",   constraints.rec_ublock_rt);
       $fwrite(out_filehandle, "  rec_ublock_ct: %0d\n",   constraints.rec_ublock_ct);
       $fwrite(out_filehandle, "  rec_grid_size_y: %0d\n", constraints.rec_grid_size_y);
       $fwrite(out_filehandle, "  rec_grid_size_x: %0d\n", constraints.rec_grid_size_x);
       $fwrite(out_filehandle, "  rec_grid_loc_y: %0d\n",  constraints.rec_grid_loc_y);
       $fwrite(out_filehandle, "  rec_grid_loc_x: %0d\n",  constraints.rec_grid_loc_x);

       //multiply op parameters
       $fwrite(out_filehandle, "  mul_mblock_m: %0d\n",         constraints.mul_mblock_m);
       $fwrite(out_filehandle, "  mul_mblock_n: %0d\n",         constraints.mul_mblock_n);
       $fwrite(out_filehandle, "  mul_ublock_rt: %0d\n",        constraints.mul_ublock_rt);
       $fwrite(out_filehandle, "  mul_ublock_ct: %0d\n",        constraints.mul_ublock_ct);
       $fwrite(out_filehandle, "  mul_grid_size_y: %0d\n",      constraints.mul_grid_size_y);
       $fwrite(out_filehandle, "  mul_grid_size_x: %0d\n",      constraints.mul_grid_size_x);
       $fwrite(out_filehandle, "  mul_grid_loc_y: %0d\n",       constraints.mul_grid_loc_y);
       $fwrite(out_filehandle, "  mul_grid_loc_x: %0d\n",       constraints.mul_grid_loc_x);
//     $fwrite(out_filehandle, "  mul_row_broadcast: %x\n", constraints.mul_row_broadcast);
//     $fwrite(out_filehandle, "  mul_col_broadcast: %x\n", constraints.mul_col_broadcast);
       $fwrite(out_filehandle, "  mul_inp_rec_broadcast: %s\n", mul_inp_rec_broadcast_str);
       $fwrite(out_filehandle, "  mul_untilize_output: %s\n",   mul_untilize_output_str);

    end
    $fclose(out_filehandle);
  end
endprogram