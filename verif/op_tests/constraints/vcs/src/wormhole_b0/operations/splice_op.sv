`ifndef __SPLICE_OP__
`define __SPLICE_OP__

`define MAX_INPUTS 4

class splice_op extends operation_constraints;

    rand integer num_inputs;
    rand tensor_constraints inputs[`MAX_INPUTS];

    rand bit direction;
    rand integer drop[`MAX_INPUTS];
    rand integer length[`MAX_INPUTS];
    rand integer stride[`MAX_INPUTS];

    rand integer output_buffer;

    function new(string name);
        super.new(name);
        this.node_type = "splice";

        // Enabled features
        m_k_enabled = 0;
        srnd_enabled = 1;
        relu_enabled = 1;
        quant_enabled = 0;
        accumulation_enabled = 0;
        bias_enabled = 0;
        l1_acc_enabled = 0;
        gradient_op_enabled = 0;

        tensor.tiny_tiles_enabled = 0;
    endfunction

    virtual function void initialize_inputs();
        $display("Initializing inputs %d", in.size());
        for (int i =0; i < in.size(); i++) begin
            in[i].is_dim_equal = 0;
            in[i].is_t_accumulation_enabled = 0;
            in[i].transpose_enabled = 0;
            inputs[i] = in[i].post_tm_tensor;
        end
    endfunction

    constraint rand_tensor_grid_size {
        direction == 0 -> tensor.grid_size_x == 1;
        direction == 1 -> tensor.grid_size_y == 1;
    }

    constraint rand_input_dims {
        foreach(inputs[i]){
            inputs[i].ublock_rt == tensor.ublock_rt;
            inputs[i].ublock_ct == tensor.ublock_ct;
        }
    }

    constraint rand_max_inputs {
        num_inputs inside {[4:`MAX_INPUTS]};
    }

    constraint rand_input_data_format {
        if (dest_data_format == int32) {
            inputs[0].data_format == int8;
        }

        foreach (inputs[i]) {
            (i > 0) -> inputs[i].data_format == inputs[i-1].data_format;
        }

        gradient_op_en == 1 -> inputs[0].data_format == tensor.data_format;
    }

    constraint rand_node_used {
        foreach (inputs[i]) {
            (i < num_inputs) -> in[i].producer.is_node_used == is_node_used;
            (i >= num_inputs) -> in[i].producer.is_node_used == 0;
        }
    }

    constraint rand_splice_attributes {
        foreach(inputs[i]) {
            if (i < num_inputs) {
                drop[i] inside {[1:16]};
                length[i] inside {[1:16]};
                stride[i] inside {[1:16]};
                stride[i] > length[i];
            } else {
                drop[i] == 0;
                length[i] == 0;
                stride[i] == 0;
            }
        }

        foreach(inputs[i]) {
            (i < num_inputs) -> {
                direction == 0 ->  drop[i] + length[i] + (stride[i] - length[i]) == inputs[i].mblock_n;
                direction == 1 ->  drop[i] + length[i] + (stride[i] - length[i]) == inputs[i].mblock_m * inputs[i].mblock_n;
                direction == 0 -> inputs[i].mblock_m * inputs[i].ublock_rt == tensor.mblock_m * tensor.ublock_rt;
                direction == 1 -> inputs[i].mblock_n == tensor.mblock_n;
            }
        }

        direction == 1 -> length.sum() == tensor.mblock_n*tensor.mblock_m;
        direction == 0 -> length.sum() == tensor.mblock_n;
    }

    constraint rand_fit_in_l1 {
        (gradient_op_en == 0 && untilize == 1) -> output_buffer == 2 * `get_ublock_size_in_bytes(tensor) * tensor.mblock_n;
        (gradient_op_en == 0 && untilize == 0) -> output_buffer == 2 * `get_ublock_size_in_bytes(tensor) * tensor.mblock_n * tensor.mblock_m;
        (gradient_op_en == 1 && untilize == 0) -> output_buffer == 2 * `get_ublock_size_in_bytes(tensor) * tensor.mblock_n * tensor.mblock_m * tensor.t;

        foreach (inputs[i]) {
            (i < num_inputs) -> 2 * `get_ublock_size_in_bytes(inputs[i]) + output_buffer  <= `MAX_L1_MEM_BUFFER_SIZE;
        }
    }

    virtual function bit has_attributes();
        return 1;
    endfunction

    virtual function write_attributes_to_file(int out_filehandle);
        super.write_attributes_to_file(out_filehandle);
        for (int i = 0; i < num_inputs; i++) begin
            $fwrite(out_filehandle, "input%0d: [%0d, %0d, %0d]", i, drop[i], length[i], stride[i]);
            if (i < num_inputs - 1) $fwrite(out_filehandle, ", ");
        end
    endfunction;

    virtual function integer get_num_inputs();
        return num_inputs;
    endfunction


    virtual function input_constraints get_op_input(int index);
        if (index < num_inputs) return in[index];
        $fatal(0, "Invalid index %0d", index);
    endfunction

    virtual function int get_max_inputs();
        return `MAX_INPUTS;
    endfunction
endclass;

`endif // __SPLICE_OP__