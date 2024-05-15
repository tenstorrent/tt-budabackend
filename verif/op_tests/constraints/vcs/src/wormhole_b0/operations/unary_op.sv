`ifndef __UNARY_OP_SV__
`define __UNARY_OP_SV__

class unary_op extends operation_constraints;
    rand tensor_constraints input0;

    rand integer in0_buffer;
    rand integer out_buffer;

    rand e_unary_type unary_op_type;

    function new(string name);
        super.new(name);
        this.node_type = "unary_op";

        // Enabled features
        m_k_enabled = 0;
        srnd_enabled = 1;
        relu_enabled = 0;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        gradient_op_enabled = 1;
        l1_acc_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        this.input0 = in[0].post_tm_tensor;
        this.in[0].transpose_enabled = 1;
    endfunction

    constraint rand_input_data_format {
        dest_data_format == int32 -> {
            input0.data_format == tensor.data_format;
            input0.data_format == int8 || int32;
            // Transpose is not supported for Int32
            in[0].transpose_en == 1 -> input0.data_format == int8;
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
        in0_buffer == 2 * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(input0.data_format);

        // TODO: why untilize defines size of output buffer
        untilize == 1 -> out_buffer == (2 *                   tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        untilize == 0 -> out_buffer == (2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));

        in0_buffer + out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
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

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        if (input0.out_tile_dim_r < 32) begin
            $fwrite(out_filehandle, "vector: r, ");
        end else if (input0.out_tile_dim_c < 32) begin
            $fwrite(out_filehandle, "vector: c, ");
        end
    endfunction

    virtual function string get_stimulus_config(int input_id);
        if (input_id > 0) begin
            $fatal(0, "Invalid input id %0d", input_id);
        end
        if (unary_op_type == sqrt) begin
            return "{type: Uniform, uniform_lower_bound: 0.0, uniform_upper_bound: 2.0}";
        end else if (unary_op_type == reciprocal || unary_op_type == log) begin
            if (get_input_data_format(input_id) inside {bfp4, bfp4_b, bfp4, bfp4_b}) begin
                return "{type: Uniform, uniform_lower_bound: 0.5, uniform_upper_bound: 2.0}";
            end else begin
                return "{type: Uniform, uniform_lower_bound: 0.1, uniform_upper_bound: 2.0}";
            end
        end else if(gradient_op_en && dest_data_format == fp32) begin
            //Formats with 5 bits exponent and fp32 acc need larger range
            if(output_data_format inside { bfp2, bfp4, bfp8 } ) begin
                return "{type: Uniform, uniform_lower_bound: -5.0, uniform_upper_bound: 5.0}";
            end
        end
        return super.get_stimulus_config(input_id);
    endfunction
endclass

class feeder_drainer_op extends unary_op;
    function new(string name);
        super.new(name);
        untilize_enabled = 0;
        gradient_op_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        this.input0 = in[0].post_tm_tensor;

        // Dimensions of the feeder / drainer are constrained by op dimensions
        this.input0.custom_block_dims = 1;
        this.in[0].producer.tensor.custom_block_dims = 1;
        this.tensor.custom_block_dims = 1;
    endfunction

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