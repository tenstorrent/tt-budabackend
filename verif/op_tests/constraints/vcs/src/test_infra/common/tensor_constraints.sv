`ifndef __TENSOR_CONSTRAINTS_SV__
`define __TENSOR_CONSTRAINTS_SV__

class tensor_constraints;
    bit custom_block_dims = 0;
    rand bit[3:0] grid_size_x;
    rand bit[3:0] grid_size_y;
    rand bit[7:0] t;
    rand bit[7:0] mblock_m, mblock_n, ublock_rt, ublock_ct;
    rand e_data_format data_format;

    // Tiny tile hangs on BH
    bit tiny_tiles_enabled = 0;
    rand bit enable_tiny_tile;
    rand bit[5:0] out_tile_dim_r, out_tile_dim_c;
    bit tile_1x32_enabled = 1;
    bit tile_2x32_enabled = 1;
    bit tile_4x32_enabled = 1;
    bit tile_8x32_enabled = 1;
    bit tile_16x32_enabled = 1;

    // _a data formats cause zero outputs in some cases on BH
    constraint rand_data_formats {
        !(data_format inside {`FLOAT_A_FORMATS});
    }

    constraint max_block_dims {
        if (custom_block_dims == 0) {
            t         inside  {[1:64]};
            mblock_m  inside  {[1:16]};
            mblock_n  inside  {[1:16]};
            ublock_rt inside  {[1:16]};
            ublock_ct inside  {[1:16]};
        }
    }

    constraint rand_grid_size {
        grid_size_x inside  {[1:`GRID_SIZE_X]};
        grid_size_y inside  {[1:`GRID_SIZE_Y]};
    }

    constraint rand_tiny_tiles_enabled {
        enable_tiny_tile dist {[0:0]:=40, [1:1]:=60};
        tiny_tiles_enabled == 0 -> enable_tiny_tile == 0;
    }

    constraint rand_tile_dims {
        if (enable_tiny_tile != 0 ) {
            `default_tile_dim_constraints(out_tile_dim_r, out_tile_dim_c)

            out_tile_dim_r == 1 -> `constraint_data_format_tile_dim_1x32(data_format);
            out_tile_dim_r == 2 -> `constraint_data_format_tile_dim_2x32(data_format);

            if (tile_1x32_enabled == 0) {
                out_tile_dim_r != 1;
            }
            if (tile_2x32_enabled == 0) {
                out_tile_dim_r != 2;
            }
            if (tile_4x32_enabled == 0) {
                out_tile_dim_r != 4;
            }
            if (tile_8x32_enabled == 0) {
                out_tile_dim_r != 8;
            }
            if (tile_16x32_enabled == 0) {
                out_tile_dim_r != 16;
            }
        } else {
            out_tile_dim_r == 32;
            out_tile_dim_c == 32;
        }
    }

    // tenstorrent/budabackend#143
    constraint pipegen {
        (t>1) -> ((t%2) == 0);
    }
endclass

`endif // __TENSOR_CONSTRAINTS_SV__