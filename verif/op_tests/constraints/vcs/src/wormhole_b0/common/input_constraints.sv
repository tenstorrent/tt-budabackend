`ifndef __INPUT_CONSTRAINTS_SV__
`define __INPUT_CONSTRAINTS_SV__

typedef class operation_constraints;

class input_constraints;
    rand node_constraints producer;
    rand tensor_constraints post_tm_tensor;
    rand operation_constraints consumer;

    bit transpose_enabled;
    rand bit transpose_en;

    bit is_dim_equal;
    bit is_t_accumulation_enabled;

    bit special_data_format = 0;

    function new(node_constraints producer, operation_constraints consumer);
        this.producer = producer;
        this.consumer = consumer;
        this.is_dim_equal = 1;
        this.is_t_accumulation_enabled = 0;
        this.transpose_enabled = 0;
        this.post_tm_tensor = new();
    endfunction

    constraint rand_post_tm_tensor {
        post_tm_tensor.data_format == producer.tensor.data_format;
        post_tm_tensor.t == producer.tensor.t;
        if (transpose_en == 0) {
            post_tm_tensor.mblock_m == producer.tensor.mblock_m;
            post_tm_tensor.mblock_n == producer.tensor.mblock_n;
            post_tm_tensor.ublock_rt == producer.tensor.ublock_rt;
            post_tm_tensor.ublock_ct == producer.tensor.ublock_ct;
            post_tm_tensor.grid_size_x == producer.tensor.grid_size_x;
            post_tm_tensor.grid_size_y == producer.tensor.grid_size_y;
            post_tm_tensor.out_tile_dim_r == producer.tensor.out_tile_dim_r;
            post_tm_tensor.out_tile_dim_c == producer.tensor.out_tile_dim_c;
        } else {
            post_tm_tensor.mblock_m == producer.tensor.mblock_n;
            post_tm_tensor.mblock_n == producer.tensor.mblock_m;
            post_tm_tensor.ublock_rt == producer.tensor.ublock_ct;
            post_tm_tensor.ublock_ct == producer.tensor.ublock_rt;
            post_tm_tensor.grid_size_x == producer.tensor.grid_size_y;
            post_tm_tensor.grid_size_y == producer.tensor.grid_size_x;
            post_tm_tensor.out_tile_dim_r == producer.tensor.out_tile_dim_c;
            post_tm_tensor.out_tile_dim_c == producer.tensor.out_tile_dim_r;
        }
    }

    constraint rand_grid_size {
        post_tm_tensor.grid_size_x == consumer.tensor.grid_size_x;
        post_tm_tensor.grid_size_y == consumer.tensor.grid_size_y;
    }

    constraint tiny_tiles_enabled {
        consumer.tensor.tiny_tiles_enabled == 0 -> { post_tm_tensor.enable_tiny_tile == 0; producer.tensor.enable_tiny_tile == 0; }
    }

    constraint rand_tiny_tiles {
        producer.tensor.enable_tiny_tile == post_tm_tensor.enable_tiny_tile;
        consumer.tensor.enable_tiny_tile == post_tm_tensor.enable_tiny_tile;
    }

    constraint rand_t {
        if (consumer.gradient_op_en == 0) {
            if (is_t_accumulation_enabled == 1) {
                post_tm_tensor.t == consumer.tensor.t * consumer._z;
            } else {
                post_tm_tensor.t == consumer.tensor.t;
            }
        } else {
            post_tm_tensor.t == consumer.tensor.t;
        }
    }

    constraint rand_block_dims {
        if (is_dim_equal) {
            post_tm_tensor.mblock_m == consumer.tensor.mblock_m;
            post_tm_tensor.mblock_n == consumer.tensor.mblock_n;
            post_tm_tensor.ublock_rt == consumer.tensor.ublock_rt;
            post_tm_tensor.ublock_ct == consumer.tensor.ublock_ct;
        }
    }

    constraint rand_data_formats {
        if (special_data_format == 0) {
            if (consumer.dest_data_format != int32) {
                (consumer.tensor.data_format inside {`FLOAT_B_FORMATS}) -> (post_tm_tensor.data_format inside {`FLOAT_B_FORMATS});
                (consumer.tensor.data_format inside {`FLOAT_A_FORMATS}) -> (post_tm_tensor.data_format inside {`FLOAT_A_FORMATS});
            }
        }
    }

    constraint rand_transpose {
        transpose_enabled == 0 -> transpose_en == 0;
        transpose_en dist {0:=80, 1:=20};
        (consumer.tensor.grid_size_x >= `GRID_SIZE_Y) || (consumer.tensor.grid_size_y >= `GRID_SIZE_X) -> (transpose_en == 0);
    }

endclass

`endif // __INPUT_CONSTRAINTS_SV__
