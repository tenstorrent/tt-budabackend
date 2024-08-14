`ifndef __UNARY_OP_SV__
`define __UNARY_OP_SV__

class unary_op extends operation_constraints;
    rand tensor_constraints input0;

    rand integer in0_buffer;
    rand integer out_buffer;
    string stimulus_config_type;
    real stimulus_config_uniform_lower_bound, stimulus_config_uniform_upper_bound;


    rand e_unary_type unary_op_type;
    bit force_grad_acc = 0;

    function new(string name, bit is_grad_acc = 0);
        super.new(name);
        this.node_type = "unary_op";
        force_grad_acc = is_grad_acc;

        // Enabled features
        m_k_enabled = 0;
        srnd_enabled = 1;
        relu_enabled = 0;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        gradient_op_enabled = 1;
        l1_acc_enabled = 0;
        kernel_broadcast_enabled = 1;
        vector_mode_enabled = 1;
    endfunction

    virtual function void initialize_inputs();
        this.input0 = in[0].post_tm_tensor;
        this.in[0].transpose_enabled = 1;
        this.in[0].broadcast_enabled = 1;
    endfunction

    constraint rand_input_data_format {
        dest_data_format == int32 -> {
            input0.data_format == tensor.data_format;
            input0.data_format == int8;
            // Transpose is not supported for Int32
            in[0].transpose_en == 1 -> input0.data_format == int8;
        }
    }

    constraint rand_force_grad_acc {
        gradient_op_en == force_grad_acc;
    }

    constraint constraint_sfpu_vector_mode {
      if (tensor.out_tile_dim_r != 32 || tensor.out_tile_dim_c != 32) {
         // For cases input == 32x16, we want vector mode c
         if (tensor.out_tile_dim_c == 16 || (tensor.out_tile_dim_r == 16 && in[0].transpose_en == 1)) {
            sfpu_vector_mode == vector_mode_c;
         } else {
            // Otherwise, for cases of Nx32, we want vector mode r (N != 32)
            sfpu_vector_mode == vector_mode_r;
         }
      } else {
         sfpu_vector_mode dist {vector_mode_rc:=50, vector_mode_r:=25, vector_mode_c:=25};
      }
   }

    constraint rand_unary_op_type {
        dest_data_format == int32 -> unary_op_type == datacopy;
        unary_op_type != datacopy -> gradient_op_en == 0;
    }

    constraint rand_tile_dims {
        // FIXME: Issue #2126
        input0.out_tile_dim_r == tensor.out_tile_dim_r && input0.out_tile_dim_c == tensor.out_tile_dim_c;
    }

    constraint rand_disable_transpose {
        gradient_op_en == 1 -> in[0].transpose_en == 0;
    }

    constraint rand_node_used {
        in[0].producer.is_node_used == is_node_used;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * (kernel_broadcast_en[0] ? kernel_broadcast_factors[0] : tensor.ublock_rt * tensor.ublock_ct) * `get_tile_size(input0.data_format);

        // TODO: why untilize defines size of output buffer
        untilize == 1 -> out_buffer == (2 *                   tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        untilize == 0 -> out_buffer == (2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));

        in0_buffer + out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    constraint rand_kernel_broadcast_freq {
        kernel_broadcast_op_en == 1 -> {
            kernel_broadcast_en[0] == 1;
        }
    }

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];

        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function string get_node_type();
        return get_unary_type(unary_op_type);
    endfunction

    virtual function integer get_num_inputs();
        return 1;
    endfunction

    virtual function int get_max_inputs();
        return 1;
    endfunction

    virtual function bit has_attributes();
        return super.has_attributes() || sfpu_vector_mode != vector_mode_rc;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        $fwrite(out_filehandle, "vector: %0s, ", get_vector_mode(sfpu_vector_mode));
    endfunction

    virtual function s_comparison_config get_comparison_config(int num_inputs);
        s_comparison_config cfg;
        cfg = tst_cfg.get_unary_comparison_config(tensor.data_format, unary_op_type);
        cfg.Type = "AllClose";
        cfg.verbosity = "Concise";
        return cfg;
    endfunction

    virtual function string get_stimulus_config(int input_id);
        if (input_id > 0) begin
            $fatal(0, "Invalid input id %0d", input_id);
        end

        stimulus_config_type = "Uniform";
        stimulus_config_uniform_lower_bound = -2.0;
        stimulus_config_uniform_upper_bound = 2.0;

        if (arch.unary_high_precision == 0 && (unary_op_type == sigmoid || unary_op_type == gelu)) begin
            stimulus_config_uniform_lower_bound = -1.0;
            stimulus_config_uniform_upper_bound = 1.0;
        end

        if (unary_op_type == sqrt) begin
            stimulus_config_uniform_lower_bound = 0.0;
            stimulus_config_uniform_upper_bound = 2.0;
        end else if (unary_op_type == reciprocal || unary_op_type == log) begin
        if (in[0].producer.tensor.data_format == bfp4 || in[0].producer.tensor.data_format == bfp4_b || output_data_format == bfp4 || output_data_format == bfp4_b) begin
            stimulus_config_uniform_lower_bound = 0.5;
        end else begin
            stimulus_config_uniform_lower_bound = 0.02;
        end
        end else if(gradient_op_en && dest_data_format == fp32) begin
            //Formats with 5 bits exponent and fp32 acc need larger range
            if(output_data_format == bfp2 || output_data_format == bfp4  || output_data_format == bfp8) begin
                stimulus_config_uniform_lower_bound = -5.0;
                stimulus_config_uniform_upper_bound = 5.0;
            end
        end

        return $sformatf(
            "{type: %s, uniform_lower_bound: %.2f, uniform_upper_bound: %.2f}",
            stimulus_config_type,
            stimulus_config_uniform_lower_bound,
            stimulus_config_uniform_upper_bound
        );
    endfunction
endclass

class feeder_drainer_op extends unary_op;
    bit is_feeder_on_sparse_input;

    function new(string name, bit is_feeder_op = 0);
        super.new(name);
        is_feeder_on_sparse_input = is_feeder_op;
        untilize_enabled = 0;
        gradient_op_enabled = 0;
        kernel_broadcast_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        this.input0 = in[0].post_tm_tensor;

        // Dimensions of the feeder / drainer are constrained by op dimensions
        this.input0.custom_block_dims = 1;
        this.in[0].producer.tensor.custom_block_dims = 1;
        this.tensor.custom_block_dims = 1;
    endfunction

    constraint constraint_sfpu_vector_mode {
        sfpu_vector_mode == vector_mode_rc;
    }

    constraint rand_disable_bfp2_bfp4_input_data_format {
       
    }

    constraint rand_output_data_format {
        if (dest_data_format == int32) {
            dequantize == 1 -> output_data_format inside {`FLOAT_OUTPUT_FORMATS};
            dequantize == 0 -> output_data_format inside {int32, int8};
            requantize == 1 -> output_data_format == int8;
            relu_en    == 1 -> output_data_format == int8 || output_data_format == fp32;
        } else {
            if (is_feeder_on_sparse_input == 1) {
                output_data_format inside {`FLOAT_FORMATS};
            }
            else {
                output_data_format inside {`FLOAT_OUTPUT_FORMATS};
            }
        }

        // TODO: Check if this works for int32
        untilize == 1 -> output_data_format inside {fp32, fp16_b, fp16};
    }

    constraint rand_unary_op_type {
        unary_op_type == datacopy;
    }

    constraint rand_input_data_format {
        input0.data_format == tensor.data_format;
    }

    constraint rand_srnd {
        stoch_rnd_mode == None;
    }

endclass

`endif // __UNARY_SFPU_OP_SV__