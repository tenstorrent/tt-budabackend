`ifndef __DEPTHWISE_OP__
`define __DEPTHWISE_OP__

class depthwise_op extends operation_constraints;

    rand tensor_constraints input_0;
    rand tensor_constraints input_1;
    rand tensor_constraints input_bias;

    rand integer in0_buffer;
    rand integer in1_buffer;
    rand integer out_buffer;
    rand integer bias_buffer;
    rand integer intermed_buffer;
    rand bit m_k_1;

    function new(string name);
        super.new(name);
        this.node_type = "depthwise";

        // Enabled features
        m_k_enabled = 1;
        srnd_enabled = 1;
        relu_enabled = 1;
        l1_acc_enabled = 1;
        bias_enabled = 1;
        kernel_broadcast_enabled = 1;

        // Disabled until fixed
        gradient_op_enabled = 0;
        tensor.tiny_tiles_enabled = 0;

        // Kernel does not support
        accumulation_enabled = 0;
        quant_enabled = 0;

        // This is not possible for operations that have intermediate buffer
        untilize_enabled = 0;

    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 0;
        in[0].is_t_accumulation_enabled = 1;
        in[0].broadcast_enabled = 1;

        in[1].is_dim_equal = 0;
        in[1].is_t_accumulation_enabled = 1;
        in[1].broadcast_enabled = 1;

        // This depends on bias
        in[2].producer.is_node_always_used = 0;
        in[2].broadcast_enabled = 1;

        input_0 = in[0].post_tm_tensor;
        input_1 = in[1].post_tm_tensor;
        input_bias = in[2].post_tm_tensor;
    endfunction

    // some of the inputs does not have to be used at all
    constraint enable_nodes {
        if (is_node_used) {
            // First two inputs are always used
            in[0].producer.is_node_used == 1;
            in[1].producer.is_node_used == 1;
            in[2].producer.is_node_used == bias_en;
        }
    }

    constraint rand_depthwise_math_fidelity {
        math_fidelity == HiFi4;
    }

    constraint rand_data_format {
        if (dest_data_format == int32) {
            input_0.data_format == int8;
            input_1.data_format == int8;
            input_bias.data_format == int32;
        }
    }

    constraint rand_inner_dim {
        m_k == input_0.mblock_n * input_0.grid_size_x;
        u_kt == input_0.ublock_ct;

        m_k == input_1.mblock_m * input_1.grid_size_y;
        u_kt == input_1.ublock_rt;

        input_0.ublock_rt == tensor.ublock_rt;
        input_0.ublock_ct == tensor.ublock_ct;

        input_0.mblock_n * input_0.ublock_ct == m_k * input_1.mblock_n * input_1.ublock_ct;

        m_k_1 dist {0:=95, 1:=5};
        m_k_1 == 1 -> m_k == 1;
        m_k_1 == 0 -> m_k > 1;
    }

    constraint rand_outer_dim {
        tensor.grid_size_x == 1;
        input_0.mblock_m * input_0.ublock_rt == tensor.mblock_m * tensor.ublock_rt;
        input_1.mblock_n * input_1.ublock_ct == tensor.mblock_n * tensor.ublock_ct;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == (kernel_broadcast_en[0] ? 2 * kernel_broadcast_factors[0] : 2 * input_0.mblock_m * input_0.ublock_rt * input_0.ublock_ct) * `get_tile_size(input_0.data_format);
        in1_buffer == (kernel_broadcast_en[1] ? 2 * kernel_broadcast_factors[1] : 2 * input_1.mblock_n * input_1.ublock_rt * input_1.ublock_ct) * `get_tile_size(input_1.data_format);
        bias_buffer == ((bias_en == 1) ? ((kernel_broadcast_en[2] ? 2 * kernel_broadcast_factors[2] : 2 * input_bias.ublock_rt * input_bias.ublock_ct) * `get_tile_size(input_bias.data_format)) : 0);
        out_buffer == (2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        intermed_buffer == ((tensor.data_format != intermed_data_format) ? (tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(intermed_data_format)) : 0);

        in0_buffer + in1_buffer + bias_buffer + out_buffer + intermed_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_disabled_features_with_gradient_enabled {
        gradient_op_en == 1 -> { bias_en == 0; relu_en == 0; dest_data_format != int32; }
    }

    constraint rand_depthwise_current_limitations {
        gradient_op_en == 0;
        tensor.tiny_tiles_enabled == 0;
        u_kt == 1;
        dest_data_format == int32 -> {
            bias_en == 0;
            l1_acc_en == 1;
            output_data_format == int32;
        }
    }

    constraint rand_kernel_broadcast {
        bias_en == 0 -> kernel_broadcast_en[2] == 0;

        foreach (in[i]) {
            kernel_broadcast_en[i] dist { 0:=50, 1:=50 };
            kernel_broadcast_en[i] == 1 -> in[i].producer.tensor.grid_size_x == 1 && in[i].producer.tensor.grid_size_y == 1;
            kernel_broadcast_factors[i] < `MAX_L1_MEM_BUFFER_SIZE / `get_tile_size(in[i].producer.tensor.data_format);
            is_kernel_broadcast_per_t[i] dist {0:=50, 1:=50};
            in[i].producer.tensor.t > 1 -> is_kernel_broadcast_per_t[i] == 1;
            in[i].broadcast_en == 0 -> kernel_broadcast_en[i] == 0;
            if (i > 0) {
                kernel_broadcast_en[i] == 1 -> { // assume that ublock_order == r
                    kernel_broadcast_period_height[i] % in[i].post_tm_tensor.ublock_rt == 0;
                    kernel_broadcast_period_height[i] <= in[i].post_tm_tensor.ublock_rt * in[i].post_tm_tensor.mblock_m;
                    kernel_broadcast_period_height[i] % (in[i].producer.tensor.mblock_m * in[i].producer.tensor.ublock_rt) == 0;
                    kernel_broadcast_period_width[i] == in[i].post_tm_tensor.mblock_n * in[i].post_tm_tensor.ublock_ct;
                    kernel_broadcast_factors[i] == kernel_broadcast_period_height[i] * kernel_broadcast_period_width[i];
                    kernel_broadcast_factors[i] < in[i].post_tm_tensor.mblock_m * in[i].post_tm_tensor.mblock_n * in[i].post_tm_tensor.ublock_rt * in[i].post_tm_tensor.ublock_ct;
                    kernel_broadcast_factors[i] > 0;
                }
            }
        }

        kernel_broadcast_en[0] == 1 -> {
            in[0].is_broadcast_c == 1;
            in[0].broadcast_factor_c % m_k == 0;

            kernel_broadcast_period_height[0] % tensor.ublock_rt == 0;
            kernel_broadcast_period_height[0] % (in[0].producer.tensor.mblock_m * in[0].producer.tensor.ublock_rt) == 0;
            kernel_broadcast_period_height[0] <= tensor.ublock_rt * tensor.mblock_m;
            kernel_broadcast_period_height[0] > 0;

            kernel_broadcast_period_width[0] % (tensor.ublock_ct * tensor.mblock_n) == 0;
            kernel_broadcast_period_width[0] <= input_0.ublock_ct * input_0.mblock_n;
            kernel_broadcast_period_width[0] > 0;

            kernel_broadcast_factors[0] == kernel_broadcast_period_height[0] * kernel_broadcast_period_width[0];
            kernel_broadcast_factors[0] < tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct;
            kernel_broadcast_factors[0] > 0;
        }
        

        if (kernel_broadcast_en[0] && kernel_broadcast_factors[0] > 1 && tensor.ublock_ct < tensor.ublock_rt) {
            kernel_broadcast_factors[0] % tensor.ublock_ct == 0;
        }
        if (kernel_broadcast_en[1] && kernel_broadcast_factors[1] > 1 && tensor.ublock_ct >= tensor.ublock_rt) {
            kernel_broadcast_factors[1] % (tensor.ublock_rt * u_kt) == 0;
        }
    }

    virtual function integer get_num_inputs();
        if (bias_en == 1) return 3;
        return 2;
    endfunction

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        if (index == 1) return in[1];
        if (index == 2 && bias_en == 1) return in[2];
        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function int get_max_inputs();
        return 3;
    endfunction

endclass
`endif // __DEPTHWISE_OP__