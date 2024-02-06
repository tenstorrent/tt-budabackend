`ifndef __MATMUL_OP__
`define __MATMUL_OP__

test_config tst_cfg = test_config::new();

class matmul_op extends operation_constraints;

    rand tensor_constraints input_0;
    rand tensor_constraints input_1;
    rand tensor_constraints input_bias;
    rand tensor_constraints input_scaller;

    rand integer in0_buffer;
    rand integer in1_buffer;
    rand integer out_buffer;
    rand integer bias_buffer;
    rand integer scaller_buffer;
    rand integer intermed_buffer;

    rand bit sfpu_op_en;
    rand bit sfpu_pack_thread_en;

    function new(string name);
        super.new(name);
        this.node_type = "matmul";

        // Enabled features
        m_k_enabled = 1;
        srnd_enabled = 1;
        relu_enabled = 1;
        quant_enabled = 1;
        accumulation_enabled = 1;
        bias_enabled = 1;
        l1_acc_enabled = 1;
        gradient_op_enabled = 1;
    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 0;
        in[0].is_t_accumulation_enabled = 1;

        in[1].is_dim_equal = 0;
        in[1].is_t_accumulation_enabled = 1;
        in[1].transpose_enabled = 1;

        in[2].producer.is_node_always_used = 0;
        in[3].producer.is_node_always_used = 0;

        input_0 = in[0].post_tm_tensor;
        input_1 = in[1].post_tm_tensor;
        input_1.tile_1x32_enabled = 0;
        input_bias = in[2].post_tm_tensor;
        input_bias.tile_16x32_enabled = 0;
        input_scaller = in[3].post_tm_tensor;
        input_scaller.enable_tiny_tile = 0;
    endfunction

    // some of the inputs does not have to be used at all
    constraint enable_nodes {
        if (is_node_used) {
            // First two inputs are always used
            in[0].producer.is_node_used == 1;
            in[1].producer.is_node_used == 1;
            in[2].producer.is_node_used == bias_en;
            in[3].producer.is_node_used == quant_en;
        }
    }

    constraint rand_input_data_format {
        if (dest_data_format == int32) {
            input_0.data_format == int8;
            input_1.data_format == int8;
            input_bias.data_format == int32;
            input_scaller.data_format == fp32;
        }
    }

    constraint rand_inner_dim {
        m_k == input_0.mblock_n * input_0.grid_size_x;
        u_kt == input_0.ublock_ct;

        m_k == input_1.mblock_m * input_1.grid_size_y;
        u_kt == input_1.ublock_rt;
    }

    constraint rand_outer_dim {
        input_0.mblock_m * input_0.ublock_rt == tensor.mblock_m * tensor.ublock_rt;
        input_1.mblock_n * input_1.ublock_ct == tensor.mblock_n * tensor.ublock_ct;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * input_0.mblock_m * input_0.ublock_rt * input_0.ublock_ct * `get_tile_size(input_0.data_format);
        in1_buffer == 2 * input_1.mblock_n * input_1.ublock_rt * input_1.ublock_ct * `get_tile_size(input_1.data_format);
        bias_buffer == ((bias_en == 1) ? (2 * input_bias.ublock_rt * input_bias.ublock_ct * `get_tile_size(input_bias.data_format)) : 0);
        scaller_buffer == ((quant_en == 1) ? (2 * input_scaller.ublock_rt * input_scaller.ublock_ct * `get_tile_size(input_scaller.data_format)) : 0);
        out_buffer == (2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        intermed_buffer == ((tensor.data_format != intermed_data_format) ? (tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(intermed_data_format)) : 0);

        in0_buffer + in1_buffer + bias_buffer + scaller_buffer + out_buffer + intermed_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_matmul_constraints {
        gradient_op_en == 1 -> {
            bias_en == 0;
            relu_en == 0;
            dest_data_format != int32;
        }

        dest_data_format == int32 -> l1_acc_en == 0;
    }

    constraint rand_tile_dims {
        input_1.out_tile_dim_r inside {16, 32};
        input_1.out_tile_dim_c == 32;
        in[1].transpose_en == 1 -> input_1.out_tile_dim_r == 32;

        input_bias.out_tile_dim_r inside {1, 32};
        input_bias.out_tile_dim_c == 32;

        tensor.out_tile_dim_c == 32;
    }

    constraint rand_sfpu_op {
        sfpu_op_en dist {0:=80, 1:=20};
        (input_0.out_tile_dim_r != 32 || input_0.out_tile_dim_c != 32 || input_1.out_tile_dim_r != 32 || input_1.out_tile_dim_c != 32 || tensor.out_tile_dim_c != 32 || tensor.out_tile_dim_r != 32) -> (sfpu_op_en == 0); // #2174
    }

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        if (sfpu_op_en == 1) begin
            $fwrite(out_filehandle, "sfpu_op: gelu, ");
            $fwrite(out_filehandle, "sfpu_execution_thread: %0s, ", sfpu_pack_thread_en ? "pack" : "math");
        end
    endfunction

    virtual function integer get_num_inputs();
        if (bias_en == 1 && quant_en == 1) return 4;
        if (bias_en == 1 || quant_en == 1) return 3;
        return 2;
    endfunction

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        if (index == 1) return in[1];
        if (index == 2 && bias_en == 1) return in[2];
        if (index == 2 && bias_en == 0 && quant_en == 1) return in[3];
        if (index == 3 && bias_en == 1 && quant_en == 1) return in[3];

        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg = tst_cfg.get_matmul_comparison_config(tensor.data_format, num_inputs, m_k,
            math_fidelity, 0, 0, intermed_data_format, _z, (relu_en) ? get_relu_mode(relu_mode) : "");
        cfg.Type = "AllClose";
        cfg.verbosity = "Concise";
        return cfg;
    endfunction

    virtual function int get_max_inputs();
        return 4;
    endfunction

endclass
`endif // __MATMUL_OP__