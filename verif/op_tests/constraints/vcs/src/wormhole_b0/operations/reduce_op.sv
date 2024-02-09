`ifndef __REDUCE_OP__
`define __REDUCE_OP__

class reduce_op extends operation_constraints;

    rand integer in0_buffer;
    rand integer out_buffer;

    rand tensor_constraints input_0;

    rand e_reduce_type reduce_type;
    rand e_reduce_dim reduce_dim;

    function new(string name);
        super.new(name);
        this.node_type = "reduce";

        // Enabled features
        m_k_enabled = 1;
        srnd_enabled = 1;
        relu_enabled = 1;
        quant_enabled = 0;
        accumulation_enabled = 1;
        bias_enabled = 0;
        l1_acc_enabled = 0;
        gradient_op_enabled = 0;

    endfunction

    virtual function void initialize_inputs();
        in[0].is_dim_equal = 0;
        in[0].is_t_accumulation_enabled = 1;

        input_0 = in[0].post_tm_tensor;

        tensor.tiny_tiles_enabled = 0;
    endfunction


    virtual function int get_max_inputs();
        return 1;
    endfunction

    constraint rand_input_data_format {
        dest_data_format == int32 -> {
            input_0.data_format == int8;
        }

        dest_data_format == fp32 -> {
            input_0.data_format inside {bfp2_b, bfp4_b, bfp8_b, fp32, fp16_b};
        }
        input_0.data_format == output_data_format;
        intermed_data_format == output_data_format;
    }

    constraint rand_reduce_dim {
        if (reduce_dim == r) {
            tensor.grid_size_y == 1;
            tensor.mblock_m == 1;
            tensor.ublock_rt == 1;
            tensor.mblock_n == input_0.mblock_n;
            tensor.ublock_ct == input_0.ublock_ct;
            accumulate_en == 0;
            m_k == input_0.mblock_m;
            u_kt == input_0.ublock_rt;
            m_k > 1;
            u_kt > 1;
        } else if (reduce_dim == c) {
            tensor.grid_size_x == 1;
            tensor.mblock_n == 1;
            tensor.ublock_ct == 1;
            tensor.mblock_m == input_0.mblock_m;
            tensor.ublock_rt == input_0.ublock_rt;
            m_k == input_0.mblock_n;
            u_kt == input_0.ublock_ct;
            accumulate_en == 0;
            m_k > 1;
            u_kt > 1;
        } else if (reduce_dim == z) {
            tensor.mblock_m == input_0.mblock_m;
            tensor.mblock_n == input_0.mblock_n;
            tensor.ublock_rt == input_0.ublock_rt;
            tensor.ublock_ct == input_0.ublock_ct;
            _z > 1;
            m_k == 1;
            u_kt == 1;
            accumulate_en == 1;
        }
    }

    constraint rand_node_used {
        in[0].producer.is_node_used == is_node_used;
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(input_0.data_format);

        if (untilize == 1 && gradient_op_en == 0) {
            out_buffer == 2 * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format);
        } else if (untilize == 0 && gradient_op_en == 0) {
            out_buffer == 2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format);
        } else if (untilize == 0 && gradient_op_en == 1) {
            out_buffer == 2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format) * tensor.t;
        }
        in0_buffer +  out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
    }

    virtual function bit has_attributes();
        return 1;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        // due to parsing issue these attributes needs to be written before other attributes
        $fwrite(out_filehandle, "type: %0s, ", get_reduce_type(reduce_type));
        $fwrite(out_filehandle, "dim: %0s, ", get_reduce_dim(reduce_dim));
        super.write_attributes_to_file(out_filehandle);
    endfunction

    virtual function input_constraints get_op_input(int index);
        if (index == 0) return in[0];
        $fatal(0, "Invalid index %0d", index);
    endfunction

endclass // reduce_op

`endif // __REDUCE_OP__