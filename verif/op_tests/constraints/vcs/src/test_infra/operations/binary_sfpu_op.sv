// Quantization operations are binary operations that are using sfpu
`ifndef __BINARY_SFPU_OP__
`define __BINARY_SFPU_OP_

// macro to check if op is quantization
`define is_quantization_op(op_type)\
    (op_type == quantization || op_type == dequantization || op_type == requantization)

class binary_sfpu_op extends operation_constraints;
    rand e_binary_sfpu_type op_type;

    rand tensor_constraints input_0;
    rand tensor_constraints input_1;

    rand integer in0_buffer;
    rand integer in1_buffer;
    rand integer out_buffer;

    rand integer zero_point;

    function new(string name);
        super.new(name);
        this.node_type = "binary_sfpu_op";

        // Enabled features
        m_k_enabled = 0;
        srnd_enabled = 1;
        relu_enabled = 0;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        gradient_op_enabled = 0;
        l1_acc_enabled = 0;
        kernel_broadcast_enabled = 1;
    endfunction

    virtual function void initialize_inputs();
        in[0].broadcast_enabled = 1;
        in[1].broadcast_enabled = 1;
        this.input_0 = in[0].post_tm_tensor;
        this.input_1 = in[1].post_tm_tensor;
    endfunction

    constraint rand_tile_dims {
        // Not supported for now
        input_0.enable_tiny_tile == 0;
        input_1.enable_tiny_tile == 0;
        tensor.enable_tiny_tile == 0;
    }

    constraint rand_node_used {
        in[0].producer.is_node_used == is_node_used;
        in[1].producer.is_node_used == is_node_used;
    }

    constraint rand_dest_data_format {
        if (`is_quantization_op(op_type)) {
            dest_data_format == int32;
        }
    }

    constraint rand_intermed_data_format {
        if (`is_quantization_op(op_type)) {
            intermed_data_format == int32; // HACK -> this is override for operation constraints
        }
    }
    constraint rand_zero_point {
        if (`is_quantization_op(op_type)) {
            zero_point > -127 && zero_point < 127;
        }
    }

    constraint rand_input_data_format {
        if (`is_quantization_op(op_type)) {
            input_1.data_format == fp32;

            op_type == quantization -> {
                input_0.data_format == fp32;
            }

            op_type == dequantization -> {
                input_0.data_format inside {int8, int32};
            }

            op_type == requantization -> {
                input_0.data_format inside {int8, int32};
            }
        }
    }


    constraint rand_output_data_format {
        if (`is_quantization_op(op_type)) {
            op_type == quantization -> {
                tensor.data_format == int8;
            }

            op_type == dequantization -> {
                tensor.data_format == fp32;
            }

            op_type == requantization -> {
                tensor.data_format == int8;
            }
        }
    }

    constraint rand_fit_in_l1 {
        in0_buffer == 2 * (kernel_broadcast_en[0] ? kernel_broadcast_factors[0] : tensor.ublock_rt * tensor.ublock_ct) * `get_tile_size(input_0.data_format);
        in1_buffer == 2 * (kernel_broadcast_en[1] ? kernel_broadcast_factors[1] : tensor.ublock_rt * tensor.ublock_ct) * `get_tile_size(input_1.data_format);
        // TODO: why untilize defines size of output buffer
        untilize == 1 -> out_buffer == (2 *                   tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        untilize == 0 -> out_buffer == (2 * tensor.mblock_m * tensor.mblock_n * tensor.ublock_rt * tensor.ublock_ct * `get_tile_size(tensor.data_format));
        in0_buffer + in1_buffer + out_buffer <= `MAX_L1_MEM_BUFFER_SIZE;
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

    virtual function string get_node_type();
        return op_type.name();
    endfunction

    virtual function integer get_num_inputs();
        return 2;
    endfunction

    virtual function int get_max_inputs();
        return 2;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        $fwrite(out_filehandle, "zero_point: %0d, ", zero_point);
    endfunction

endclass

`endif // __BINARY_SFPU_OP__