`ifndef __BINARY_OP__
`define __BINARY_OP__

class binary_op extends operation_constraints;

    rand integer in0_buffer;
    rand integer in1_buffer;
    rand integer out_buffer;

    rand tensor_constraints input_0;
    rand tensor_constraints input_1;

    rand e_binary_type binary_type;
    bit force_grad_acc = 0;

    function new(string name, bit is_grad_acc = 0);
        super.new(name);
        this.node_type = "binary";
        force_grad_acc = is_grad_acc;

        // Enabled features
        m_k_enabled = 0;
        srnd_enabled = 1;
        relu_enabled = 1;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        l1_acc_enabled = 0;
        gradient_op_enabled = 1;
        kernel_broadcast_enabled = 1;
    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 1;
        in[0].is_t_accumulation_enabled = 0;
        in[0].transpose_enabled = 1;
        in[0].broadcast_enabled = 1;

        in[1].is_dim_equal = 1;
        in[1].is_t_accumulation_enabled = 0;
        in[1].broadcast_enabled = 1;

        input_0 = in[0].post_tm_tensor;
        input_1 = in[1].post_tm_tensor;
    endfunction

    virtual function string get_node_type();
        return get_binary_type(binary_type);
    endfunction

    virtual function int get_max_inputs();
        return 2;
    endfunction

    constraint rand_input_data_format {
        dest_data_format == int32 -> {
            if (binary_type == add) {
                input_0.data_format inside {int8, int32};
                input_1.data_format inside {int8, int32};
            } else {
                input_0.data_format == int8;
                input_1.data_format == int8;
            }
        }
    }

    constraint rand_force_grad_acc {
        gradient_op_en == force_grad_acc;
    }

    constraint rand_transpose {
        dest_data_format==int32 -> in[0].transpose_en == 0;
        in[0].transpose_en == 1 -> gradient_op_en == 0;
        arch.transpose_input_0_binary == 0 -> in[0].transpose_en == 0;
    }

    constraint rand_node_used {
        in[0].producer.is_node_used == is_node_used;
        in[1].producer.is_node_used == is_node_used;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * (kernel_broadcast_en[0] ? kernel_broadcast_factors[0] : tensor.ublock_rt * tensor.ublock_ct) * `get_tile_size(input_0.data_format);
        in1_buffer == 2 * (kernel_broadcast_en[1] ? kernel_broadcast_factors[1] : tensor.ublock_rt * tensor.ublock_ct) * `get_tile_size(input_1.data_format);

        if (untilize == 1 && gradient_op_en == 0) {
            out_buffer == 2 * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format);
        } else if (untilize == 0 && gradient_op_en == 0) {
            out_buffer == 2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format);
        } else if (untilize == 0 && gradient_op_en == 1) {
            out_buffer == 2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format) * tensor.t;
        }
        in0_buffer + in1_buffer + out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_tile_dims {
        input_0.out_tile_dim_r == input_1.out_tile_dim_r;
        input_0.out_tile_dim_c == input_1.out_tile_dim_c;
        tensor.out_tile_dim_c == 16 -> input_1.out_tile_dim_r == 32 && input_1.out_tile_dim_c == 16;
        tensor.out_tile_dim_c == 32 -> input_1.out_tile_dim_c == 32;
    }

    constraint rand_binary_constraints {
        binary_type == maximum -> gradient_op_en == 0;
        arch.gradient_binary_multiply_constraint == 0 && binary_type != multiply -> gradient_op_en == 0;
    
    }

    constraint rand_maximum_tiny_tiles {
        input_0.out_tile_dim_r != 32 || input_0.out_tile_dim_c != 32 -> binary_type != maximum;
    }

    constraint rand_kernel_broadcast_freq {
        kernel_broadcast_op_en == 1 -> {
            kernel_broadcast_en[0] == 1 || kernel_broadcast_en[1] == 1;
        }
    }

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        if (index == 1) return in[1];
        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg;
        cfg = tst_cfg.get_binary_comparison_config(tensor.data_format);
        cfg.Type = "AllClose";
        cfg.verbosity = "Concise";
        return cfg;
    endfunction

endclass
`endif // __BINARY_OP__